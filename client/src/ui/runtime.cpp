#include "module.h"
#include "imgui_bindings.h"
#include "windows.h"
#include "modloader_events.h"
#include <algorithm>
#include <string.h>

namespace {
void Ui_Trapped(Module& module, const char* where) {
    module.Trap(module.uiContext.execEnv, where);
    Ui_Windows_Release_Owner(module);
}
} // namespace

void Runtime::Start_Ui_Instance(Module& module) {
    Module_Context context;
    if (!module.Create_Context(module.execEnv, context)) {
        Log_Error(module.name.c_str(), "cannot create UI context");
        return;
    }

    wasm_module_inst_t instance = wasm_runtime_get_module_inst(context.execEnv);
    wasm_function_inst_t phase = wasm_runtime_lookup_function(instance, "modloader_ui_phase");
    wasm_function_inst_t event = wasm_runtime_lookup_function(instance, "modloader_ui_event");
    wasm_function_inst_t reserve = wasm_runtime_lookup_function(instance, "modloader_ui_event_buffer");

    if (phase == nullptr || event == nullptr || reserve == nullptr) {
        Log_Error(module.name.c_str(), "missing UI exports");
        module.Destroy_Context(context);
        return;
    }

    std::lock_guard lock(uiLock);
    module.uiContext = context;
    module.uiPhase = phase;
    module.uiEvent = event;
    module.uiEventReserve = reserve;
    uiModules.push_back(&module);
}

void Runtime::Stop_Ui_Instance(Module& module) {
    std::lock_guard lock(uiLock);

    std::erase(uiModules, &module);
    Ui_Windows_Release_Owner(module);
    Imgui_Release_Owner(module);
    module.Destroy_Context(module.uiContext);
}

void Runtime::Ui() {
    std::lock_guard lock(uiLock);

    Imgui_Frame_Begin();
    Ui_Windows_Frame_Begin();
    auto deliver = [](Module* module, u32 phase) {
        if (module->uiContext.execEnv == nullptr || module->disabled || module->stopping) {
            return;
        }

        wasm_val_t argument = Wasm_I32(static_cast<s32>(phase));
        Imgui_Enter_Module(*module);
        if (!module->Execute(module->uiContext.execEnv, module->uiPhase, 0, nullptr, 1, &argument)) {
            Ui_Trapped(*module, "a UI phase");
        }

        Imgui_Leave_Module(*module);
    };

    for (u32 phase : { MODLOADER_EVENT_PHASE_PRE, MODLOADER_EVENT_PHASE_NORMAL }) {
        for (Module* module : uiModules) {
            deliver(module, phase);
        }
    }

    for (auto module = uiModules.rbegin(); module != uiModules.rend(); module++) {
        deliver(*module, MODLOADER_EVENT_PHASE_POST);
    }

    Ui_Windows_Frame_End();
}

u32 Runtime::Ui_Event(Module& module, u64 listener, const void* record, u64 size) {
    wasm_exec_env_t exec_env = module.uiContext.execEnv;
    wasm_val_t capacity = Wasm_I64(size);
    wasm_val_t address = Wasm_I64(0);

    if (module.disabled || exec_env == nullptr || module.uiEventReserve == nullptr ||
        !module.Execute(exec_env, module.uiEventReserve, 1, &address, 1, &capacity)) {
        return 0;
    }

    void* buffer = address.of.i64 != 0 ? module.Memory(static_cast<u64>(address.of.i64), size) : nullptr;
    if (size != 0 && buffer == nullptr) {
        return 0;
    }

    if (size != 0) {
        memcpy(buffer, record, size);
    }

    wasm_val_t arguments[2] = { Wasm_I64(listener), Wasm_I64(size) };
    wasm_val_t result = Wasm_I32(0);
    if (!module.Execute(exec_env, module.uiEvent, 1, &result, 2, arguments)) {
        Ui_Trapped(module, "a listener");
        return 0;
    }
    
    return static_cast<u32>(result.of.i32);
}

