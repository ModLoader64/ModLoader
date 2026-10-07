#include "guest_layout.h"
#include "module.h"

#include <cstring>

namespace {

thread_local Layout_Execution* sLayoutExecution = nullptr;

void Report(Module& module, const std::string& error) {
    Log_Error(module.name.c_str(), "layout: %s", error.c_str());
}

NativeSymbol sNatives[] = {
    Native("layout_load", "(II)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        auto json = module.Text(slots[0], slots[1]);
        std::string error;
        slots[0] = json ? module.runtime.layouts.Load(module, *json, error) : 0;
        if (slots[0] == 0 && !error.empty()) {
            Report(module, error);
        }
    }),
    Native("layout_select", "(i)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        slots[0] = Layout_Execution::Select(module, wasm_runtime_get_module_inst(exec_env), static_cast<u32>(slots[0]));
    }),
    Native("layout_unload", "(i)", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        module.runtime.layouts.Unload(module, static_cast<u32>(slots[0]));
    }),
};

} // namespace

void Layouts_Register_Natives() {
    Register_Natives(sNatives, "guest layouts");
}

bool Layouts_Bind(Module& module) {
    s32 count = wasm_runtime_get_export_count(module.module);
    std::string error;
    for (s32 index = 0; index < count; index++) {
        wasm_export_t exported = {};
        wasm_runtime_get_export_type(module.module, index, &exported);
        std::string_view name = exported.name != nullptr ? exported.name : "";
        if (!name.starts_with(gLayoutPrefix)) {
            continue;
        }

        if (exported.kind == WASM_IMPORT_EXPORT_KIND_FUNC && name.ends_with(".describe")) {
            wasm_function_inst_t function = wasm_runtime_lookup_function(module.instance, exported.name);
            wasm_val_t result = {};
            if (wasm_func_type_get_param_count(exported.u.func_type) != 0 ||
                wasm_func_type_get_result_count(exported.u.func_type) != 1 ||
                !module.Call(function, "layout descriptor", {}, &result)) {
                return false;
            }

            u64 address = result.kind == WASM_I32 ? static_cast<u32>(result.of.i32) : static_cast<u64>(result.of.i64);
            const char* description = module.C_Text(address);
            if (description == nullptr || !module.runtime.layouts.Register(description, error)) {
                Report(module, error.empty() ? "invalid descriptor address" : error);
                return false;
            }
        }
        else if (exported.kind == WASM_IMPORT_EXPORT_KIND_GLOBAL) {
            wasm_global_inst_t global = {};
            if (!wasm_runtime_get_export_global_inst(module.instance, exported.name, &global) ||
                global.kind != WASM_I64 || !global.is_mutable || global.global_data == nullptr) {
                Report(module, "invalid slot " + std::string(name));
                return false;
            }
            u64 baseline;
            memcpy(&baseline, global.global_data, sizeof(baseline));
            module.layoutSlots.push_back({ std::string(name), baseline });
        }
    }

    if (!module.runtime.layouts.Refresh(error)) {
        Report(module, error);
        return false;
    }
    return true;
}

bool Layouts_Apply(Module& module, wasm_module_inst_t instance, const Guest_Layout_Snapshot& snapshot) {
    if (module.layoutSlots.empty()) {
        return true;
    }

    std::lock_guard guard(module.resourceLock);
    Module_Layout_Binding& binding = module.layoutBindings[instance];
    if (binding.addresses.size() != module.layoutSlots.size()) {
        binding.addresses.clear();
        binding.snapshot.reset();
        for (const Module_Layout_Slot& slot : module.layoutSlots) {
            wasm_global_inst_t global = {};
            if (!wasm_runtime_get_export_global_inst(instance, slot.name.c_str(), &global) ||
                global.kind != WASM_I64 || !global.is_mutable || global.global_data == nullptr) {
                binding.addresses.clear();
                return false;
            }
            binding.addresses.push_back(global.global_data);
        }
    }

    if (binding.snapshot == snapshot) {
        return true;
    }

    for (usize index = 0; index < module.layoutSlots.size(); index++) {
        const Module_Layout_Slot& slot = module.layoutSlots[index];
        auto found = snapshot->find(slot.name);
        u64 value = found == snapshot->end() ? slot.baseline : found->second;
        memcpy(binding.addresses[index], &value, sizeof(value));
    }

    binding.snapshot = snapshot;
    return true;
}

Layout_Execution::Layout_Execution(Module& owner, wasm_module_inst_t target) : module(owner), instance(target), previous(sLayoutExecution) {
    snapshot = previous != nullptr && &previous->module.runtime == &module.runtime ? previous->snapshot : module.runtime.layouts.Snapshot();
    ready = Layouts_Apply(module, instance, snapshot);
    sLayoutExecution = this;
}

Layout_Execution::~Layout_Execution() {
    sLayoutExecution = previous;
}

bool Layout_Execution::Select(Module& module, wasm_module_inst_t instance, u32 handle) {
    std::string error;
    auto selected = module.runtime.layouts.Select(module, handle, error);
    if (selected == nullptr) {
        Report(module, error);
        return false;
    }
    
    for (Layout_Execution* execution = sLayoutExecution; execution != nullptr; execution = execution->previous) {
        if (&execution->module.runtime == &module.runtime) {
            execution->snapshot = selected;
            if (!Layouts_Apply(execution->module, execution->instance, selected)) {
                return false;
            }
        }
    }
    return Layouts_Apply(module, instance, selected);
}
