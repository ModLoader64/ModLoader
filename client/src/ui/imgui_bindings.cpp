#include "ui/imgui_bindings.h"
#include "ui/handle_table.h"
#include <SDL3/SDL_init.h>
#include <float.h>
#include <stdio.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr u64 gLayout[] = MODLOADER_IMGUI_LAYOUT;

struct Module_Texture {
    ImTextureData* data;
    const Module* owner;
    std::vector<u8> pixels;
    u32 width;
    u32 height;
};

struct Module_Font {
    ImGuiID id;
    const Module* owner; // null after removal
};

struct Tracked_Draw_List {
    ImDrawList* list;
    u64 handedFrame;
    u64 pushFrame;
    u32 clipPushes;
    u32 texturePushes;
};

struct Module_Clipper {
    ImGuiListClipper* clipper;
    const Module* owner;
    u64 id;
};

struct Bindings {
    std::mutex lock;
    Handle_Table<Module_Texture> textures;
    std::vector<ImTextureData*> dying;
    std::vector<Module_Font> fonts;
    std::vector<Tracked_Draw_List> drawLists;
    std::vector<Module_Clipper> clippers;
    u64 nextClipper = 0;
    std::vector<const char*> strings;
    u64 frame = 0;
    const Module* current = nullptr;
    ImGuiErrorRecoveryState recovery;
};

Bindings sBindings;

Module_Texture* Find_Texture(const Module* owner, u64 handle) {
    Module_Texture* texture = handle <= 0xFFFFFFFFull ? sBindings.textures.Find(static_cast<u32>(handle)) : nullptr;
    return texture != nullptr && texture->owner == owner ? texture : nullptr;
}

void Take_Pixels(Module_Texture& texture) {
    if (texture.pixels.empty()) {
        return;
    }

    if (texture.data == nullptr) {
        texture.data = IM_NEW(ImTextureData)();
        texture.data->Create(ImTextureFormat_RGBA32, texture.width, texture.height);
        texture.data->UseColors = true;
        ImGui::RegisterUserTexture(texture.data);
    }

    memcpy(texture.data->Pixels, texture.pixels.data(), texture.pixels.size());
    if (texture.data->Status == ImTextureStatus_OK || texture.data->Status == ImTextureStatus_WantUpdates) {
        ImTextureDataQueueUpload(texture.data, 0, 0, texture.width, texture.height);
    }

    texture.pixels = {};
}

bool In_Module_Ui(const Module& module, wasm_exec_env_t exec_env) {
    return SDL_IsMainThread() && sBindings.current == &module && exec_env == module.uiContext.execEnv;
}

u64 Texture_Create(Module& module, wasm_exec_env_t exec_env, u32 width, u32 height, u64 address) {
    const ImGuiPlatformIO& renderer = ImGui::GetPlatformIO();
    bool sized = width != 0 && height != 0 && width <= static_cast<u32>(renderer.Renderer_TextureMaxWidth) && height <= static_cast<u32>(renderer.Renderer_TextureMaxHeight) && static_cast<u64>(width) * height <= SIZE_MAX / 4;
    u64 bytes = static_cast<u64>(width) * height * 4;
    const u8* source = sized ? module.Memory_As<u8>(address, bytes) : nullptr;
    std::lock_guard lock(sBindings.lock);
    u32 handle;

    if (source == nullptr) {
        return 0;
    }

    handle = sBindings.textures.Add({ nullptr, &module, std::vector<u8>(source, source + bytes), width, height });
    if (handle != 0 && In_Module_Ui(module, exec_env)) {
        Take_Pixels(*sBindings.textures.Find(handle));
    }

    return handle;
}

bool Texture_Update(Module& module, wasm_exec_env_t exec_env, u64 handle, u64 address) {
    std::lock_guard lock(sBindings.lock);
    Module_Texture* texture = Find_Texture(&module, handle);
    const u8* source = texture != nullptr ? module.Memory_As<u8>(address, static_cast<u64>(texture->width) * texture->height * 4) : nullptr;

    if (source == nullptr) {
        return false;
    }

    texture->pixels.assign(source, source + static_cast<u64>(texture->width) * texture->height * 4);
    if (In_Module_Ui(module, exec_env)) {
        Take_Pixels(*texture);
    }

    return true;
}

void Update_Textures() {
    std::lock_guard lock(sBindings.lock);

    sBindings.textures.For_Each([](u32 index, Module_Texture& texture) {
        if (texture.owner != nullptr) {
            Take_Pixels(texture);
            return;
        }

        if (texture.data != nullptr) {
            texture.data->WantDestroyNextFrame = true;
            sBindings.dying.push_back(texture.data);
        }

        sBindings.textures.Remove(index);
    });
    std::erase_if(sBindings.dying, [](ImTextureData* data) {
        if (data->Status != ImTextureStatus_Destroyed) {
            return false;
        }

        ImGui::UnregisterUserTexture(data);
        IM_DELETE(data);
        return true;
    });
}

ImFont* Font_Of(ImGuiID id) {
    for (ImFont* font : ImGui::GetIO().Fonts->Fonts) {
        if (font->FontId == id) {
            return font;
        }
    }
    return nullptr;
}

void Update_Fonts() {
    std::erase_if(sBindings.fonts, [](const Module_Font& added) {
        ImFont* font = added.owner == nullptr ? Font_Of(added.id) : nullptr;
        if (font != nullptr) {
            ImGui::GetIO().Fonts->RemoveFont(font);
        }
        return added.owner == nullptr;
    });
}

