#include "module.h"
#include "modloader_events.h"

#include <algorithm>
#include <string.h>

Module::Module(Runtime& owner, std::string module_name)
    : runtime(owner)
    , name(std::move(module_name)) {
}

Module::~Module() {
    runtime.Stop_Ui_Instance(*this);
    if (execEnv != nullptr) {
        wasm_runtime_destroy_exec_env(execEnv);
    }

    if (instance != nullptr) {
        wasm_runtime_deinstantiate(instance);
    }

    if (module != nullptr) {
        wasm_runtime_unload(module);
    }
}

void* Module::Memory(u64 address, u64 size) const {
    return wasm_runtime_validate_app_addr(instance, address, size) ? wasm_runtime_addr_app_to_native(instance, address) : nullptr;
}

std::optional<std::string_view> Module::Text(u64 address, u64 length) const {
    const char* text = length != 0 ? static_cast<const char*>(Memory(address, length)) : "";

    return text != nullptr ? std::optional(std::string_view(text, length)) : std::nullopt;
}

const char* Module::C_Text(u64 address) const {
    if (address == 0 || !wasm_runtime_validate_app_str_addr(instance, address)) {
        return nullptr;
    }
    return static_cast<const char*>(wasm_runtime_addr_app_to_native(instance, address));
}

bool Module::Call(wasm_function_inst_t function, const char* what, std::initializer_list<wasm_val_t> arguments, wasm_val_t* result) {
    wasm_val_t values[8];

    if (disabled || function == nullptr || execEnv == nullptr || arguments.size() > std::size(values)) {
        return false;
    }

    std::ranges::copy(arguments, values);
    if (Execute(execEnv, function, result != nullptr ? 1 : 0, result, static_cast<u32>(arguments.size()), values)) {
        return true;
    }

    Trap(execEnv, what);
    return false;
}

bool Module::Execute(wasm_exec_env_t context, wasm_function_inst_t function, u32 result_count, wasm_val_t* results, u32 argument_count, wasm_val_t* arguments) {
    Layout_Execution layout(*this, wasm_runtime_get_module_inst(context));
    return layout.Ready() && wasm_runtime_call_wasm_a(context, function, result_count, results, argument_count, arguments);
}

void Module::Trap(wasm_exec_env_t context, const char* what) {
    wasm_module_inst_t trapped = wasm_runtime_get_module_inst(context);
    const char* exception = wasm_runtime_get_exception(trapped);

    Log_Error(name.c_str(), "disabled after %s: %s", what, exception != nullptr ? exception : "unknown trap");
    wasm_runtime_clear_exception(trapped);
    disabled = true;
}

void* Module::Event_Memory(u64 size) {
    wasm_val_t result = Wasm_I64(0);

    return Call(eventReserve, "event buffer", { Wasm_I64(size) }, &result) && result.of.i64 != 0 ? Memory(static_cast<u64>(result.of.i64), size) : nullptr;
}

bool Module::Call_Handler(u64 handler, u32 processor, void* state, u64 size, bool& out_result) {
    void* buffer = Event_Memory(size);
    wasm_val_t result = {};

    out_result = false;
    if (buffer == nullptr) {
        return false;
    }

    memcpy(buffer, state, size);
    if (!Call(eventHypercall, "a handler", { Wasm_I64(handler), Wasm_I32(static_cast<s32>(processor)), Wasm_I64(size) }, &result)) {
        return false;
    }

    memcpy(state, buffer, size);
    out_result = result.of.i32 != 0;
    return true;
}

bool Module::Follows(u32 event_id) const {
    return std::ranges::binary_search(events, event_id);
}

void Module::Deliver(u32 event_id, const void* record, u64 record_size, bool subscribed) {
    if (disabled || (subscribed && !Follows(event_id))) {
        return;
    }

    Deliver_Phase(MODLOADER_EVENT_PHASE_PRE, event_id, record, record_size);
    Deliver_Phase(MODLOADER_EVENT_PHASE_NORMAL, event_id, record, record_size);
    Deliver_Phase(MODLOADER_EVENT_PHASE_POST, event_id, record, record_size);
}

void Module::Deliver_Phase(u32 phase, u32 event_id, const void* record, u64 record_size) {
    if (disabled) {
        return;
    }

    void* buffer = Event_Memory(record_size);
    if (buffer != nullptr) {
        memcpy(buffer, record, record_size);
        Call(eventPhase, "an event phase", { Wasm_I32(static_cast<s32>(phase)), Wasm_I32(static_cast<s32>(event_id)), Wasm_I64(record_size) });
    }
}

