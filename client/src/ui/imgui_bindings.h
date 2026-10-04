#pragma once

#include "module.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <string.h>
#include <algorithm>
#include <bit>

struct Imgui_Call {
    Module* module;
    wasm_module_inst_t instance;
    const char* function;
    bool failed;
};

void Imgui_Register_Natives();
void Imgui_Frame_Begin();
void Imgui_Enter_Module(const Module& module);
void Imgui_Leave_Module(const Module& module);
void Imgui_Release_Owner(const Module& owner);
void Imgui_Shutdown();

void Imgui_Register_Generated_Natives();
Imgui_Call Imgui_Enter(wasm_exec_env_t exec_env, const char* function);
void Imgui_Fail(Imgui_Call* call, const char* reason);
void* Imgui_Memory(Imgui_Call* call, u64 address, u64 size, bool nullable);
const char* Imgui_String(Imgui_Call* call, u64 address, bool required);
const char* Imgui_Text(Imgui_Call* call, u64 begin, u64 end);
const char* Imgui_Text_End(const char* text, u64 begin, u64 end);
const char* Imgui_Zero_Separated(Imgui_Call* call, u64 address);
const char* const* Imgui_Strings(Imgui_Call* call, u64 address, int count);
void* Imgui_Data(Imgui_Call* call, u64 address, ImGuiDataType type, int components, bool nullable);
const float* Imgui_Values(Imgui_Call* call, u64 address, int count, int stride);
ImTextureRef Imgui_Texture(Imgui_Call* call, u64 handle);
ImFont* Imgui_Font(Imgui_Call* call, u64 handle);
u64 Imgui_Font_Handle(const ImFont* font);
ImDrawList* Imgui_Draw_List(Imgui_Call* call, u64 handle);
u64 Imgui_Draw_List_Handle(ImDrawList* list);
ImGuiViewport* Imgui_Viewport(Imgui_Call* call, u64 id);
u64 Imgui_Viewport_Id(const ImGuiViewport* viewport);
u64 Imgui_String_Result(Imgui_Call* call, const char* text, u64 buffer, u64 capacity);
bool Imgui_Text_Link_Open_Url(const char* label, const char* url);
f32 Imgui_Finite(f32 value);

void Imgui_Push_Clip_Rect(ImDrawList* list, ImVec2 low, ImVec2 high, bool intersect);
void Imgui_Push_Clip_Rect_Full_Screen(ImDrawList* list);
void Imgui_Pop_Clip_Rect(Imgui_Call* call, ImDrawList* list);
void Imgui_Push_Texture(ImDrawList* list, ImTextureRef texture);
void Imgui_Pop_Texture(Imgui_Call* call, ImDrawList* list);

void Imgui_Guard_Range(Imgui_Call* call, int value, int low, int high);
void Imgui_Guard_Key(Imgui_Call* call, ImGuiKey key);
void Imgui_Guard_Key_Chord(Imgui_Call* call, ImGuiKeyChord chord);
void Imgui_Guard_Window(Imgui_Call* call, ImGuiWindowFlags flags);
void Imgui_Guard_Menu_Bar(Imgui_Call* call);
void Imgui_Guard_Group(Imgui_Call* call);
void Imgui_Guard_Tree(Imgui_Call* call);
void Imgui_Guard_Tab_Item(Imgui_Call* call);
void Imgui_Guard_Drag_Drop(Imgui_Call* call, bool source);
void Imgui_Guard_Table(Imgui_Call* call);
void Imgui_Guard_Table_Column(Imgui_Call* call, int column, bool need_table);
void Imgui_Guard_Table_Set_Column(Imgui_Call* call, int column);
void Imgui_Guard_Table_Background(Imgui_Call* call, ImGuiTableBgTarget target, int column);
void Imgui_Guard_Table_Freeze(Imgui_Call* call, int columns, int rows);
void Imgui_Guard_Column(Imgui_Call* call, int column, bool need_columns);

template <typename T>
T* Imgui_Pointer(Imgui_Call* call, u64 address, u64 count, bool nullable) {
    if (count > UINT64_MAX / sizeof(T) || address % alignof(T) != 0) {
        Imgui_Fail(call, "a pointer argument has an invalid count or alignment");
        return nullptr;
    }
    return static_cast<T*>(Imgui_Memory(call, address, count * sizeof(T), nullable));
}

inline f32 Imgui_F32(u64 slot) {
    return Imgui_Finite(std::bit_cast<f32>(static_cast<u32>(slot)));
}

inline f64 Imgui_F64(u64 slot) {
    return std::bit_cast<f64>(slot);
}

inline ImVec2 Imgui_Vec2(u64 slot) {
    return ImVec2(Imgui_F32(slot), Imgui_F32(slot >> 32));
}

inline ImVec4 Imgui_Vec4(u64 low, u64 high) {
    return ImVec4(Imgui_F32(low), Imgui_F32(low >> 32), Imgui_F32(high), Imgui_F32(high >> 32));
}

inline u64 Imgui_Pack(ImVec2 vector) {
    return std::bit_cast<u32>(vector.x) | static_cast<u64>(std::bit_cast<u32>(vector.y)) << 32;
}

inline const void* Imgui_Id(u64 slot) {
    return reinterpret_cast<const void*>(static_cast<uintptr_t>(slot));
}

inline void Imgui_Return_F32(u64* slots, f32 value) {
    slots[0] = std::bit_cast<u32>(value);
}

inline void Imgui_Return_F64(u64* slots, f64 value) {
    slots[0] = std::bit_cast<u64>(value);
}
