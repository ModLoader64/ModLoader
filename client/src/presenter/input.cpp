#include "presenter.h"

#include "platform.h"
#include "ui/toolbar.h"
#include "ui/ui.h"
#include "ui/windows.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <string.h>

namespace {
constexpr f32 gDeadZone = 6000.0f / 32767.0f;
} // namespace

bool Presenter::Poll_Input(u32 port, void* out_state, u64 size) const {
    std::lock_guard lock(inputLock);
    if (port != 0 || out_state == nullptr || size != inputState.size() || inputState.empty()) {
        return false;
    }
    memcpy(out_state, inputState.data(), size);
    return true;
}

void Presenter::Set_Rumble(u32 port, u32 strength) {
    if (port == 0) {
        rumbleRequested = strength != 0;
    }
}

void Presenter::Set_Controls(const Platform& controlled) {
    platform = &controlled;
    controls.assign(controlled.controls.size(), {});
}

std::vector<Presenter::Bound_Input> Presenter::Parse_Binding(std::string_view binding) {
    std::vector<Bound_Input> inputs;

    while (!binding.empty()) {
        usize end = binding.find('|');
        std::string input(binding.substr(0, end));
        binding.remove_prefix(end != std::string_view::npos ? end + 1 : binding.size());
        Bound_Input bound;
        if (input.starts_with("key:")) {
            SDL_Scancode key = SDL_GetScancodeFromName(input.c_str() + 4);
            if (key != SDL_SCANCODE_UNKNOWN) {
                bound = { Input_Kind::Key, static_cast<s32>(key), 1.0f };
            }
        }
        else if (input.starts_with("pad:")) {
            bound = { Input_Kind::Pad, Binding_Pad_Button(input.c_str() + 4), 1.0f };
        }
        else if (input.starts_with("axis:") && input.size() > 6) {
            f32 sign = input.back() == '-' ? -1.0f : 1.0f;
            input.pop_back();
            bound = { Input_Kind::Axis, static_cast<s32>(SDL_GetGamepadAxisFromString(input.c_str() + 5)), sign };
        }

        if (bound.kind != Input_Kind::None && bound.code >= 0) {
            inputs.push_back(bound);
        }
    }
    return inputs;
}

void Presenter::Bind(u32 control, std::string_view binding) {
    if (control < controls.size()) {
        controls[control] = Parse_Binding(binding);
    }
}

void Presenter::Bind_Hotkey(Hotkey hotkey, std::string_view binding) {
    hotkeys[static_cast<u32>(hotkey)] = Parse_Binding(binding);
}

f32 Presenter::Input_Value(const Bound_Input& input, const bool* keys, bool keyboard) const {
    f32 value;

    switch (input.kind) {
    case Input_Kind::Key:
        return keyboard && keys[input.code] ? 1.0f : 0.0f;
    case Input_Kind::Pad:
        return gamepad != nullptr && SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(input.code)) ? 1.0f : 0.0f;
    case Input_Kind::Axis:
        value = gamepad != nullptr ? SDL_GetGamepadAxis(gamepad, static_cast<SDL_GamepadAxis>(input.code)) / 32767.0f * input.sign : 0.0f;
        return value > gDeadZone ? std::min(value, 1.0f) : 0.0f;
    default:
        return 0.0f;
    }
}

void Presenter::Update_Input() {
    s32 rumble = rumbleRequested.exchange(-1);
    if (rumble >= 0 && gamepad != nullptr) {
        u16 intensity = rumble != 0 ? 0xC000 : 0;
        SDL_RumbleGamepad(gamepad, intensity, intensity, rumble != 0 ? 1000 : 0);
    }

    if (platform == nullptr) {
        return;
    }
    
    const bool* keys = SDL_GetKeyboardState(nullptr);
    std::vector<f32> values(controls.size());
    bool keyboard = SDL_GetKeyboardFocus() == window && !ui->Wants_Keyboard() && !Ui_Windows_Wants_Keyboard() && !Toolbar_Capturing();
    for (usize control = 0; control < controls.size(); control++) {
        for (const Bound_Input& input : controls[control]) {
            values[control] = std::max(values[control], Input_Value(input, keys, keyboard));
        }
    }

    std::lock_guard lock(inputLock);
    platform->Pack_Controls(values, inputState);
}

void Presenter::Hotkey_Event(const SDL_Event& event) {
    bool pressed;
    bool past;

    for (u32 hotkey = 0; hotkey < static_cast<u32>(Hotkey::Count); hotkey++) {
        pressed = false;
        for (const Bound_Input& input : hotkeys[hotkey]) {
            if (event.type == SDL_EVENT_KEY_DOWN && input.kind == Input_Kind::Key) {
                pressed = pressed || (!event.key.repeat && input.code == event.key.scancode);
            }
            else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && input.kind == Input_Kind::Pad) {
                pressed = pressed || input.code == event.gbutton.button;
            }
            else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && input.kind == Input_Kind::Axis && input.code == event.gaxis.axis) {
                past = event.gaxis.value * input.sign > 16384.0f;
                pressed = pressed || (past && !hotkeyAxisPast[hotkey]);
                hotkeyAxisPast[hotkey] = past;
            }
        }
        
        if (pressed) {
            hotkeysPressed |= 1u << hotkey;
        }
    }
}

