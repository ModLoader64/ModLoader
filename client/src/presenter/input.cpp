#include "presenter.h"

#include "platform.h"
#include "ui/toolbar.h"
#include "ui/ui.h"
#include "ui/windows.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <string.h>

using ModLoader::Detail::Input_Binding_Value;
using ModLoader::Detail::Input_Axis_Value;

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

void Presenter::Set_Background_Controller_Input(bool enabled) {
    backgroundControllerInput = enabled;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, enabled ? "1" : "0");
}

std::vector<Presenter::Bound_Input> Presenter::Parse_Binding(std::string_view binding) {
    std::vector<Bound_Input> inputs;

    while (!binding.empty()) {
        usize end = binding.find('|');
        std::string_view alternative = binding.substr(0, end);
        binding.remove_prefix(end != std::string_view::npos ? end + 1 : binding.size());
        const usize first = inputs.size();
        while (!alternative.empty()) {
            end = alternative.find('&');
            std::string input(alternative.substr(0, end));
            alternative.remove_prefix(end != std::string_view::npos ? end + 1 : alternative.size());
            Bound_Input bound = {};
            if (input.starts_with("key:")) {
                SDL_Scancode key = SDL_GetScancodeFromName(input.c_str() + 4);
                if (key != SDL_SCANCODE_UNKNOWN) {
                    bound = { MODLOADER_INPUT_KEY, static_cast<s32>(key), 1.0f };
                }
            }
            else if (input.starts_with("pad:")) {
                bound = { MODLOADER_INPUT_PAD, Binding_Pad_Button(input.c_str() + 4), 1.0f };
            }
            else if (input.starts_with("mouse:")) {
                bound = { MODLOADER_INPUT_MOUSE, Binding_Mouse_Button(input.c_str() + 6), 1.0f };
            }
            else if (input.starts_with("axis:") && input.size() > 6 && (input.back() == '+' || input.back() == '-')) {
                f32 sign = input.back() == '-' ? -1.0f : 1.0f;
                input.pop_back();
                bound = { MODLOADER_INPUT_AXIS, static_cast<s32>(SDL_GetGamepadAxisFromString(input.c_str() + 5)), sign };
            }

            if (bound.kind == MODLOADER_INPUT_NONE || bound.code < 0
                || (end != std::string_view::npos && alternative.empty())) {
                inputs.resize(first);
                break;
            }

            bound.combined = inputs.size() != first;
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
    hotkeyAxisPast[static_cast<u32>(hotkey)] = false;
}

f32 Presenter::Input_Value(const Bound_Input& input, const bool* keys, bool focused) const {
    if (!focused && (input.kind == MODLOADER_INPUT_MOUSE || !backgroundControllerInput)) {
        return 0.0f;
    }

    switch (input.kind) {
    case MODLOADER_INPUT_KEY:
        return focused && keys[input.code] ? 1.0f : 0.0f;
    case MODLOADER_INPUT_MOUSE:
        return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(input.code)) != 0 ? 1.0f : 0.0f;
    case MODLOADER_INPUT_PAD:
        return gamepad != nullptr && SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(input.code)) ? 1.0f : 0.0f;
    case MODLOADER_INPUT_AXIS:
        return gamepad != nullptr ? Input_Axis_Value(SDL_GetGamepadAxis(gamepad, static_cast<SDL_GamepadAxis>(input.code)), input.sign, pendingInput.DeadZone) : 0.0f;
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

    bool focused = SDL_GetKeyboardFocus() == window;
    bool capture_mouse = mouseLock && focused && !Toolbar_Capturing();
    if (capture_mouse != mouseCaptured && SDL_SetWindowRelativeMouseMode(window, capture_mouse)) {
        mouseCaptured = capture_mouse;
        if (mouseCaptured) {
            s32 width = 0;
            s32 height = 0;
            SDL_GetWindowSize(window, &width, &height);
            SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_WARP_MOTION, "0", SDL_HINT_OVERRIDE);
            SDL_WarpMouseInWindow(window, static_cast<f32>(width) * 0.5f, static_cast<f32>(height) * 0.5f);
        }
        ui->Set_Mouse_Captured(mouseCaptured);
    }

    const bool ui_active = Toolbar_Capturing() || (!mouseCaptured && (ui->Wants_Keyboard() || Ui_Windows_Wants_Keyboard()));
    ModLoader_Input_Frame& frame = pendingInput;
    frame.Focused = focused;
    frame.UiActive = ui_active;
    frame.MouseCaptured = mouseCaptured && frame.Focused;
    frame.ControllerConnected = gamepad != nullptr;
    memcpy(frame.Keys, SDL_GetKeyboardState(nullptr), sizeof(frame.Keys));

    for (u32 index = 0; index < SDL_GAMEPAD_BUTTON_COUNT; index++) {
        frame.Buttons[index] = gamepad != nullptr && SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(index));
    }

    for (u32 index = 0; index < SDL_GAMEPAD_AXIS_COUNT; index++) {
        frame.Axes[index] = gamepad != nullptr ? SDL_GetGamepadAxis(gamepad, static_cast<SDL_GamepadAxis>(index)) : 0;
    }
    frame.MouseButtons = SDL_GetMouseState(nullptr, nullptr);

    ModLoader_Input_Frame game_controls = frame;
    if (!focused || ui_active) {
        memset(game_controls.Keys, 0, sizeof(game_controls.Keys));
    }

    if (ui_active || (!focused && !backgroundControllerInput)) {
        memset(game_controls.Buttons, 0, sizeof(game_controls.Buttons));
        memset(game_controls.Axes, 0, sizeof(game_controls.Axes));
    }

    if (!focused || ui_active || (!mouseCaptured && (ui->Wants_Mouse() || Ui_Windows_Wants_Mouse()))) {
        game_controls.MouseButtons = 0;
    }

    std::vector<f32> values(controls.size());
    for (usize control = 0; control < controls.size(); control++) {
        values[control] = Input_Binding_Value(controls[control], game_controls);
    }

    std::lock_guard lock(inputLock);
    rawInput = frame;
    if (platform != nullptr) {
        platform->Pack_Controls(values, inputState);
    }
}

