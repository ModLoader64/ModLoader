#include "imgui_bindings.h"

#include <modloader/ui/imgui_dock.h>

#include "../internal.h"

#include <modloader/detail/mounts.h>
#include <modloader/memory.h>
#include <modloader/ui/ui.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Imgui = ModLoader::Runtime::Imgui;

extern "C" {

IMGUI_IMPORT("ImGui_GetIO") void ImGui_GetIO(ImGuiIO* out);
IMGUI_IMPORT("ImDrawList_AddRectsFilled") void ImDrawList_AddRectsFilled(u64 list, const void* rects, u64 count);
IMGUI_IMPORT("ImDrawList_AddTextRuns") void ImDrawList_AddTextRuns(u64 list, const char* text, u64 size, const void* runs, u64 count);
IMGUI_IMPORT("ImGui_DockBuilderHasNode") bool ImGui_DockBuilderHasNode(ImGuiID node);
IMGUI_IMPORT("ImGui_DockBuilderAddNode") ImGuiID ImGui_DockBuilderAddNode(ImGuiID node, ImGuiDockNodeFlags flags);
IMGUI_IMPORT("ImGui_DockBuilderRemoveNode") void ImGui_DockBuilderRemoveNode(ImGuiID node);
IMGUI_IMPORT("ImGui_DockBuilderSetNodeSize") void ImGui_DockBuilderSetNodeSize(ImGuiID node, u64 size);
IMGUI_IMPORT("ImGui_DockBuilderSplitNode") ImGuiID ImGui_DockBuilderSplitNode(ImGuiID node, ImGuiDir direction, f32 ratio, ImGuiID* out_at, ImGuiID* out_opposite);
IMGUI_IMPORT("ImGui_DockBuilderDockWindow") void ImGui_DockBuilderDockWindow(const char* window_name, ImGuiID node);
IMGUI_IMPORT("ImGui_DockBuilderFinish") void ImGui_DockBuilderFinish(ImGuiID node);
IMGUI_IMPORT("ImGui_GetStyle") void ImGui_GetStyle(ImGuiStyle* out);
IMGUI_IMPORT("ImGui_SetStyle") void ImGui_SetStyle(const void* before, const void* after);
IMGUI_IMPORT("ImGui_GetStyleColorVec4") void ImGui_GetStyleColorVec4(ImGuiCol color, ImVec4* out);
IMGUI_IMPORT("ImGui_GetViewport") bool ImGui_GetViewport(ImGuiID id, ImGuiViewport* out);
IMGUI_IMPORT("ImGui_CheckLayout") bool ImGui_CheckLayout(const char* version, const u64* sizes, u32 count);
IMGUI_IMPORT("ImGui_AcceptDragDropPayload") bool ImGui_AcceptDragDropPayload(const char* type, ImGuiDragDropFlags flags, ImGuiPayload* out);
IMGUI_IMPORT("ImGui_GetDragDropPayload") bool ImGui_GetDragDropPayload(ImGuiPayload* out);
IMGUI_IMPORT("ImGui_PayloadData") void ImGui_PayloadData(void* buffer, u64 size);
IMGUI_IMPORT("ImGui_TableGetSortSpecs") s32 ImGui_TableGetSortSpecs(ImGuiTableSortSpecs* out, ImGuiTableColumnSortSpecs* columns, s32 capacity);
IMGUI_IMPORT("ImFontAtlas_AddFontDefault") u64 ImFontAtlas_AddFontDefault(ModLoader_ImGui_Font_Kind kind, const ImFontConfig* config, u64 config_size);
IMGUI_IMPORT("ImFontAtlas_AddFontFromMemoryTTF") u64 ImFontAtlas_AddFontFromMemoryTTF(const void* data, s32 size, f32 size_pixels, const ImFontConfig* config, u64 config_size);
IMGUI_IMPORT("ImFontAtlas_AddFontFromMemoryCompressedTTF") u64 ImFontAtlas_AddFontFromMemoryCompressedTTF(const void* data, s32 size, f32 size_pixels, const ImFontConfig* config, u64 config_size);
IMGUI_IMPORT("ImFontAtlas_AddFontFromMemoryCompressedBase85TTF") u64 ImFontAtlas_AddFontFromMemoryCompressedBase85TTF(const char* text, f32 size_pixels, const ImFontConfig* config, u64 config_size);
IMGUI_IMPORT("ImFontAtlas_RemoveFont") void ImFontAtlas_RemoveFont(u64 font);
IMGUI_IMPORT("ImFont_LegacySize") f32 ImFont_LegacySize(u64 font);
IMGUI_IMPORT("ImDrawList_ChannelsSplit") void ImDrawList_ChannelsSplit(u64 list, s32 count);
IMGUI_IMPORT("ImDrawList_ChannelsMerge") void ImDrawList_ChannelsMerge(u64 list);
IMGUI_IMPORT("ImDrawList_ChannelsSetCurrent") void ImDrawList_ChannelsSetCurrent(u64 list, s32 channel);
IMGUI_IMPORT("ImGuiListClipper_Begin") u64 ImGuiListClipper_Begin(s32 items_count, f32 items_height);
IMGUI_IMPORT("ImGuiListClipper_Step") bool ImGuiListClipper_Step(u64 clipper, ImGuiListClipper* out);
IMGUI_IMPORT("ImGuiListClipper_End") void ImGuiListClipper_End(u64 clipper);
IMGUI_IMPORT("ImGuiListClipper_IncludeItemsByIndex") void ImGuiListClipper_IncludeItemsByIndex(u64 clipper, s32 begin, s32 end);
IMGUI_IMPORT("ImGuiListClipper_SeekCursorForItem") void ImGuiListClipper_SeekCursorForItem(u64 clipper, s32 item);
IMGUI_IMPORT("ImGuiStyle_Construct") void ImGuiStyle_Construct(ImGuiStyle* style, u64 size);
IMGUI_IMPORT("ImGuiStyle_ScaleAllSizes") void ImGuiStyle_ScaleAllSizes(ImGuiStyle* style, u64 size, f32 factor);
IMGUI_IMPORT("ImFontConfig_Construct") void ImFontConfig_Construct(ImFontConfig* config, u64 size);

} // extern "C"

