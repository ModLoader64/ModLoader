#include "internal.h"
#include <modloader/input.h>

using namespace ModLoader;

void Input::Binding::Set(std::string_view binding) {
    inputs.resize(ModLoader_Host_Input_Binding_Parse(binding.data(), binding.size(), nullptr, 0));
    if (!inputs.empty()) {
        ModLoader_Host_Input_Binding_Parse(binding.data(), binding.size(), inputs.data(), inputs.size());
    }
}

f32 Input::Binding::Value(const Frame& frame) const {
    return Detail::Input_Binding_Value(inputs, frame);
}

void Input::Set_Mouse_Locked(bool locked) {
    ModLoader_Host_Input_Set_Mouse_Locked(locked ? 1 : 0);
}

void Input::Toggle_Mouse_Lock() {
    ModLoader_Host_Input_Toggle_Mouse_Lock();
}

bool Input::Read(Frame* frame) {
    if (frame == nullptr) {
        return false;
    }
    *frame = {};
    return ModLoader_Host_Input_Read(frame, sizeof(*frame)) != 0;
}
