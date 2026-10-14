#pragma once

#include <stdint.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <algorithm>
#include <span>

enum ModLoader_Input_Kind {
    MODLOADER_INPUT_NONE,
    MODLOADER_INPUT_KEY,
    MODLOADER_INPUT_PAD,
    MODLOADER_INPUT_AXIS,
    MODLOADER_INPUT_MOUSE,
};

struct ModLoader_Input_Binding {
    ModLoader_Input_Kind kind;
    int32_t code;
    float sign;
    uint32_t combined = 0;
};

struct ModLoader_Input_Frame {
    bool Focused;
    bool UiActive; // UI is consuming input
    bool ControllerConnected;
    bool MouseCaptured;
    float DeadZone;
    SDL_MouseButtonFlags MouseButtons;
    bool Keys[SDL_SCANCODE_COUNT];
    bool Buttons[SDL_GAMEPAD_BUTTON_COUNT];
    int16_t Axes[SDL_GAMEPAD_AXIS_COUNT];
    double MouseX;
    double MouseY;
    double MouseWheelX;
    double MouseWheelY;
    bool GyroSupported[SDL_SENSOR_GYRO_R + 1];
    float GyroRate[SDL_SENSOR_GYRO_R + 1][3];
    double GyroRotation[SDL_SENSOR_GYRO_R + 1][3]; // cumulative radians
};

namespace ModLoader::Detail {

inline float Input_Axis_Value(int16_t raw, float sign, float dead_zone) {
    const float value = std::min(raw / 32767.0f * sign, 1.0f);
    return value > dead_zone ? value : 0.0f;
}

inline float Input_Binding_Value(std::span<const ModLoader_Input_Binding> binding, const ModLoader_Input_Frame& frame) {
    float value = 0.0f;
    float chord = 0.0f;
    for (const ModLoader_Input_Binding& input : binding) {
        float part = 0.0f;
        switch (input.kind) {
        case MODLOADER_INPUT_KEY:
            part = frame.Keys[input.code] ? 1.0f : 0.0f;
            break;
        case MODLOADER_INPUT_PAD:
            part = frame.Buttons[input.code] ? 1.0f : 0.0f;
            break;
        case MODLOADER_INPUT_MOUSE:
            part = (frame.MouseButtons & SDL_BUTTON_MASK(input.code)) != 0 ? 1.0f : 0.0f;
            break;
        case MODLOADER_INPUT_AXIS:
            part = Input_Axis_Value(frame.Axes[input.code], input.sign, frame.DeadZone);
            break;
        default:
            break;
        }
        if (!input.combined) {
            value = std::max(value, chord);
            chord = part;
        }
        else {
            chord = std::min(chord, part);
        }
    }
    return std::max(value, chord);
}

} // namespace ModLoader::Detail