namespace {

struct Draw_List_Stand_In {
    ImDrawList list;
    u64 handle;
};

struct Font_Stand_In {
    ImFont font;
    u64 handle;
};

enum class Layout : u8 {
    Unchecked,
    Matches,
    Differs,
};

Draw_List_Stand_In** sDrawLists; // by handle - 1
u32 sDrawListCount;
Font_Stand_In** sFonts;
u32 sFontCount;
ImGuiViewport** sViewports;
u32 sViewportCount;
alignas(ImGuiIO) u8 sIo[sizeof(ImGuiIO)];
alignas(ImGuiStyle) u8 sStyle[sizeof(ImGuiStyle)];
alignas(ImGuiStyle) u8 sStyleBefore[sizeof(ImGuiStyle)];
alignas(ImFontAtlas) u8 sAtlas[sizeof(ImFontAtlas)];
bool sIoFetched;
bool sStyleFetched;
Layout sLayout;
ImVec4 sStyleColors[ImGuiCol_COUNT];
ImGuiPayload sPayload;
ImGuiTableSortSpecs sSortSpecs;
ImGuiTableColumnSortSpecs* sSortColumns;
s32 sSortCapacity;
u8 sContext; // what GetCurrentContext points at

template <typename T>
bool Reserve(T*** array, u32 count) {
    T** grown = static_cast<T**>(realloc(*array, count * sizeof(T*)));

    if (grown == nullptr) {
        return false;
    }
    *array = grown;
    return true;
}

ImFontAtlas* Atlas() {
    return reinterpret_cast<ImFontAtlas*>(sAtlas);
}

// Translate the local font pointer to its host handle
const ImFontConfig* Crossing_Config(const ImFontConfig* config, ImFontConfig* out_copy) {
    if (config == nullptr) {
        return nullptr;
    }
    *out_copy = *config;
    out_copy->DstFont = reinterpret_cast<ImFont*>(static_cast<uintptr_t>(Imgui::Font_Handle(config->DstFont)));
    return out_copy;
}

// The host copies font data
ImFont* Add_Font_Memory(void* data, int size, float size_pixels, const ImFontConfig* config) {
    ImFontConfig copy;
    u64 font = ImFontAtlas_AddFontFromMemoryTTF(data, size, size_pixels, Crossing_Config(config, &copy), sizeof(ImFontConfig));

    if (config == nullptr || config->FontDataOwnedByAtlas) {
        IM_FREE(data);
    }
    return Imgui::Font(font);
}

const ImGuiPayload* Payload_With_Data() {
    sPayload.Data = nullptr;
    if (sPayload.DataSize > 0) {
        sPayload.Data = ModLoader::Memory::Scratch_Alloc(sPayload.DataSize);
        ImGui_PayloadData(sPayload.Data, sPayload.DataSize);
    }
    return &sPayload;
}

const char* const* Getter_Items(const char* (*getter)(void* user_data, int index), void* user_data, int count) {
    const char** items = static_cast<const char**>(ModLoader::Memory::Scratch_Alloc(sizeof(const char*) * (count > 0 ? count : 1)));
    const char* item;

    for (int index = 0; index < count; index++) {
        item = getter(user_data, index);
        items[index] = item != nullptr ? item : "*Unknown item*";
    }
    return items;
}

const float* Getter_Values(float (*getter)(void* data, int index), void* data, int count) {
    float* values = static_cast<float*>(ModLoader::Memory::Scratch_Alloc(sizeof(float) * (count > 0 ? count : 1)));

    for (int index = 0; index < count; index++) {
        values[index] = getter(data, index);
    }
    return values;
}

u64 Clipper_Handle(const ImGuiListClipper* clipper) {
    return reinterpret_cast<uintptr_t>(clipper->TempData);
}

} // namespace