bool Font_Config(Imgui_Call* call, u64 address, u64 size, ImFontConfig* out_config) {
    const ImFontConfig* config = Imgui_Pointer<const ImFontConfig>(call, address, 1, true);

    *out_config = ImFontConfig();
    if (config == nullptr) {
        return !call->failed;
    }

    if (size != sizeof(ImFontConfig)) {
        Imgui_Fail(call, "the module's ImFontConfig differs from the client's");
        return false;
    }

    *out_config = *config;
    out_config->FontData = nullptr;
    out_config->FontDataSize = 0;
    out_config->FontDataOwnedByAtlas = true;
    out_config->GlyphRanges = nullptr;
    out_config->GlyphExcludeRanges = nullptr;
    out_config->FontLoader = nullptr; 
    out_config->FontLoaderData = nullptr;
    out_config->DstFont = Imgui_Font(call, reinterpret_cast<uintptr_t>(config->DstFont));
    return !call->failed;
}

u64 Own_Font(Imgui_Call* call, const ImFontConfig* config, ImFont* font) {
    if (font == nullptr) {
        return 0;
    }

    if (!config->MergeMode) {
        sBindings.fonts.push_back({ font->FontId, call->module });
    }

    return Imgui_Font_Handle(font);
}

void Native_Add_Font_Default(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImFontAtlas::AddFontDefault");
    auto kind = static_cast<ModLoader_ImGui_Font_Kind>(slots[0]);
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    ImFontConfig config;
    ImFont* font;

    slots[0] = 0;
    if (call.failed || !Font_Config(&call, slots[1], slots[2], &config)) {
        return;
    }

    switch (kind) {
    case ModLoader_ImGui_Font_Kind::Vector:
        font = atlas->AddFontDefaultVector(&config);
        break;
    case ModLoader_ImGui_Font_Kind::Bitmap:
        font = atlas->AddFontDefaultBitmap(&config);
        break;
    case ModLoader_ImGui_Font_Kind::Default:
    default:
        font = atlas->AddFontDefault(&config);
        break;
    }

    slots[0] = Own_Font(&call, &config, font);
}

void Add_Font_Memory(wasm_exec_env_t exec_env, u64* slots, bool compressed) {
    Imgui_Call call = Imgui_Enter(exec_env, compressed ? "ImFontAtlas::AddFontFromMemoryCompressedTTF" : "ImFontAtlas::AddFontFromMemoryTTF");
    s32 size = static_cast<s32>(slots[1]);
    const u8* data = size > 0 ? Imgui_Pointer<const u8>(&call, slots[0], static_cast<u64>(size), false) : nullptr;
    ImFontConfig config;
    ImFont* font;

    slots[0] = 0;
    if (size <= 0) {
        Imgui_Fail(&call, "font data must not be empty");
    }

    if (call.failed || !Font_Config(&call, slots[3], slots[4], &config)) {
        return;
    }

    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    if (compressed) {
        font = atlas->AddFontFromMemoryCompressedTTF(data, size, Imgui_F32(slots[2]), &config);
    }
    else {
        void* copy = IM_ALLOC(size);
        if (copy == nullptr) {
            Imgui_Fail(&call, "cannot allocate font data");
            return;
        }

        memcpy(copy, data, size);
        font = atlas->AddFontFromMemoryTTF(copy, size, Imgui_F32(slots[2]), &config);
    }
    slots[0] = Own_Font(&call, &config, font);
}

void Native_Add_Font_Base85(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImFontAtlas::AddFontFromMemoryCompressedBase85TTF");
    const char* text = Imgui_String(&call, slots[0], true);
    ImFontConfig config;

    slots[0] = 0;
    if (call.failed || !Font_Config(&call, slots[2], slots[3], &config)) {
        return;
    }
    slots[0] = Own_Font(&call, &config, ImGui::GetIO().Fonts->AddFontFromMemoryCompressedBase85TTF(text, Imgui_F32(slots[1]), &config));
}

void Native_Remove_Font(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImFontAtlas::RemoveFont");
    for (Module_Font& font : sBindings.fonts) {
        if (!call.failed && font.id == slots[0] && font.owner == call.module) {
            font.owner = nullptr;
            return;
        }
    }
    Imgui_Fail(&call, "the font is not one the module added");
}

void Native_Font_Size(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImFont::LegacySize");
    ImFont* font = Imgui_Font(&call, slots[0]);
    Imgui_Return_F32(slots, font != nullptr ? font->LegacySize : 0.0f);
}

Tracked_Draw_List& Track(ImDrawList* list) {
    auto found = std::ranges::find(sBindings.drawLists, list, &Tracked_Draw_List::list);
    Tracked_Draw_List* tracked = found != sBindings.drawLists.end() ? &*found : &sBindings.drawLists.emplace_back(Tracked_Draw_List{ list, 0, 0, 0, 0 });

    if (tracked->pushFrame != sBindings.frame) {
        tracked->pushFrame = sBindings.frame;
        tracked->clipPushes = 0;
        tracked->texturePushes = 0;
    }

    return *tracked;
}

void Native_Channels_Split(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImDrawList::ChannelsSplit");
    ImDrawList* list = Imgui_Draw_List(&call, slots[0]);

    Imgui_Guard_Range(&call, static_cast<int>(slots[1]), 1, INT_MAX);
    if (!call.failed && list->_Splitter._Count > 1) {
        Imgui_Fail(&call, "the draw list is split already");
    }

    if (!call.failed) {
        list->ChannelsSplit(static_cast<int>(slots[1]));
    }
}

void Native_Channels_Merge(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImDrawList::ChannelsMerge");
    ImDrawList* list = Imgui_Draw_List(&call, slots[0]);

    if (!call.failed) {
        list->ChannelsMerge();
    }
}

void Native_Channels_Set_Current(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImDrawList::ChannelsSetCurrent");
    ImDrawList* list = Imgui_Draw_List(&call, slots[0]);

    if (!call.failed) {
        Imgui_Guard_Range(&call, static_cast<int>(slots[1]), 0, list->_Splitter._Count);
    }

    if (!call.failed) {
        list->ChannelsSetCurrent(static_cast<int>(slots[1]));
    }
}

