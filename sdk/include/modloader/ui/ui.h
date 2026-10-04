#pragma once

#include <modloader/types.h>
#include <modloader/subscription.h>

#include <imgui.h>
#include <span>
#include <string_view>

namespace ModLoader::Ui {

using Handle = u32;

// FreeType flags for monochrome glyphs
constexpr u32 gFontMonochrome = (1u << 7) | (1u << 4);

ImTextureID Texture_Create(u32 width, u32 height, const void* pixels);
bool Texture_Update(ImTextureID texture, const void* pixels);
void Texture_Destroy(ImTextureID texture);

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

void Add_Rects_Filled(ImDrawList* list, std::span<const Rect_Run> rects);
void Add_Text_Runs(ImDrawList* list, std::string_view text, std::span<const Text_Run> runs);

class Window {
public:
    Window() = default;
    explicit Window(Ui::Handle handle) : handle(handle) {
    }

    static Window Create(std::string_view title, u32 width, u32 height);
    static Window Overlay();

    void Destroy();
    void Show(bool shown) const;
    bool Shown() const;

    Ui::Handle Handle() const {
        return handle;
    }

    bool Is_Valid() const {
        return handle != 0;
    }

private:
    Ui::Handle handle = 0;
};

// Runs after On_Ui
[[nodiscard]] Subscription Subscribe(Function<void()> frame);

} // namespace ModLoader::Ui