u64 Imgui::Pack(const ImVec2& vector) {
    return __builtin_bit_cast(u32, vector.x) | static_cast<u64>(__builtin_bit_cast(u32, vector.y)) << 32;
}

u64 Imgui::Pack_Low(const ImVec4& vector) {
    return Pack(ImVec2(vector.x, vector.y));
}

u64 Imgui::Pack_High(const ImVec4& vector) {
    return Pack(ImVec2(vector.z, vector.w));
}

ImVec2 Imgui::Unpack(u64 packed) {
    return ImVec2(__builtin_bit_cast(f32, static_cast<u32>(packed)), __builtin_bit_cast(f32, static_cast<u32>(packed >> 32)));
}

char* Imgui::Scratch_Text(u64 capacity) {
    return static_cast<char*>(ModLoader::Memory::Scratch_Alloc(capacity, 1));
}

const char* Imgui::Format(const char* format, va_list arguments) {
    va_list copy;
    char* text = Scratch_Text(256);
    int length;

    va_copy(copy, arguments);
    length = vsnprintf(text, 256, format, copy);
    va_end(copy);
    if (length < 0) {
        return "";
    }

    if (length >= 256) {
        text = Scratch_Text(length + 1);
        vsnprintf(text, length + 1, format, arguments);
    }
    return text;
}

u64 Imgui::Draw_List_Handle(const ImDrawList* list) {
    return list != nullptr ? reinterpret_cast<const Draw_List_Stand_In*>(list)->handle : 0;
}

ImDrawList* Imgui::Draw_List(u64 handle) {
    Draw_List_Stand_In* stand_in;

    if (handle == 0) {
        return nullptr;
    }

    if (handle > sDrawListCount) {
        if (!Reserve(&sDrawLists, static_cast<u32>(handle))) {
            return nullptr;
        }
        memset(&sDrawLists[sDrawListCount], 0, (handle - sDrawListCount) * sizeof(Draw_List_Stand_In*));
        sDrawListCount = static_cast<u32>(handle);
    }

    if (sDrawLists[handle - 1] == nullptr) {
        stand_in = static_cast<Draw_List_Stand_In*>(malloc(sizeof(Draw_List_Stand_In)));
        if (stand_in == nullptr) {
            return nullptr;
        }
        new (&stand_in->list) ImDrawList(nullptr);
        stand_in->handle = handle;
        sDrawLists[handle - 1] = stand_in;
    }

    return &sDrawLists[handle - 1]->list;
}