s32 Find_Clipper(Imgui_Call* call, u64 id) {
    for (u32 index = 0; index < sBindings.clippers.size() && !call->failed; index++) {
        if (sBindings.clippers[index].id == id && sBindings.clippers[index].owner == call->module) {
            return static_cast<s32>(index);
        }
    }

    Imgui_Fail(call, "the list clipper has ended");
    return -1;
}

void Drop_Clipper(u32 index) {
    IM_DELETE(sBindings.clippers[index].clipper);
    sBindings.clippers.erase(sBindings.clippers.begin() + index);
}

void Native_Clipper_Begin(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGuiListClipper::Begin");
    int items_count = static_cast<int>(slots[0]);
    f32 items_height = Imgui_F32(slots[1]);
    ImGuiListClipper* clipper;

    slots[0] = 0;
    if (call.failed) {
        return;
    }

    clipper = IM_NEW(ImGuiListClipper)();
    clipper->Begin(items_count, items_height);
    sBindings.clippers.push_back({ clipper, call.module, ++sBindings.nextClipper });
    slots[0] = sBindings.nextClipper;
}

void Native_Clipper_Step(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGuiListClipper::Step");
    s32 index = Find_Clipper(&call, slots[0]);
    ImGuiListClipper* out = Imgui_Pointer<ImGuiListClipper>(&call, slots[1], 1, false);
    ImGuiListClipper* clipper;
    bool more;

    slots[0] = 0;
    if (call.failed) {
        return;
    }

    clipper = sBindings.clippers[index].clipper;
    more = clipper->Step();
    out->DisplayStart = clipper->DisplayStart;
    out->DisplayEnd = clipper->DisplayEnd;
    out->ItemsHeight = clipper->ItemsHeight;
    out->StartPosY = clipper->StartPosY;
    out->StartSeekOffsetY = clipper->StartSeekOffsetY;

    if (!more) {
        Drop_Clipper(index);
    }

    slots[0] = more ? 1 : 0;
}

void Native_Clipper_End(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGuiListClipper::End");
    s32 index = Find_Clipper(&call, slots[0]);

    if (!call.failed) {
        Drop_Clipper(index);
    }
}

void Native_Clipper_Include(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGuiListClipper::IncludeItemsByIndex");
    s32 index = Find_Clipper(&call, slots[0]);

    if (!call.failed) {
        sBindings.clippers[index].clipper->IncludeItemsByIndex(static_cast<int>(slots[1]), static_cast<int>(slots[2]));
    }
}

void Native_Clipper_Seek(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGuiListClipper::SeekCursorForItem");
    s32 index = Find_Clipper(&call, slots[0]);

    if (!call.failed) {
        sBindings.clippers[index].clipper->SeekCursorForItem(static_cast<int>(slots[1]));
    }
}

void Native_Get_Io(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::GetIO");
    ImGuiIO* out = Imgui_Pointer<ImGuiIO>(&call, slots[0], 1, false);
    const ImGuiIO& io = ImGui::GetIO();

    if (call.failed) {
        return;
    }

    memcpy(static_cast<void*>(out), &io, sizeof(ImGuiIO));
    out->IniFilename = nullptr;
    out->LogFilename = nullptr;
    out->UserData = nullptr;
    out->Fonts = nullptr;
    out->FontDefault = reinterpret_cast<ImFont*>(static_cast<uintptr_t>(Imgui_Font_Handle(io.FontDefault)));
    out->BackendPlatformName = nullptr;
    out->BackendRendererName = nullptr;
    out->BackendPlatformUserData = nullptr;
    out->BackendRendererUserData = nullptr;
    out->BackendLanguageUserData = nullptr;
    out->Ctx = nullptr;
    memset(static_cast<void*>(&out->InputQueueCharacters), 0, sizeof(out->InputQueueCharacters));
}

void Native_Get_Style(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::GetStyle");
    ImGuiStyle* out = Imgui_Pointer<ImGuiStyle>(&call, slots[0], 1, false);

    if (!call.failed) {
        *out = ImGui::GetStyle();
    }
}

void Native_Set_Style(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::GetStyle");
    const u8* before = Imgui_Pointer<const u8>(&call, slots[0], sizeof(ImGuiStyle), false);
    const u8* after = Imgui_Pointer<const u8>(&call, slots[1], sizeof(ImGuiStyle), false);
    u8* style = reinterpret_cast<u8*>(&ImGui::GetStyle());

    for (u32 index = 0; index < sizeof(ImGuiStyle) && !call.failed; index++) {
        if (after[index] != before[index]) {
            style[index] = after[index];
        }
    }
}

void Native_Style_Color(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::GetStyleColorVec4");
    ImVec4* out = Imgui_Pointer<ImVec4>(&call, slots[1], 1, false);

    Imgui_Guard_Range(&call, static_cast<int>(slots[0]), 0, ImGuiCol_COUNT);
    if (!call.failed) {
        *out = ImGui::GetStyleColorVec4(static_cast<ImGuiCol>(slots[0]));
    }
}

void Native_Get_Viewport(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::GetMainViewport");
    ImGuiViewport* out = Imgui_Pointer<ImGuiViewport>(&call, slots[1], 1, false);
    ImGuiViewport* viewport = call.failed ? nullptr : ImGui::FindViewportByID(static_cast<ImGuiID>(slots[0]));

    slots[0] = 0;
    if (viewport == nullptr) {
        return;
    }

    memcpy(static_cast<void*>(out), viewport, sizeof(ImGuiViewport));
    out->ParentViewport = nullptr;
    out->DrawData = nullptr;
    out->RendererUserData = nullptr;
    out->PlatformUserData = nullptr;
    out->PlatformIconData = nullptr;
    out->PlatformHandle = nullptr;
    out->PlatformHandleRaw = nullptr;
    slots[0] = 1;
}

