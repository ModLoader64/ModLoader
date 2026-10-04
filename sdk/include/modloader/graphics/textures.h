#pragma once

#include <modloader/subscription.h>
#include <modloader/types.h>
#include <modloader_texture.h>

#include <string_view>

namespace ModLoader::Textures {

enum class State : u32 {
    Disabled = MODLOADER_TEXTURE_DISABLED,
    Pending = MODLOADER_TEXTURE_PENDING,
    Ready = MODLOADER_TEXTURE_READY,
    Unsupported = MODLOADER_TEXTURE_UNSUPPORTED,
    Error = MODLOADER_TEXTURE_ERROR,
};

// Owns one replacement source; destruction unregisters it
// Replacement format and support depend on the renderer
class Source {
public:
    Source() = default;
    ~Source();
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;
    Source(Source&& other) noexcept;
    Source& operator=(Source&& other) noexcept;

    // Stable ids retain the user's priority and enabled setting across launches
    // Paths use assets:/ or the module's writable folder, as File::Open does
    bool Create(std::string_view id, std::string_view name, std::string_view path, bool enabled = true);
    bool Enable();
    bool Disable();
    bool Reload(); // Re-reads the source after its files change
    void Destroy();
    Textures::State State() const;
    bool Is_Enabled() const;

    bool Is_Created() const {
        return handle != 0;
    }

    u32 Handle() const {
        return handle;
    }

private:
    bool Create_In(std::string_view folder, std::string_view id, std::string_view name, std::string_view path, bool enabled);
    u32 handle = 0;
};

struct Source_Change {
    u32 source;
    Textures::State state;
    bool enabled;
    u32 priority; // Position in the client's replacement order; lower values take precedence
};

struct Changed_Event {
    // Match Source_Change::source against Source::Handle() to observe a particular source
    [[nodiscard]] Subscription Subscribe(Function<void(const Source_Change&)> callback) const;
};

inline constexpr Changed_Event Event_Changed;

} // namespace ModLoader::Textures