u64 Imgui::Font_Handle(const ImFont* font) {
    return font != nullptr ? reinterpret_cast<const Font_Stand_In*>(font)->handle : 0;
}

ImFont* Imgui::Font(u64 handle) {
    Font_Stand_In* stand_in;

    if (handle == 0) {
        return nullptr;
    }

    for (u32 index = 0; index < sFontCount; index++) {
        if (sFonts[index]->handle == handle) {
            return &sFonts[index]->font;
        }
    }

    stand_in = static_cast<Font_Stand_In*>(malloc(sizeof(Font_Stand_In)));
    if (stand_in == nullptr || !Reserve(&sFonts, sFontCount + 1)) {
        free(stand_in);
        return nullptr;
    }

    new (&stand_in->font) ImFont();
    stand_in->handle = handle;
    stand_in->font.OwnerAtlas = Atlas();
    stand_in->font.FontId = static_cast<ImGuiID>(handle);
    stand_in->font.LegacySize = ImFont_LegacySize(handle);
    sFonts[sFontCount++] = stand_in;
    return &stand_in->font;
}

ImGuiID Imgui::Viewport_Id(const ImGuiViewport* viewport) {
    return viewport != nullptr ? viewport->ID : 0;
}

ImGuiViewport* Imgui::Viewport(ImGuiID id) {
    ImGuiViewport* viewport = nullptr;

    if (id == 0) {
        return nullptr;
    }

    for (u32 index = 0; index < sViewportCount && viewport == nullptr; index++) {
        if (sViewports[index]->ID == id) {
            viewport = sViewports[index];
        }
    }

    if (viewport == nullptr) {
        viewport = static_cast<ImGuiViewport*>(malloc(sizeof(ImGuiViewport)));
        if (viewport == nullptr || !Reserve(&sViewports, sViewportCount + 1)) {
            free(viewport);
            return nullptr;
        }
        new (viewport) ImGuiViewport();
        viewport->ID = id;
        sViewports[sViewportCount++] = viewport;
    }
    return ImGui_GetViewport(id, viewport) ? viewport : nullptr;
}

static_assert(sizeof(ModLoader::Ui::Rect_Run) == 20 && sizeof(ModLoader::Ui::Text_Run) == 20, "sizeof(ModLoader::Ui::Rect_Run) == 20 && sizeof(ModLoader::Ui::Text_Run) == 20");

void ModLoader::Ui::Add_Rects_Filled(ImDrawList* list, std::span<const Rect_Run> rects) {
    if (!rects.empty()) {
        ImDrawList_AddRectsFilled(Imgui::Draw_List_Handle(list), rects.data(), rects.size());
    }
}

void ModLoader::Ui::Add_Text_Runs(ImDrawList* list, std::string_view text, std::span<const Text_Run> runs) {
    if (!runs.empty()) {
        ImDrawList_AddTextRuns(Imgui::Draw_List_Handle(list), text.data(), text.size(), runs.data(), runs.size());
    }
}

bool Imgui::Frame_Begin() {
    const u64 layout[] = MODLOADER_IMGUI_LAYOUT;

    if (sLayout == Layout::Unchecked) {
        sLayout = ImGui_CheckLayout(IMGUI_VERSION, layout, IM_COUNTOF(layout)) ? Layout::Matches : Layout::Differs;
    }
    sIoFetched = false;
    sStyleFetched = false;
    return sLayout == Layout::Matches;
}

void Imgui::Frame_End() {
    if (sStyleFetched && memcmp(sStyle, sStyleBefore, sizeof(sStyle)) != 0) {
        ImGui_SetStyle(sStyleBefore, sStyle);
    }
    sIoFetched = false;
    sStyleFetched = false;
}

ImGuiContext* ImGui::GetCurrentContext() {
    return reinterpret_cast<ImGuiContext*>(&sContext);
}