void Native_Check_Layout(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::DebugCheckVersionAndDataLayout");
    const char* version = Imgui_String(&call, slots[0], true);
    u32 count = static_cast<u32>(slots[2]);
    const u64* sizes = Imgui_Pointer<const u64>(&call, slots[1], count, false);
    bool matches = !call.failed && strcmp(version, IMGUI_VERSION) == 0 && count == IM_COUNTOF(gLayout) && memcmp(sizes, gLayout, sizeof(gLayout)) == 0;

    if (!call.failed && !matches) {
        Log_Error(call.module->name.c_str(), "ImGui mismatch: plugin %s, client %s", version, IMGUI_VERSION);
    }

    slots[0] = matches ? 1 : 0;
}

bool Copy_Payload(Imgui_Call* call, const ImGuiPayload* payload, u64 address) {
    ImGuiPayload* out = Imgui_Pointer<ImGuiPayload>(call, address, 1, false);

    if (call->failed || payload == nullptr) {
        return false;
    }

    *out = *payload;
    out->Data = nullptr;
    return true;
}

void Native_Accept_Payload(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::AcceptDragDropPayload");
    const char* type = Imgui_String(&call, slots[0], true);

    Imgui_Guard_Drag_Drop(&call, false);
    slots[0] = !call.failed && Copy_Payload(&call, ImGui::AcceptDragDropPayload(type, static_cast<ImGuiDragDropFlags>(slots[1])), slots[2]) ? 1 : 0;
}

void Native_Get_Payload(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::GetDragDropPayload");
    slots[0] = !call.failed && Copy_Payload(&call, ImGui::GetDragDropPayload(), slots[0]) ? 1 : 0;
}

void Native_Payload_Data(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::GetDragDropPayload");
    const ImGuiPayload* payload = call.failed ? nullptr : ImGui::GetDragDropPayload();
    u64 size = payload != nullptr ? static_cast<u64>(payload->DataSize) : 0;
    void* out = size != 0 && slots[1] == size ? Imgui_Memory(&call, slots[0], size, false) : nullptr;

    if (out != nullptr) {
        memcpy(out, payload->Data, size);
    }
}

void Native_Table_Sort_Specs(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::TableGetSortSpecs");
    ImGuiTableSortSpecs* out = Imgui_Pointer<ImGuiTableSortSpecs>(&call, slots[0], 1, false);
    s32 capacity = static_cast<s32>(slots[2]);
    ImGuiTableColumnSortSpecs* columns = Imgui_Pointer<ImGuiTableColumnSortSpecs>(&call, slots[1], capacity > 0 ? capacity : 0, true);
    ImGuiTableSortSpecs* specs = call.failed ? nullptr : ImGui::TableGetSortSpecs();

    slots[0] = static_cast<u64>(-1);
    if (specs == nullptr) {
        return;
    }

    out->Specs = nullptr;
    out->SpecsCount = specs->SpecsCount;
    out->SpecsDirty = specs->SpecsDirty;
    if (specs->SpecsCount <= capacity) {
        memcpy(columns, specs->Specs, specs->SpecsCount * sizeof(ImGuiTableColumnSortSpecs));
        specs->SpecsDirty = false;
    }

    slots[0] = static_cast<u64>(specs->SpecsCount);
}

Imgui_Call Plain_Call(wasm_exec_env_t exec_env, const char* function) {
    return { &Module_Of(exec_env), wasm_runtime_get_module_inst(exec_env), function, false };
}

void Native_Style_Construct(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Plain_Call(exec_env, "ImGuiStyle::ImGuiStyle");
    ImGuiStyle* style = slots[1] == sizeof(ImGuiStyle) ? Imgui_Pointer<ImGuiStyle>(&call, slots[0], 1, false) : nullptr;

    if (style != nullptr) {
        *style = ImGuiStyle();
    }
}

void Native_Style_Scale(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Plain_Call(exec_env, "ImGuiStyle::ScaleAllSizes");
    ImGuiStyle* style = slots[1] == sizeof(ImGuiStyle) ? Imgui_Pointer<ImGuiStyle>(&call, slots[0], 1, false) : nullptr;

    if (style != nullptr) {
        style->ScaleAllSizes(Imgui_F32(slots[2]));
    }
}

void Native_Font_Config_Construct(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Plain_Call(exec_env, "ImFontConfig::ImFontConfig");
    ImFontConfig* config = slots[1] == sizeof(ImFontConfig) ? Imgui_Pointer<ImFontConfig>(&call, slots[0], 1, false) : nullptr;

    if (config != nullptr) {
        *config = ImFontConfig();
    }
}

struct Rect_Run {
    ImVec2 min;
    ImVec2 max;
    ImU32 color;
};

struct Text_Run {
    ImVec2 position;
    ImU32 color;
    u32 offset;
    u32 length;
};

static_assert(sizeof(Rect_Run) == 20 && sizeof(Text_Run) == 20, "runs are 20 bytes");

void Native_Add_Rects_Filled(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ModLoader::Ui::Add_Rects_Filled");
    ImDrawList* list = Imgui_Draw_List(&call, slots[0]);
    u64 count = slots[2];
    const Rect_Run* rects = count != 0 ? Imgui_Pointer<const Rect_Run>(&call, slots[1], count, false) : nullptr;

    for (u64 index = 0; index < count && !call.failed && rects != nullptr; index++) {
        list->AddRectFilled(rects[index].min, rects[index].max, rects[index].color);
    }
}

void Native_Add_Text_Runs(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ModLoader::Ui::Add_Text_Runs");
    ImDrawList* list = Imgui_Draw_List(&call, slots[0]);
    u64 size = slots[2];
    u64 count = slots[4];
    const char* text = size != 0 ? Imgui_Pointer<const char>(&call, slots[1], size, false) : nullptr;
    const Text_Run* runs = count != 0 ? Imgui_Pointer<const Text_Run>(&call, slots[3], count, false) : nullptr;

    for (u64 index = 0; index < count && !call.failed && runs != nullptr && text != nullptr; index++) {
        const Text_Run& run = runs[index];
        if (run.offset <= size && run.length <= size - run.offset) {
            list->AddText(run.position, run.color, text + run.offset, text + run.offset + run.length);
        }
    }
}

