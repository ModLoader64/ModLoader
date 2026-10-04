#pragma once

#include "base.h"
#include "renderer/renderer.h"
#include "ui/theme.h"

union SDL_Event;
struct SDL_Window;
struct ImGuiContext;

class Ui {
public:
    Ui(Renderer& renderer, SDL_Window* window, const std::string& data_directory, Ui_Theme theme);
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;
    ~Ui();

    void Frame(const std::function<void()>& build);

    void Set_Theme(Ui_Theme theme) {
        pendingTheme = theme;
    }

    static void Status(const char* text);

    u32 Top_Inset() const {
        return topInset;
    }

    bool Process_Event(const SDL_Event& event);

    bool Wants_Keyboard() const {
        return wantKeyboard;
    }

    void Update_Textures();
    void Draw();
    void Draw_Windows();

private:
    std::string iniPath;
    ImGuiContext* context = nullptr;
    bool built = false;
    u32 topInset = 0;
    bool wantKeyboard = false;
    bool demo = false;
    std::optional<Ui_Theme> pendingTheme;
};