void ImGui::SetCurrentContext(ImGuiContext*) {
}

void* ImGui::MemAlloc(size_t size) {
    return malloc(size);
}

void ImGui::MemFree(void* pointer) {
    free(pointer);
}

bool ImGui::DebugCheckVersionAndDataLayout(const char* version, size_t io, size_t style, size_t vector2, size_t vector4, size_t vertex, size_t index) {
    return strcmp(version, IMGUI_VERSION) == 0 && io == sizeof(ImGuiIO) && style == sizeof(ImGuiStyle) && vector2 == sizeof(ImVec2) &&
        vector4 == sizeof(ImVec4) && vertex == sizeof(ImDrawVert) && index == sizeof(ImDrawIdx) && sLayout != Layout::Differs;
}

ImGuiIO& ImGui::GetIO() {
    ImGuiIO* io = reinterpret_cast<ImGuiIO*>(sIo);

    if (!sIoFetched) {
        ImGui_GetIO(io);
        io->Fonts = Atlas();
        io->FontDefault = Imgui::Font(reinterpret_cast<uintptr_t>(io->FontDefault));
        sIoFetched = true;
    }
    return *io;
}

ImGuiStyle& ImGui::GetStyle() {
    ImGuiStyle* style = reinterpret_cast<ImGuiStyle*>(sStyle);

    if (!sStyleFetched) {
        ImGui_GetStyle(style);
        memcpy(sStyleBefore, sStyle, sizeof(sStyle));
        sStyleFetched = true;
    }
    return *style;
}

const ImVec4& ImGui::GetStyleColorVec4(ImGuiCol idx) {
    ImVec4* color = idx >= 0 && idx < ImGuiCol_COUNT ? &sStyleColors[idx] : &sStyleColors[0];

    ImGui_GetStyleColorVec4(idx, color);
    return *color;
}

ImGuiTableSortSpecs* ImGui::TableGetSortSpecs() {
    s32 count = ImGui_TableGetSortSpecs(&sSortSpecs, sSortColumns, sSortCapacity);
    ImGuiTableColumnSortSpecs* grown;

    if (count > sSortCapacity) {
        grown = static_cast<ImGuiTableColumnSortSpecs*>(realloc(sSortColumns, count * sizeof(ImGuiTableColumnSortSpecs)));
        if (grown == nullptr) {
            return nullptr;
        }
        sSortColumns = grown;
        sSortCapacity = count;
        count = ImGui_TableGetSortSpecs(&sSortSpecs, sSortColumns, sSortCapacity);
    }

    if (count < 0) {
        return nullptr;
    }
    sSortSpecs.Specs = sSortColumns;
    return &sSortSpecs;
}

const ImGuiPayload* ImGui::AcceptDragDropPayload(const char* type, ImGuiDragDropFlags flags) {
    return ImGui_AcceptDragDropPayload(type, flags, &sPayload) ? Payload_With_Data() : nullptr;
}

const ImGuiPayload* ImGui::GetDragDropPayload() {
    return ImGui_GetDragDropPayload(&sPayload) ? Payload_With_Data() : nullptr;
}

bool ImGui::Combo(
    const char* label,
    int* current_item,
    const char* (*getter)(void* user_data, int idx),
    void* user_data,
    int items_count,
    int popup_max_height_in_items
) {
    return ImGui::Combo(label, current_item, Getter_Items(getter, user_data, items_count), items_count, popup_max_height_in_items);
}

bool ImGui::ListBox(
    const char* label,
    int* current_item,
    const char* (*getter)(void* user_data, int idx),
    void* user_data,
    int items_count,
    int height_in_items
) {
    return ImGui::ListBox(label, current_item, Getter_Items(getter, user_data, items_count), items_count, height_in_items);
}

void ImGui::PlotLines(
    const char* label,
    float (*values_getter)(void* data, int idx),
    void* data,
    int values_count,
    int values_offset,
    const char* overlay_text,
    float scale_min,
    float scale_max,
    ImVec2 graph_size
) {
    ImGui::PlotLines(
        label,
        Getter_Values(values_getter, data, values_count),
        values_count,
        values_offset,
        overlay_text,
        scale_min,
        scale_max,
        graph_size,
        sizeof(float)
    );
}