void Native_Dock_Has_Node(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::DockBuilderGetNode");
    slots[0] = !call.failed && ImGui::DockBuilderGetNode(static_cast<ImGuiID>(slots[0])) != nullptr ? 1 : 0;
}

void Native_Dock_Add_Node(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::DockBuilderAddNode");
    slots[0] = !call.failed ? ImGui::DockBuilderAddNode(static_cast<ImGuiID>(slots[0]), static_cast<ImGuiDockNodeFlags>(slots[1])) : 0;
}

void Native_Dock_Remove_Node(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::DockBuilderRemoveNode");

    if (!call.failed) {
        ImGui::DockBuilderRemoveNode(static_cast<ImGuiID>(slots[0]));
    }
}

void Native_Dock_Set_Node_Size(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::DockBuilderSetNodeSize");

    if (!call.failed) {
        ImGui::DockBuilderSetNodeSize(static_cast<ImGuiID>(slots[0]), Imgui_Vec2(slots[1]));
    }
}

void Native_Dock_Split_Node(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::DockBuilderSplitNode");
    ImGuiID* at = Imgui_Pointer<ImGuiID>(&call, slots[3], 1, true);
    ImGuiID* opposite = Imgui_Pointer<ImGuiID>(&call, slots[4], 1, true);

    Imgui_Guard_Range(&call, static_cast<int>(slots[1]), ImGuiDir_Left, ImGuiDir_Down + 1);
    slots[0] = !call.failed ? ImGui::DockBuilderSplitNode(static_cast<ImGuiID>(slots[0]), static_cast<ImGuiDir>(slots[1]), Imgui_F32(slots[2]), at, opposite) : 0;
}

void Native_Dock_Window(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::DockBuilderDockWindow");
    const char* name = Imgui_String(&call, slots[0], false);

    if (!call.failed) {
        ImGui::DockBuilderDockWindow(name, static_cast<ImGuiID>(slots[1]));
    }
}

void Native_Dock_Finish(wasm_exec_env_t exec_env, u64* slots) {
    Imgui_Call call = Imgui_Enter(exec_env, "ImGui::DockBuilderFinish");

    if (!call.failed) {
        ImGui::DockBuilderFinish(static_cast<ImGuiID>(slots[0]));
    }
}

void Log_Error(ImGuiContext*, void* user_data, const char* message) {
    Log_Warning(static_cast<const Module*>(user_data)->name.c_str(), "ImGui: %s", message);
}

NativeSymbol sImguiNatives[] = {
    Native("ImGui_GetIO", "(I)", Native_Get_Io),
    Native("ImGui_DockBuilderHasNode", "(i)i", Native_Dock_Has_Node),
    Native("ImGui_DockBuilderAddNode", "(ii)i", Native_Dock_Add_Node),
    Native("ImGui_DockBuilderRemoveNode", "(i)", Native_Dock_Remove_Node),
    Native("ImGui_DockBuilderSetNodeSize", "(iI)", Native_Dock_Set_Node_Size),
    Native("ImGui_DockBuilderSplitNode", "(iifII)i", Native_Dock_Split_Node),
    Native("ImGui_DockBuilderDockWindow", "(Ii)", Native_Dock_Window),
    Native("ImGui_DockBuilderFinish", "(i)", Native_Dock_Finish),
    Native("ImDrawList_AddRectsFilled", "(III)", Native_Add_Rects_Filled),
    Native("ImDrawList_AddTextRuns", "(IIIII)", Native_Add_Text_Runs),
    Native("ImGui_GetStyle", "(I)", Native_Get_Style),
    Native("ImGui_SetStyle", "(II)", Native_Set_Style),
    Native("ImGui_GetStyleColorVec4", "(iI)", Native_Style_Color),
    Native("ImGui_GetViewport", "(iI)i", Native_Get_Viewport),
    Native("ImGui_CheckLayout", "(IIi)i", Native_Check_Layout),
    Native("ImGui_AcceptDragDropPayload", "(IiI)i", Native_Accept_Payload),
    Native("ImGui_GetDragDropPayload", "(I)i", Native_Get_Payload),
    Native("ImGui_PayloadData", "(II)", Native_Payload_Data),
    Native("ImGui_TableGetSortSpecs", "(IIi)i", Native_Table_Sort_Specs),
    Native("ImFontAtlas_AddFontDefault", "(iII)I", Native_Add_Font_Default),
    Native("ImFontAtlas_AddFontFromMemoryTTF", "(IifII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        Add_Font_Memory(exec_env, slots, false);
    }),
    Native("ImFontAtlas_AddFontFromMemoryCompressedTTF", "(IifII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        Add_Font_Memory(exec_env, slots, true);
    }),
    Native("ImFontAtlas_AddFontFromMemoryCompressedBase85TTF", "(IfII)I", Native_Add_Font_Base85),
    Native("ImFontAtlas_RemoveFont", "(I)", Native_Remove_Font),
    Native("ImFont_LegacySize", "(I)f", Native_Font_Size),
    Native("ImDrawList_ChannelsSplit", "(Ii)", Native_Channels_Split),
    Native("ImDrawList_ChannelsMerge", "(I)", Native_Channels_Merge),
    Native("ImDrawList_ChannelsSetCurrent", "(Ii)", Native_Channels_Set_Current),
    Native("ImGuiListClipper_Begin", "(if)I", Native_Clipper_Begin),
    Native("ImGuiListClipper_Step", "(II)i", Native_Clipper_Step),
    Native("ImGuiListClipper_End", "(I)", Native_Clipper_End),
    Native("ImGuiListClipper_IncludeItemsByIndex", "(Iii)", Native_Clipper_Include),
    Native("ImGuiListClipper_SeekCursorForItem", "(Ii)", Native_Clipper_Seek),
    Native("ImGuiStyle_Construct", "(II)", Native_Style_Construct),
    Native("ImGuiStyle_ScaleAllSizes", "(IIf)", Native_Style_Scale),
    Native("ImFontConfig_Construct", "(II)", Native_Font_Config_Construct),
};

