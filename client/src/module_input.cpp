#include "module.h"
#include "presenter.h"

#include <algorithm>

namespace {

NativeSymbol sNatives[] = {
    Native("input_binding_parse", "(IIII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        auto text = module.Text(slots[0], slots[1]);
        auto bindings = text ? Presenter::Parse_Binding(*text) : std::vector<Presenter::Bound_Input>{};
        if (slots[3] >= bindings.size() && !bindings.empty()) {
            auto* output = module.Memory_As<ModLoader_Input_Binding>(slots[2], bindings.size());
            if (output != nullptr) {
                std::copy(bindings.begin(), bindings.end(), output);
            }
        }
        slots[0] = bindings.size();
    }),
    Native("input_set_mouse_locked", "(i)", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        module.runtime.Host().Set_Mouse_Locked(slots[0] != 0);
    }),
    Native("input_toggle_mouse_lock", "()", [](wasm_exec_env_t exec_env, u64*) {
        Module& module = Module_Of(exec_env);
        module.runtime.Host().Toggle_Mouse_Lock();
    }),
    Native("input_read", "(II)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        ModLoader_Input_Frame* destination = slots[1] >= sizeof(ModLoader_Input_Frame)
            ? module.Memory_As<ModLoader_Input_Frame>(slots[0]) : nullptr;
        slots[0] = destination != nullptr && module.runtime.Host().Read_Input(*destination) ? 1 : 0;
    }),
};

} // namespace

void Module_Input_Register_Natives() {
    Register_Natives(sNatives, "input");
}