void Presenter::Hotkey_Event(const SDL_Event& event, bool mouse_capture) {
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_MOUSE_BUTTON_DOWN
        && event.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.type != SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        return;
    }

    const bool focused = SDL_GetKeyboardFocus() == window;
    const bool controller = backgroundControllerInput || focused;
    const bool* keys = SDL_GetKeyboardState(nullptr);
    for (u32 hotkey = 0; hotkey < static_cast<u32>(Hotkey::Count); hotkey++) {
        if ((hotkey == static_cast<u32>(Hotkey::Mouse_Capture)) != mouse_capture) {
            continue;
        }

        bool pressed = false;
        bool axis_past = false;
        f32 chord = 0.0f;
        bool triggered = false;
        for (const Bound_Input& input : hotkeys[hotkey]) {
            if (!input.combined) {
                pressed = pressed || (triggered && chord > 0.5f);
                axis_past = axis_past || chord > 0.5f;
                chord = 1.0f;
                triggered = false;
            }

            f32 held = Input_Value(input, keys, focused);
            bool button_pressed = (event.type == SDL_EVENT_KEY_DOWN && input.kind == MODLOADER_INPUT_KEY && focused && !event.key.repeat && input.code == event.key.scancode)
                || (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && input.kind == MODLOADER_INPUT_MOUSE && focused && input.code == event.button.button)
                || (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && input.kind == MODLOADER_INPUT_PAD && controller && input.code == event.gbutton.button);

            if (button_pressed) {
                triggered = true;
                held = 1.0f;
            }
            else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && input.kind == MODLOADER_INPUT_AXIS
                && controller && input.code == event.gaxis.axis) {
                held = Input_Axis_Value(event.gaxis.value, input.sign, pendingInput.DeadZone);
                triggered = triggered || !hotkeyAxisPast[hotkey];
            }

            chord = std::min(chord, held);
        }

        pressed = pressed || (triggered && chord > 0.5f);
        axis_past = axis_past || chord > 0.5f;
        if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
            hotkeyAxisPast[hotkey] = axis_past;
        }

        if (pressed && mouse_capture && focused && !Toolbar_Capturing()) {
            Toggle_Mouse_Lock();
        }
        else if (pressed && !mouse_capture) {
            hotkeysPressed |= 1u << hotkey;
        }
    }
}

bool Presenter::Read_Input(ModLoader_Input_Frame& frame) const {
    std::lock_guard lock(inputLock);
    frame = rawInput;
    return true;
}

void Presenter::Enable_Gyro() {
    memset(pendingInput.GyroSupported, 0, sizeof(pendingInput.GyroSupported));
    memset(pendingInput.GyroRate, 0, sizeof(pendingInput.GyroRate));
    memset(gyroTimestamp, 0, sizeof(gyroTimestamp));
    for (SDL_SensorType sensor : { SDL_SENSOR_GYRO, SDL_SENSOR_GYRO_L, SDL_SENSOR_GYRO_R }) {
        pendingInput.GyroSupported[sensor] = gamepad != nullptr && SDL_GamepadHasSensor(gamepad, sensor) && SDL_SetGamepadSensorEnabled(gamepad, sensor, true);
    }
}

void Presenter::Input_Event(const SDL_Event& event) {
    if (event.type == wakeEvent && event.user.code != static_cast<s32>(Wake_Action::None)) {
        Wake_Action action = static_cast<Wake_Action>(event.user.code);
        mouseLock = action == Wake_Action::Toggle_Mouse_Lock ? !mouseLock : action == Wake_Action::Lock_Mouse;
        if (mouseLock && SDL_GetKeyboardFocus() != window) {
            SDL_RaiseWindow(window);
        }
    }

    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && event.window.windowID == SDL_GetWindowID(window)) {
        mouseLock = false;
        SDL_SetWindowRelativeMouseMode(window, false);
        mouseCaptured = false;
        ui->Set_Mouse_Captured(false);
    }

    if (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.windowID == SDL_GetWindowID(window)) {
        pendingInput.MouseX += static_cast<f64>(event.motion.xrel);
        pendingInput.MouseY += static_cast<f64>(event.motion.yrel);
    }
    else if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.windowID == SDL_GetWindowID(window)) {
        f64 direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0 : 1.0;
        pendingInput.MouseWheelX += static_cast<f64>(event.wheel.x) * direction;
        pendingInput.MouseWheelY += static_cast<f64>(event.wheel.y) * direction;
    }
    else if (event.type == SDL_EVENT_GAMEPAD_SENSOR_UPDATE && gamepad != nullptr && event.gsensor.which == SDL_GetGamepadID(gamepad)) {
        u32 sensor = event.gsensor.sensor;
        if (pendingInput.GyroSupported[sensor]) {
            u64 timestamp = event.gsensor.sensor_timestamp;
            u64 previous = gyroTimestamp[sensor];
            gyroTimestamp[sensor] = timestamp;
            f64 seconds = previous != 0 && timestamp > previous && timestamp - previous <= 250000000 ? static_cast<f64>(timestamp - previous) / 1000000000.0 : 0.0;
            for (u32 axis = 0; axis < 3; axis++) {
                pendingInput.GyroRate[sensor][axis] = event.gsensor.data[axis];
                pendingInput.GyroRotation[sensor][axis] += static_cast<f64>(event.gsensor.data[axis]) * seconds;
            }
        }
    }
}