NativeSymbol sTextureNatives[] = {
    Native("ui_texture_create", "(iiI)I", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Texture_Create(Module_Of(exec_env), exec_env, static_cast<u32>(slots[0]), static_cast<u32>(slots[1]), slots[2]);
    }),
    Native("ui_texture_update", "(II)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Texture_Update(Module_Of(exec_env), exec_env, slots[0], slots[1]) ? 1 : 0;
    }),
    Native("ui_texture_destroy", "(I)", [](wasm_exec_env_t exec_env, u64* slots) {
        std::lock_guard lock(sBindings.lock);
        Module_Texture* texture = Find_Texture(&Module_Of(exec_env), slots[0]);

        if (texture != nullptr) {
            texture->owner = nullptr;
            texture->pixels = {};
        }
    }),
};

}

void Imgui_Register_Natives() {
    Imgui_Register_Generated_Natives();
    Register_Natives(sImguiNatives, "ImGui", "imgui");
    Register_Natives(sTextureNatives, "UI's texture");
}

void Imgui_Frame_Begin() {
    sBindings.frame++;
    Update_Textures();
    Update_Fonts();
}

void Imgui_Enter_Module(const Module& module) {
    sBindings.current = &module;
    ImGui::ErrorRecoveryStoreState(&sBindings.recovery);
    GImGui->ErrorCallback = Log_Error;
    GImGui->ErrorCallbackUserData = const_cast<Module*>(&module);
}

void Imgui_Leave_Module(const Module& module) {
    for (usize index = sBindings.clippers.size(); index > 0; index--) {
        if (sBindings.clippers[index - 1].owner == &module) {
            Drop_Clipper(static_cast<u32>(index - 1));
        }
    }

    ImGui::ErrorRecoveryTryToRecoverState(&sBindings.recovery);
    GImGui->ErrorCallback = nullptr;
    GImGui->ErrorCallbackUserData = nullptr;
    sBindings.current = nullptr;
}

void Imgui_Release_Owner(const Module& owner) {
    {
        std::lock_guard lock(sBindings.lock);
        sBindings.textures.For_Each([&](u32, Module_Texture& texture) {
            if (texture.owner == &owner) {
                texture.owner = nullptr;
                texture.pixels = {};
            }
        });
    }

    for (Module_Font& font : sBindings.fonts) {
        font.owner = font.owner == &owner ? nullptr : font.owner;
    }
}

void Imgui_Shutdown() {
    sBindings.textures.For_Each([](u32, Module_Texture& texture) {
        if (texture.data != nullptr) {
            sBindings.dying.push_back(texture.data);
        }
    });

    for (ImTextureData* data : sBindings.dying) {
        ImGui::UnregisterUserTexture(data);
        IM_DELETE(data);
    }

    sBindings.textures.Clear();
    sBindings.dying.clear();
    sBindings.fonts.clear();
    sBindings.drawLists.clear();
    sBindings.clippers.clear();
    sBindings.strings.clear();
}

Imgui_Call Imgui_Enter(wasm_exec_env_t exec_env, const char* function) {
    Imgui_Call call = Plain_Call(exec_env, function);
    if (!In_Module_Ui(*call.module, exec_env)) {
        Imgui_Fail(&call, "ImGui's functions work in On_Ui only");
    }
    return call;
}

void Imgui_Fail(Imgui_Call* call, const char* reason) {
    char message[256];

    if (call->failed) {
        return;
    }

    snprintf(message, sizeof(message), "%s: %s", call->function, reason);
    wasm_runtime_set_exception(call->instance, message);
    call->failed = true;
}

void* Imgui_Memory(Imgui_Call* call, u64 address, u64 size, bool nullable) {
    if (call->failed) {
        return nullptr;
    }

    if (address == 0) {
        if (!nullable) {
            Imgui_Fail(call, "a pointer argument is null");
        }
        return nullptr;
    }

    if (!wasm_runtime_validate_app_addr(call->instance, address, size)) {
        Imgui_Fail(call, "a pointer argument leaves the module's memory");
        return nullptr;
    }

    return wasm_runtime_addr_app_to_native(call->instance, address);
}

const char* Imgui_String(Imgui_Call* call, u64 address, bool required) {
    if (call->failed) {
        return nullptr;
    }

    if (address == 0) {
        if (required) {
            Imgui_Fail(call, "a string argument is null");
        }
        return nullptr;
    }

    if (!wasm_runtime_validate_app_str_addr(call->instance, address)) {
        Imgui_Fail(call, "a string argument leaves the module's memory");
        return nullptr;
    }

    return static_cast<const char*>(wasm_runtime_addr_app_to_native(call->instance, address));
}

const char* Imgui_Text(Imgui_Call* call, u64 begin, u64 end) {
    if (end == 0) {
        return Imgui_String(call, begin, true);
    }

    if (end < begin) {
        Imgui_Fail(call, "a text ends before it begins");
        return nullptr;
    }

    return static_cast<const char*>(Imgui_Memory(call, begin, end - begin, false));
}

const char* Imgui_Text_End(const char* text, u64 begin, u64 end) {
    return text != nullptr && end != 0 ? text + (end - begin) : nullptr;
}

const char* Imgui_Zero_Separated(Imgui_Call* call, u64 address) {
    const char* items = Imgui_String(call, address, true);
    const char* item = items;

    while (item != nullptr && *item != '\0') {
        address += strlen(item) + 1;
        item = Imgui_String(call, address, true);
    }

    return call->failed ? nullptr : items;
}