void ImGui::PlotHistogram(
    const char* label,
    float (*values_getter)(void* data, int idx),
    void* data,
    int values_count,
    int values_offset,
    const char* overlay_text,
    float scale_min,
    float scale_max,
    ImVec2 graph_size
) {
    ImGui::PlotHistogram(
        label,
        Getter_Values(values_getter, data, values_count),
        values_count,
        values_offset,
        overlay_text,
        scale_min,
        scale_max,
        graph_size,
        sizeof(float)
    );
}

ImGuiStyle::ImGuiStyle() {
    ImGuiStyle_Construct(this, sizeof(*this));
}

void ImGuiStyle::ScaleAllSizes(float scale_factor) {
    ImGuiStyle_ScaleAllSizes(this, sizeof(*this), scale_factor);
}

ImFontConfig::ImFontConfig() {
    ImFontConfig_Construct(this, sizeof(*this));
}

ImFont::ImFont() {
    memset(static_cast<void*>(this), 0, sizeof(*this));
}

ImDrawList::ImDrawList(ImDrawListSharedData* shared_data) {
    memset(static_cast<void*>(this), 0, sizeof(*this));
    _Data = shared_data;
}

void ImDrawListSplitter::Split(ImDrawList* draw_list, int count) {
    ImDrawList_ChannelsSplit(Imgui::Draw_List_Handle(draw_list), count);
}

void ImDrawListSplitter::Merge(ImDrawList* draw_list) {
    ImDrawList_ChannelsMerge(Imgui::Draw_List_Handle(draw_list));
}

void ImDrawListSplitter::SetCurrentChannel(ImDrawList* draw_list, int channel_idx) {
    ImDrawList_ChannelsSetCurrent(Imgui::Draw_List_Handle(draw_list), channel_idx);
}

ImGuiListClipper::ImGuiListClipper() {
    memset(static_cast<void*>(this), 0, sizeof(*this));
    ItemsCount = -1;
}

ImGuiListClipper::~ImGuiListClipper() {
    End();
}

void ImGuiListClipper::Begin(int items_count, float items_height) {
    End();
    Ctx = ImGui::GetCurrentContext();
    ItemsCount = items_count;
    ItemsHeight = items_height;
    TempData = reinterpret_cast<void*>(static_cast<uintptr_t>(ImGuiListClipper_Begin(items_count, items_height)));
}

void ImGuiListClipper::End() {
    if (TempData != nullptr) {
        ImGuiListClipper_End(Clipper_Handle(this));
        TempData = nullptr;
    }
    ItemsCount = -1;
}

bool ImGuiListClipper::Step() {
    bool more = TempData != nullptr && ImGuiListClipper_Step(Clipper_Handle(this), this);

    if (!more) {
        TempData = nullptr;
        ItemsCount = -1;
    }
    return more;
}

void ImGuiListClipper::IncludeItemsByIndex(int item_begin, int item_end) {
    if (TempData != nullptr) {
        ImGuiListClipper_IncludeItemsByIndex(Clipper_Handle(this), item_begin, item_end);
    }
}

void ImGuiListClipper::SeekCursorForItem(int item_index) {
    if (TempData != nullptr) {
        ImGuiListClipper_SeekCursorForItem(Clipper_Handle(this), item_index);
    }
}

ImFont* ImFontAtlas::AddFont(const ImFontConfig* font_cfg) {
    return Add_Font_Memory(font_cfg->FontData, font_cfg->FontDataSize, font_cfg->SizePixels, font_cfg);
}

ImFont* ImFontAtlas::AddFontDefault(const ImFontConfig* font_cfg) {
    ImFontConfig copy;

    return Imgui::Font(ImFontAtlas_AddFontDefault(ModLoader_ImGui_Font_Kind::Default, Crossing_Config(font_cfg, &copy), sizeof(ImFontConfig)));
}

