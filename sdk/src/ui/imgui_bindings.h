#pragma once

#include <modloader/types.h>
#include <imgui.h>
#include <stdarg.h>

#define IMGUI_IMPORT(name) __attribute__((import_module("imgui"), import_name(name)))

namespace ModLoader::Runtime::Imgui {

// Each u64 carries two f32 values
u64 Pack(const ImVec2& vector);
u64 Pack_Low(const ImVec4& vector);
u64 Pack_High(const ImVec4& vector);
ImVec2 Unpack(u64 packed);
const char* Format(const char* format, va_list arguments);
char* Scratch_Text(u64 capacity);
// handle 0 maps to null
u64 Draw_List_Handle(const ImDrawList* list);
ImDrawList* Draw_List(u64 handle);
u64 Font_Handle(const ImFont* font);
ImFont* Font(u64 handle);
ImGuiID Viewport_Id(const ImGuiViewport* viewport);
ImGuiViewport* Viewport(ImGuiID id);

bool Frame_Begin();
void Frame_End();

} // namespace ModLoader::Runtime::Imgui