const char* const* Imgui_Strings(Imgui_Call* call, u64 address, int count) {
    const u64* items;

    static const char* sNone[1];
    items = Imgui_Pointer<const u64>(call, address, count > 0 ? count : 0, count == 0);

    if (call->failed || count == 0) {
        return call->failed ? nullptr : sNone;
    }

    if (count < 0) {
        Imgui_Fail(call, "too many strings");
        return nullptr;
    }

    sBindings.strings.resize(count);
    for (int index = 0; index < count; index++) {
        sBindings.strings[index] = Imgui_String(call, items[index], true);
    }

    return call->failed ? nullptr : sBindings.strings.data();
}

void* Imgui_Data(Imgui_Call* call, u64 address, ImGuiDataType type, int components, bool nullable) {
    if (!call->failed && (type < 0 || type >= ImGuiDataType_COUNT || components < 0)) {
        Imgui_Fail(call, "a data type or component count is out of range");
    }

    if (call->failed) {
        return nullptr;
    }

    return Imgui_Memory(call, address, ImGui::DataTypeGetInfo(type)->Size * static_cast<u64>(components), nullable);
}

const float* Imgui_Values(Imgui_Call* call, u64 address, int count, int stride) {
    if (!call->failed && (count < 0 || stride < static_cast<int>(sizeof(float)))) {
        Imgui_Fail(call, "a value count or stride is out of range");
    }

    if (call->failed) {
        return nullptr;
    }

    return static_cast<const float*>(Imgui_Memory(call, address, count == 0 ? 0 : (count - 1) * static_cast<u64>(stride) + sizeof(float), count == 0));
}

ImTextureRef Imgui_Texture(Imgui_Call* call, u64 handle) {
    ImTextureRef reference;
    Module_Texture* texture;

    if (call->failed) {
        return reference;
    }

    {
        std::lock_guard lock(sBindings.lock);
        texture = Find_Texture(call->module, handle);
        if (texture != nullptr && texture->data != nullptr) {
            reference = texture->data->GetTexRef();
        }
    }

    if (reference._TexData == nullptr) {
        Imgui_Fail(call, "the texture is not one of the module's");
    }
    return reference;
}

ImFont* Imgui_Font(Imgui_Call* call, u64 handle) {
    ImFont* font;

    if (call->failed || handle == 0) {
        return nullptr;
    }

    font = Font_Of(static_cast<ImGuiID>(handle));
    if (font == nullptr || handle > 0xFFFFFFFFull) {
        Imgui_Fail(call, "the font is not ImGui's");
        return nullptr;
    }

    return font;
}

u64 Imgui_Font_Handle(const ImFont* font) {
    return font != nullptr ? font->FontId : 0;
}

ImDrawList* Imgui_Draw_List(Imgui_Call* call, u64 handle) {
    u64 index = handle - 1;

    if (call->failed) {
        return nullptr;
    }

    if (index >= sBindings.drawLists.size() || sBindings.drawLists[index].handedFrame != sBindings.frame) {
        Imgui_Fail(call, "the draw list is not one ImGui handed out this frame");
        return nullptr;
    }

    return sBindings.drawLists[index].list;
}

u64 Imgui_Draw_List_Handle(ImDrawList* list) {
    if (list == nullptr) {
        return 0;
    }

    Tracked_Draw_List& tracked = Track(list);
    tracked.handedFrame = sBindings.frame;
    return static_cast<u64>(&tracked - sBindings.drawLists.data() + 1);
}

ImGuiViewport* Imgui_Viewport(Imgui_Call* call, u64 id) {
    return call->failed || id == 0 ? nullptr : ImGui::FindViewportByID(static_cast<ImGuiID>(id));
}

u64 Imgui_Viewport_Id(const ImGuiViewport* viewport) {
    return viewport != nullptr ? viewport->ID : 0;
}

u64 Imgui_String_Result(Imgui_Call* call, const char* text, u64 buffer, u64 capacity) {
    u64 length = text != nullptr ? strlen(text) : 0;
    char* out = capacity != 0 ? static_cast<char*>(Imgui_Memory(call, buffer, capacity, false)) : nullptr;
    u64 kept = length < capacity ? length : capacity - 1;

    if (out == nullptr) {
        return length;
    }

    memcpy(out, text != nullptr ? text : "", kept);
    out[kept] = '\0';
    return length;
}

bool Imgui_Text_Link_Open_Url(const char* label, const char* url) {
    const char* target = url != nullptr ? url : label;

    if (strncmp(target, "https://", 8) == 0 || strncmp(target, "http://", 7) == 0) {
        return ImGui::TextLinkOpenURL(label, url);
    }

    return ImGui::TextLink(label);
}

f32 Imgui_Finite(f32 value) {
    if (std::isnan(value)) {
        return 0.0f;
    }

    return std::clamp(value, -FLT_MAX, FLT_MAX);
}

void Imgui_Push_Clip_Rect(ImDrawList* list, ImVec2 low, ImVec2 high, bool intersect) {
    Tracked_Draw_List& tracked = Track(list != nullptr ? list : ImGui::GetWindowDrawList());

    if (list != nullptr) {
        list->PushClipRect(low, high, intersect);
    }
    else {
        ImGui::PushClipRect(low, high, intersect);
    }

    tracked.clipPushes++;
}

void Imgui_Push_Clip_Rect_Full_Screen(ImDrawList* list) {
    Tracked_Draw_List& tracked = Track(list);

    list->PushClipRectFullScreen();
    tracked.clipPushes++;
}