ImFont* ImFontAtlas::AddFontDefaultVector(const ImFontConfig* font_cfg) {
    ImFontConfig copy;

    return Imgui::Font(ImFontAtlas_AddFontDefault(ModLoader_ImGui_Font_Kind::Vector, Crossing_Config(font_cfg, &copy), sizeof(ImFontConfig)));
}

ImFont* ImFontAtlas::AddFontDefaultBitmap(const ImFontConfig* font_cfg) {
    ImFontConfig copy;

    return Imgui::Font(ImFontAtlas_AddFontDefault(ModLoader_ImGui_Font_Kind::Bitmap, Crossing_Config(font_cfg, &copy), sizeof(ImFontConfig)));
}

ImFont* ModLoader::Detail::Imgui_Load_Font(std::string_view folder, const char* filename, float size_pixels, const ImFontConfig* font_cfg) {
    std::optional<std::vector<u8>> file = File_Read_All(folder, filename);
    ImFontConfig copy;
    u64 font;

    if (!file) {
        return nullptr;
    }
    font = ImFontAtlas_AddFontFromMemoryTTF(file->data(), static_cast<s32>(file->size()), size_pixels, Crossing_Config(font_cfg, &copy), sizeof(ImFontConfig));
    return Imgui::Font(font);
}

ImFont* ImFontAtlas::AddFontFromMemoryTTF(void* font_data, int font_data_size, float size_pixels, const ImFontConfig* font_cfg, const ImWchar*) {
    return Add_Font_Memory(font_data, font_data_size, size_pixels, font_cfg);
}

ImFont* ImFontAtlas::AddFontFromMemoryCompressedTTF(
    const void* compressed_font_data,
    int compressed_font_data_size,
    float size_pixels,
    const ImFontConfig* font_cfg,
    const ImWchar*
) {
    ImFontConfig copy;

    return Imgui::Font(ImFontAtlas_AddFontFromMemoryCompressedTTF(
        compressed_font_data,
        compressed_font_data_size,
        size_pixels,
        Crossing_Config(font_cfg, &copy),
        sizeof(ImFontConfig)
    ));
}

ImFont* ImFontAtlas::AddFontFromMemoryCompressedBase85TTF(
    const char* compressed_font_data_base85,
    float size_pixels,
    const ImFontConfig* font_cfg,
    const ImWchar*
) {
    ImFontConfig copy;

    return Imgui::Font(
        ImFontAtlas_AddFontFromMemoryCompressedBase85TTF(compressed_font_data_base85, size_pixels, Crossing_Config(font_cfg, &copy), sizeof(ImFontConfig))
    );
}

void ImFontAtlas::RemoveFont(ImFont* font) {
    ImFontAtlas_RemoveFont(Imgui::Font_Handle(font));
}

bool ImGui::DockBuilderHasNode(ImGuiID node_id) {
    return ImGui_DockBuilderHasNode(node_id);
}

ImGuiID ImGui::DockBuilderAddNode(ImGuiID node_id, ImGuiDockNodeFlags flags) {
    return ImGui_DockBuilderAddNode(node_id, flags);
}

void ImGui::DockBuilderRemoveNode(ImGuiID node_id) {
    ImGui_DockBuilderRemoveNode(node_id);
}

void ImGui::DockBuilderSetNodeSize(ImGuiID node_id, ImVec2 size) {
    ImGui_DockBuilderSetNodeSize(node_id, ModLoader::Runtime::Imgui::Pack(size));
}

ImGuiID ImGui::DockBuilderSplitNode(
    ImGuiID node_id,
    ImGuiDir split_dir,
    float size_ratio_for_node_at_dir,
    ImGuiID* out_id_at_dir,
    ImGuiID* out_id_at_opposite_dir
) {
    return ImGui_DockBuilderSplitNode(node_id, split_dir, size_ratio_for_node_at_dir, out_id_at_dir, out_id_at_opposite_dir);
}

void ImGui::DockBuilderDockWindow(const char* window_name, ImGuiID node_id) {
    ImGui_DockBuilderDockWindow(window_name, node_id);
}

void ImGui::DockBuilderFinish(ImGuiID node_id) {
    ImGui_DockBuilderFinish(node_id);
}
