#pragma once

#include <modloader/types.h>
#include <modloader/detail/input.h>
#include <string_view>
#include <vector>

namespace ModLoader::Input {

using Frame = ModLoader_Input_Frame;

class Binding {
public:
    void Set(std::string_view binding);
    f32 Value(const Frame& frame) const;

private:
    std::vector<ModLoader_Input_Binding> inputs;
};

bool Read(Frame* frame);
void Set_Mouse_Locked(bool locked);
void Toggle_Mouse_Lock();

} // namespace ModLoader::Input
