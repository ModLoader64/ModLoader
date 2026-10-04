#include "ui/theme.h"
#include "imgui.h"

namespace {

constexpr ImVec4 gRed = ImVec4(0.77f, 0.0f, 0.0f, 1.0f);
constexpr ImVec4 gRedMiddle = ImVec4(0.56f, 0.0f, 0.0f, 1.0f);
constexpr ImVec4 gRedDark = ImVec4(0.345f, 0.0f, 0.0f, 1.0f);
constexpr ImVec4 gRedBright = ImVec4(0.88f, 0.13f, 0.13f, 1.0f);
constexpr ImVec4 gWhite = ImVec4(0.973f, 0.973f, 0.973f, 1.0f);

ImVec4 Grey(f32 level, f32 alpha = 1.0f) {
    return ImVec4(level, level, level, alpha);
}

ImVec4 Alpha(const ImVec4& color, f32 alpha) {
    return ImVec4(color.x, color.y, color.z, alpha);
}

ImVec4 Mix(const ImVec4& from, const ImVec4& to, f32 amount) {
    return ImVec4(from.x + (to.x - from.x) * amount, from.y + (to.y - from.y) * amount, from.z + (to.z - from.z) * amount, from.w + (to.w - from.w) * amount);
}

void Dark_Colors(ImVec4* colors) {
    colors[ImGuiCol_Text] = Grey(0.95f);
    colors[ImGuiCol_TextDisabled] = Grey(0.54f);
    colors[ImGuiCol_WindowBg] = Grey(0.07f);
    colors[ImGuiCol_ChildBg] = Grey(0.0f, 0.0f);
    colors[ImGuiCol_PopupBg] = Grey(0.09f, 0.98f);
    colors[ImGuiCol_Border] = Grey(0.23f, 0.8f);
    colors[ImGuiCol_BorderShadow] = Grey(0.0f, 0.0f);
    colors[ImGuiCol_FrameBg] = Grey(0.14f);
    colors[ImGuiCol_FrameBgHovered] = Mix(Grey(0.14f), gRedMiddle, 0.3f);
    colors[ImGuiCol_FrameBgActive] = Mix(Grey(0.14f), gRed, 0.45f);
    colors[ImGuiCol_TitleBg] = Grey(0.05f);
    colors[ImGuiCol_TitleBgActive] = gRedDark;
    colors[ImGuiCol_TitleBgCollapsed] = Grey(0.0f, 0.6f);
    colors[ImGuiCol_MenuBarBg] = Grey(0.10f);
    colors[ImGuiCol_ScrollbarBg] = Grey(0.04f, 0.6f);
    colors[ImGuiCol_ScrollbarGrab] = Grey(0.23f);
    colors[ImGuiCol_ScrollbarGrabHovered] = Grey(0.31f);
    colors[ImGuiCol_ScrollbarGrabActive] = Grey(0.40f);
    colors[ImGuiCol_CheckMark] = gRedBright;
    colors[ImGuiCol_SliderGrab] = Mix(gRedMiddle, gRed, 0.6f);
    colors[ImGuiCol_SliderGrabActive] = gRedBright;
    colors[ImGuiCol_Button] = Alpha(gRedMiddle, 0.75f);
    colors[ImGuiCol_ButtonHovered] = Mix(gRedMiddle, gRed, 0.6f);
    colors[ImGuiCol_ButtonActive] = gRed;
    colors[ImGuiCol_Header] = Alpha(gRedMiddle, 0.45f);
    colors[ImGuiCol_HeaderHovered] = Alpha(Mix(gRedMiddle, gRed, 0.6f), 0.8f);
    colors[ImGuiCol_HeaderActive] = gRed;
    colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
    colors[ImGuiCol_SeparatorHovered] = Alpha(gRed, 0.78f);
    colors[ImGuiCol_SeparatorActive] = gRed;
    colors[ImGuiCol_ResizeGrip] = Alpha(gRed, 0.2f);
    colors[ImGuiCol_ResizeGripHovered] = Alpha(gRed, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = Alpha(gRed, 0.95f);
    colors[ImGuiCol_DockingEmptyBg] = Grey(0.09f);
    colors[ImGuiCol_PlotLines] = Grey(0.61f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
    colors[ImGuiCol_PlotHistogram] = gRed;
    colors[ImGuiCol_PlotHistogramHovered] = gRedBright;
    colors[ImGuiCol_TableHeaderBg] = Grey(0.125f);
    colors[ImGuiCol_TableBorderStrong] = Grey(0.23f);
    colors[ImGuiCol_TableBorderLight] = Grey(0.17f);
    colors[ImGuiCol_TableRowBg] = Grey(0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = Grey(1.0f, 0.04f);
    colors[ImGuiCol_TextLink] = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
    colors[ImGuiCol_TextSelectedBg] = Alpha(gRed, 0.4f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 0.8f, 0.0f, 0.9f);
    colors[ImGuiCol_UnsavedMarker] = gWhite;
    colors[ImGuiCol_NavCursor] = gRedBright;
    colors[ImGuiCol_NavWindowingHighlight] = Grey(1.0f, 0.7f);
    colors[ImGuiCol_NavWindowingDimBg] = Grey(0.8f, 0.2f);
    colors[ImGuiCol_ModalWindowDimBg] = Grey(0.0f, 0.55f);
}

void Light_Colors(ImVec4* colors) {
    colors[ImGuiCol_Text] = Grey(0.08f);
    colors[ImGuiCol_TextDisabled] = Grey(0.47f);
    colors[ImGuiCol_WindowBg] = Grey(0.957f);
    colors[ImGuiCol_ChildBg] = Grey(0.0f, 0.0f);
    colors[ImGuiCol_PopupBg] = Grey(0.98f, 0.98f);
    colors[ImGuiCol_Border] = Grey(0.70f, 0.8f);
    colors[ImGuiCol_BorderShadow] = Grey(0.0f, 0.0f);
    colors[ImGuiCol_FrameBg] = Grey(0.88f);
    colors[ImGuiCol_FrameBgHovered] = Mix(Grey(0.88f), gRed, 0.12f);
    colors[ImGuiCol_FrameBgActive] = Mix(Grey(0.88f), gRed, 0.22f);
    colors[ImGuiCol_TitleBg] = Grey(0.86f);
    colors[ImGuiCol_TitleBgActive] = Mix(gWhite, gRed, 0.2f);
    colors[ImGuiCol_TitleBgCollapsed] = Grey(1.0f, 0.5f);
    colors[ImGuiCol_MenuBarBg] = Grey(0.90f);
    colors[ImGuiCol_ScrollbarBg] = Grey(0.93f, 0.6f);
    colors[ImGuiCol_ScrollbarGrab] = Grey(0.72f);
    colors[ImGuiCol_ScrollbarGrabHovered] = Grey(0.62f);
    colors[ImGuiCol_ScrollbarGrabActive] = Grey(0.52f);
    colors[ImGuiCol_CheckMark] = gRedMiddle;
    colors[ImGuiCol_SliderGrab] = Alpha(gRed, 0.78f);
    colors[ImGuiCol_SliderGrabActive] = gRedMiddle;
    colors[ImGuiCol_Button] = Alpha(gRed, 0.18f);
    colors[ImGuiCol_ButtonHovered] = Alpha(gRed, 0.32f);
    colors[ImGuiCol_ButtonActive] = Alpha(gRed, 0.48f);
    colors[ImGuiCol_Header] = Alpha(gRed, 0.16f);
    colors[ImGuiCol_HeaderHovered] = Alpha(gRed, 0.28f);
    colors[ImGuiCol_HeaderActive] = Alpha(gRed, 0.42f);
    colors[ImGuiCol_Separator] = Grey(0.62f);
    colors[ImGuiCol_SeparatorHovered] = Alpha(gRedMiddle, 0.78f);
    colors[ImGuiCol_SeparatorActive] = gRed;
    colors[ImGuiCol_ResizeGrip] = Grey(0.35f, 0.17f);
    colors[ImGuiCol_ResizeGripHovered] = Alpha(gRed, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = Alpha(gRed, 0.95f);
    colors[ImGuiCol_DockingEmptyBg] = Grey(0.86f);
    colors[ImGuiCol_PlotLines] = Grey(0.39f);
    colors[ImGuiCol_PlotLinesHovered] = gRed;
    colors[ImGuiCol_PlotHistogram] = gRedMiddle;
    colors[ImGuiCol_PlotHistogramHovered] = gRedBright;
    colors[ImGuiCol_TableHeaderBg] = Grey(0.85f);
    colors[ImGuiCol_TableBorderStrong] = Grey(0.62f);
    colors[ImGuiCol_TableBorderLight] = Grey(0.78f);
    colors[ImGuiCol_TableRowBg] = Grey(0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = Grey(0.0f, 0.04f);
    colors[ImGuiCol_TextLink] = gRedMiddle;
    colors[ImGuiCol_TextSelectedBg] = Alpha(gRed, 0.25f);
    colors[ImGuiCol_DragDropTarget] = Alpha(gRed, 0.95f);
    colors[ImGuiCol_UnsavedMarker] = Grey(0.0f);
    colors[ImGuiCol_NavCursor] = Alpha(gRed, 0.8f);
    colors[ImGuiCol_NavWindowingHighlight] = Grey(0.7f, 0.7f);
    colors[ImGuiCol_NavWindowingDimBg] = Grey(0.2f, 0.2f);
    colors[ImGuiCol_ModalWindowDimBg] = Grey(0.2f, 0.35f);
}

} // namespace

Ui_Theme Ui_Theme_From_Name(std::string_view name) {
    return name == "light" ? Ui_Theme::Light : Ui_Theme::Dark;
}

void Ui_Theme_Style(Ui_Theme theme, ImGuiStyle& style) {
    ImVec4* colors = style.Colors;

    style = ImGuiStyle();
    if (theme == Ui_Theme::Light) {
        Light_Colors(colors);
    }
    else {
        Dark_Colors(colors);
    }
    
    colors[ImGuiCol_CheckboxSelectedBg] = Mix(colors[ImGuiCol_FrameBg], colors[ImGuiCol_FrameBgHovered], 0.65f);
    colors[ImGuiCol_InputTextCursor] = colors[ImGuiCol_Text];
    colors[ImGuiCol_TabHovered] = colors[ImGuiCol_HeaderHovered];
    colors[ImGuiCol_Tab] = theme == Ui_Theme::Light ? Grey(0.86f) : Grey(0.16f);
    colors[ImGuiCol_TabSelected] = theme == Ui_Theme::Light ? Mix(gWhite, gRed, 0.28f) : Mix(gRedMiddle, gRedDark, 0.5f);
    colors[ImGuiCol_TabSelectedOverline] = gRed;
    colors[ImGuiCol_TabDimmed] = Mix(colors[ImGuiCol_Tab], colors[ImGuiCol_TitleBg], 0.5f);
    colors[ImGuiCol_TabDimmedSelected] = Mix(colors[ImGuiCol_TabSelected], colors[ImGuiCol_TitleBg], 0.4f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = Grey(0.5f, 0.0f);
    colors[ImGuiCol_DockingPreview] = Alpha(gRed, 0.7f);
    colors[ImGuiCol_TreeLines] = colors[ImGuiCol_Border];
    colors[ImGuiCol_DragDropTargetBg] = Grey(0.0f, 0.0f);
    style.WindowRounding = 4.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
}