void Imgui_Pop_Clip_Rect(Imgui_Call* call, ImDrawList* list) {
    Tracked_Draw_List& tracked = Track(list != nullptr ? list : ImGui::GetWindowDrawList());

    if (tracked.clipPushes == 0) {
        Imgui_Fail(call, "pops a clip rectangle the module did not push");
        return;
    }

    tracked.clipPushes--;
    if (list != nullptr) {
        list->PopClipRect();
    }
    else {
        ImGui::PopClipRect();
    }
}

void Imgui_Push_Texture(ImDrawList* list, ImTextureRef texture) {
    Tracked_Draw_List& tracked = Track(list);

    list->PushTexture(texture);
    tracked.texturePushes++;
}

void Imgui_Pop_Texture(Imgui_Call* call, ImDrawList* list) {
    Tracked_Draw_List& tracked = Track(list);

    if (tracked.texturePushes == 0) {
        Imgui_Fail(call, "pops a texture the module did not push");
        return;
    }

    tracked.texturePushes--;
    list->PopTexture();
}

void Imgui_Guard_Range(Imgui_Call* call, int value, int low, int high) {
    if (value < low || value >= high) {
        Imgui_Fail(call, "an argument is out of range");
    }
}

void Imgui_Guard_Key(Imgui_Call* call, ImGuiKey key) {
    if (!ImGui::IsNamedKeyOrMod(key)) {
        Imgui_Fail(call, "the key is not an ImGuiKey");
    }
}

void Imgui_Guard_Key_Chord(Imgui_Call* call, ImGuiKeyChord chord) {
    ImGuiKey key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
    int mods = chord & ImGuiMod_Mask_;

    if (key != ImGuiKey_None ? !ImGui::IsNamedKey(key) : mods != ImGuiMod_Ctrl && mods != ImGuiMod_Shift && mods != ImGuiMod_Alt && mods != ImGuiMod_Super) {
        Imgui_Fail(call, "the key chord holds no key");
    }
}

void Imgui_Guard_Window(Imgui_Call* call, ImGuiWindowFlags flags) {
    ImGuiContext& g = *GImGui;

    if (!call->failed && (g.CurrentWindowStack.Size <= sBindings.recovery.SizeOfWindowStack || (g.CurrentWindow->Flags & flags) != flags)) {
        Imgui_Fail(call, "ends a window the module did not begin");
    }
}

void Imgui_Guard_Menu_Bar(Imgui_Call* call) {
    ImGuiWindow* window = GImGui->CurrentWindow;

    if (!call->failed && !window->SkipItems && (!(window->Flags & ImGuiWindowFlags_MenuBar) || !window->DC.MenuBarAppending)) {
        Imgui_Fail(call, "ends a menu bar that did not begin");
    }
}

void Imgui_Guard_Group(Imgui_Call* call) {
    if (!call->failed && GImGui->GroupStack.Size <= sBindings.recovery.SizeOfGroupStack) {
        Imgui_Fail(call, "ends a group the module did not begin");
    }
}

void Imgui_Guard_Tree(Imgui_Call* call) {
    if (!call->failed && GImGui->CurrentWindow->DC.TreeDepth <= 0) {
        Imgui_Fail(call, "pops a tree node that was not pushed");
    }
}

void Imgui_Guard_Tab_Item(Imgui_Call* call) {
    ImGuiTabBar* tab_bar = GImGui->CurrentTabBar;

    if (!call->failed && !GImGui->CurrentWindow->SkipItems && (tab_bar == nullptr || tab_bar->LastTabItemIdx < 0)) {
        Imgui_Fail(call, "ends a tab item that did not begin");
    }
}

void Imgui_Guard_Drag_Drop(Imgui_Call* call, bool source) {
    if (!call->failed && !(source ? GImGui->DragDropWithinSource : GImGui->DragDropWithinTarget)) {
        Imgui_Fail(call, source ? "is not inside a drag and drop source" : "is not inside a drag and drop target");
    }
}

void Imgui_Guard_Table(Imgui_Call* call) {
    if (!call->failed && GImGui->CurrentTable == nullptr) {
        Imgui_Fail(call, "is not inside a table");
    }
}

void Imgui_Guard_Table_Column(Imgui_Call* call, int column, bool need_table) {
    ImGuiTable* table = GImGui->CurrentTable;

    if (call->failed || (table == nullptr && !need_table)) {
        return;
    }

    if (table == nullptr) {
        Imgui_Fail(call, "is not inside a table");
        return;
    }

    column = column < 0 ? table->CurrentColumn : column;
    if (column < 0 || column >= table->ColumnsCount) {
        Imgui_Fail(call, "the table has no such column");
    }
}

void Imgui_Guard_Table_Set_Column(Imgui_Call* call, int column) {
    ImGuiTable* table = GImGui->CurrentTable;

    if (!call->failed && table != nullptr && (column < 0 || column >= table->ColumnsCount)) {
        Imgui_Fail(call, "the table has no such column");
    }
}

void Imgui_Guard_Table_Background(Imgui_Call* call, ImGuiTableBgTarget target, int column) {
    if (target == ImGuiTableBgTarget_CellBg) {
        Imgui_Guard_Table_Column(call, column, true);
    }
    else {
        Imgui_Guard_Table(call);
    }
}

void Imgui_Guard_Table_Freeze(Imgui_Call* call, int columns, int rows) {
    if (columns < 0 || columns >= IMGUI_TABLE_MAX_COLUMNS || rows < 0 || rows >= 128) {
        Imgui_Fail(call, "freezes more columns or rows than a table has");
    }
}

void Imgui_Guard_Column(Imgui_Call* call, int column, bool need_columns) {
    ImGuiOldColumns* columns = GImGui->CurrentWindow->DC.CurrentColumns;

    if (call->failed || (columns == nullptr && !need_columns)) {
        return;
    }

    if (columns == nullptr) {
        Imgui_Fail(call, "is not inside columns");
        return;
    }
    
    column = column < 0 ? columns->Current : column;
    if (column < 0 || column >= columns->Count) {
        Imgui_Fail(call, "there is no such column");
    }
}
