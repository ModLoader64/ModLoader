#pragma once

#include "ui/windows.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <deque>

enum class Window_State : u32 {
    Free,
    Asked,
    Open,
    Closing,
};

enum class Input_Kind : u32 {
    Move,
    Down,
    Up,
    Wheel,
    Leave,
    Key_Down,
    Key_Up,
    Text,
};

struct Window_Input {
    Input_Kind kind;
    s32 value; // button (left 0, right 1, middle 2) or key ID
    s32 modifiers;
    f32 x; // pixels or wheel delta
    f32 y;
    std::string text;
};

struct Frame_Batch {
    f32 transform[16];
    bool transformed;
    u32 firstDraw;
    u32 drawCount;
};

struct Window_Frame {
    std::vector<Renderer_Vertex> vertices;
    std::vector<u32> indices;
    std::vector<Renderer_Draw> draws;
    std::vector<Frame_Batch> batches;

    void Clear();
    void Draw(Renderer& renderer) const;
};

struct Module_Window {
    std::atomic<Window_State> state = Window_State::Free;
    u32 handle = 0;
    Module* owner = nullptr;
    bool overlay = false;
    std::string title;
    u32 width = 0;
    u32 height = 0;
    std::atomic<bool> shown = false;
    bool hidden = false;
    SDL_Window* window = nullptr;
    std::unique_ptr<Renderer_Surface> surface;
    Rml::Context* context = nullptr;
    std::deque<Window_Input> input;
    Window_Frame frame;
    s32 scissor[4] = {};
    bool scissored = false;
    f32 transform[16] = {};
    bool transformed = false;
};

struct Windows_State {
    std::mutex lock;
    Renderer* renderer = nullptr;
    SDL_Window* main = nullptr;
    u64 uiThread = 0;
    bool started = false;
    Module* worker = nullptr;
    Module_Window* current = nullptr;
    Rml::Context* debugged = nullptr;
    SDL_Cursor* cursors[SDL_SYSTEM_CURSOR_COUNT] = {};
    bool wantsKeyboard = false;
};

extern Windows_State gWindows;

SDL_Window* Shown_In(const Module_Window& window);
void Rml_Interfaces_Install();
