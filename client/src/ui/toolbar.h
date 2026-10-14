#pragma once

#include "base.h"

union SDL_Event;
class Texture_Manager;
class Settings_Group;

enum Toolbar_Action : u32 {
    Toolbar_Action_None,
    Toolbar_Action_Pause,
    Toolbar_Action_Restart,
    Toolbar_Action_Mouse_Capture,
    Toolbar_Action_Save_State,
    Toolbar_Action_Load_State,
    Toolbar_Action_Quit,
};

struct Toolbar_State {
    bool paused;
    std::string pauseBinding;
    std::string restartBinding;
    std::string unthrottledBinding;
    std::string mouseCaptureBinding;
    Texture_Manager& textures;
    bool savestates = false;
    bool mouseCaptured = false;
};

Toolbar_Action Toolbar_Build(const Toolbar_State& state);
bool Toolbar_Draw_Setting(Settings_Group& group, std::string_view key);
bool Toolbar_Capture_Event(const SDL_Event& event);
bool Toolbar_Capturing();

s32 Binding_Pad_Button(std::string_view name);
s32 Binding_Mouse_Button(std::string_view name);