bool Module::Adopt_Instance(wasm_module_inst_t spawned) const {
    if (runtime.heapChain != nullptr && !wasm_runtime_attach_shared_heap(spawned, runtime.heapChain)) {
        Log_Error(name.c_str(), "cannot map guest memory for worker");
        return false;
    }

    for (const Module_Guest_Global& binding : guestGlobals) {
        wasm_global_inst_t destination = {};
        if (!wasm_runtime_get_export_global_inst(spawned, binding.name, &destination) || destination.global_data == nullptr || destination.kind != binding.global.kind) {
            return false;
        }

        memcpy(destination.global_data, binding.global.global_data, binding.global.kind == WASM_I64 ? sizeof(u64) : sizeof(u32));
    }
    return true;
}

bool Module::Create_Context(wasm_exec_env_t caller, Module_Context& out) {
    wasm_module_inst_t caller_instance = wasm_runtime_get_module_inst(caller);
    wasm_function_inst_t prepare = wasm_runtime_lookup_function(caller_instance, "modloader_thread_prepare");
    wasm_val_t record = Wasm_I64(0);

    if (prepare == nullptr || !Execute(caller, prepare, 1, &record, 0, nullptr) || record.of.i64 == 0) {
        return false;
    }

    Module_Context context;
    context.threadRecord = static_cast<u64>(record.of.i64);
    context.execEnv = wasm_runtime_spawn_exec_env(caller);
    if (context.execEnv != nullptr) {
        wasm_module_inst_t spawned = wasm_runtime_get_module_inst(context.execEnv);
        wasm_function_inst_t adopt = wasm_runtime_lookup_function(spawned, "modloader_thread_adopt");
        if (adopt != nullptr && Adopt_Instance(spawned) && Execute(context.execEnv, adopt, 0, nullptr, 1, &record)) {
            out = context;
            return true;
        }
        wasm_runtime_destroy_spawned_exec_env(context.execEnv);
        {
            std::lock_guard guard(resourceLock);
            layoutBindings.erase(spawned);
        }
    }

    wasm_function_inst_t discard = wasm_runtime_lookup_function(caller_instance, "modloader_thread_release");
    if (discard != nullptr) {
        Execute(caller, discard, 0, nullptr, 1, &record);
    }

    return false;
}

void Module::Destroy_Context(Module_Context& context) {
    if (context.execEnv == nullptr) {
        return;
    }

    wasm_module_inst_t spawned = wasm_runtime_get_module_inst(context.execEnv);
    wasm_function_inst_t release = wasm_runtime_lookup_function(spawned, "modloader_thread_release");
    wasm_val_t record = Wasm_I64(context.threadRecord);

    if (release != nullptr) {
        Execute(context.execEnv, release, 0, nullptr, 1, &record);
    }

    wasm_runtime_destroy_spawned_exec_env(context.execEnv);
    {
        std::lock_guard guard(resourceLock);
        layoutBindings.erase(spawned);
    }
    context = {};
}

const Module_Manifest* Module::Manifest_For_Folder(std::string_view folder) const {
    auto found = std::ranges::find(manifests, folder, &Module_Manifest::folder);
    return found != manifests.end() ? &*found : nullptr;
}

std::optional<std::string> Module::Owner_Folder(u64 folder_address, u64 folder_length) const {
    std::optional<std::string_view> folder = Text(folder_address, folder_length);

    if (folder && Manifest_For_Folder(*folder) != nullptr) {
        return std::string(*folder);
    }

    return std::nullopt;
}

std::string Module::Folder_Path(std::string_view folder) const {
    return Text_Format("%s/mods/%.*s", runtime.config.dataDirectory.c_str(), static_cast<int>(folder.size()), folder.data());
}

Module& Module_Of(wasm_exec_env_t exec_env) {
    return *static_cast<Module*>(wasm_runtime_get_custom_data(wasm_runtime_get_module_inst(exec_env)));
}

void Register_Natives(std::span<NativeSymbol> natives, const char* what, const char* import_module) {
    if (!wasm_runtime_register_natives_raw(import_module, natives.data(), static_cast<u32>(natives.size()))) {
        Log_Error("runtime", "cannot register %s imports", what);
    }
}

bool Require_Emulation_Thread(wasm_exec_env_t exec_env, const char* what) {
    std::string message;

    if (Module_Of(exec_env).runtime.On_Emulation_Thread()) {
        return true;
    }
    message = Text_Format("%s requires the emulation thread", what);
    wasm_runtime_set_exception(wasm_runtime_get_module_inst(exec_env), message.c_str());
    return false;
}
