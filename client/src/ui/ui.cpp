#include "ui/ui.h"
#include "ui/imgui_bindings.h"
#include "ui/imgui_renderer.h"
#include "backends/imgui_impl_sdl3.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <SDL3/SDL.h>
#include <string.h>

namespace {

ImGuiStyle sBaseStyle;
f32 sStyleScale = 1.0f;

void Scale_Style(f32 scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiStyle scaled = sBaseStyle;

    scaled.ScaleAllSizes(scale);
    memcpy(scaled.Colors, style.Colors, sizeof(style.Colors));
    scaled.FontSizeBase = style.FontSizeBase;
    scaled.FontScaleMain = style.FontScaleMain;
    scaled.FontScaleDpi = scale;
    style = scaled;
    sStyleScale = scale;
}

void Viewport_Changed(ImGuiViewport* viewport) {
    if (viewport->DpiScale > 0.0f && viewport->DpiScale != sStyleScale && GImGui->StyleVarStack.Size == 0) {
        Scale_Style(viewport->DpiScale);
    }
}

void Apply_Theme(Ui_Theme theme) {
    Ui_Theme_Style(theme, sBaseStyle);
    memcpy(ImGui::GetStyle().Colors, sBaseStyle.Colors, sizeof(sBaseStyle.Colors));
    Scale_Style(sStyleScale);
}

} // namespace

Ui::Ui(Renderer& renderer, SDL_Window* window, const std::string& data_directory, Ui_Theme theme) : iniPath(Path_Join(data_directory, "imgui.ini")) {
    f32 scale = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window));
    ImGuiIO* io;

    scale = scale > 0.0f ? scale : 1.0f;
    IMGUI_CHECKVERSION();
    context = ImGui::CreateContext();
    io = &ImGui::GetIO();
    io->IniFilename = iniPath.c_str();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    io->ConfigNavCaptureKeyboard = false; // leaves the client's hotkeys alone
    io->ConfigDpiScaleFonts = true;
    io->ConfigDpiScaleViewports = true;
    io->ConfigErrorRecoveryEnableAssert = false;
    io->Fonts->AddFontDefaultVector();
    sStyleScale = scale;
    Apply_Theme(theme);
    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui::GetPlatformIO().Platform_OnChangedViewport = Viewport_Changed;
    Imgui_Renderer_Init(renderer);
}

Ui::~Ui() {
    Imgui_Renderer_Shutdown();
    Imgui_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(context);
}

void Ui::Frame(const std::function<void()>& build) {
    ImGuiViewport* viewport;

    if (pendingTheme) {
        Apply_Theme(*pendingTheme);
        pendingTheme.reset();
    }

    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    viewport = ImGui::GetMainViewport();
    topInset = static_cast<u32>((viewport->WorkPos.y - viewport->Pos.y) * ImGui::GetIO().DisplayFramebufferScale.y + 0.5f);
    ImGui::DockSpaceOverViewport(0, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    if (demo) {
        ImGui::ShowDemoWindow(&demo);
    }

    if (build) {
        build();
    }

    ImGui::Render();
    ImGui::UpdatePlatformWindows();
    wantKeyboard = ImGui::GetIO().WantCaptureKeyboard;
    built = true;
}

void Ui::Status(const char* text) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    f32 size = ImGui::GetFontSize() * 1.5f;
    ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, 1e9f, 0.0f, text);
    ImVec2 position(viewport->Pos.x + (viewport->Size.x - extent.x) * 0.5f, viewport->Pos.y + (viewport->Size.y - extent.y) * 0.5f);
    ImGui::GetForegroundDrawList(viewport)->AddText(ImGui::GetFont(), size, position, IM_COL32_WHITE, text);
}

bool Ui::Process_Event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F1 && !event.key.repeat) {
        demo = !demo;
    }
    
    ImGui_ImplSDL3_ProcessEvent(&event);
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
        return !ImGui::GetIO().WantCaptureKeyboard;
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
        return !ImGui::GetIO().WantCaptureMouse;
    default:
        return true;
    }
}

void Ui::Update_Textures() {
    if (built) {
        Imgui_Renderer_Update_Textures();
    }
}

void Ui::Draw() {
    if (built) {
        Imgui_Renderer_Draw(ImGui::GetDrawData());
    }
}

void Ui::Draw_Windows() {
    if (built) {
        ImGui::RenderPlatformWindowsDefault();
    }
}
