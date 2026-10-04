#pragma once

#include <modloader/platforms/n64/cpu.h>
#include <modloader/platforms/n64/assembler.h>
#include <modloader/guest/heap.h>
#include <modloader/guest/patch.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ModLoader::N64 {

namespace Detail {
struct Patch_State;
} // namespace Detail

// Instruction aligned search
// Hex nibbles match literally; '?' and '.' match any nibble, ex: "27 BD ?? ??"
class Signature {
public:
    enum class Space : u32 {
        Rdram = 0,
        Rom = 1,
    };

    Signature() = default;
    explicit Signature(std::string_view pattern);

    // Search [from, to); zero bounds select the space ends; returns 0 on failure
    Guest::uptr Find(Space space = Space::Rdram, Guest::uptr from = 0, Guest::uptr to = 0) const;
    // Returns 0 when more than one match exists
    Guest::uptr Find_Unique(Space space = Space::Rdram, Guest::uptr from = 0, Guest::uptr to = 0) const;
    static Guest::uptr Scan(std::string_view pattern, Space space = Space::Rdram, Guest::uptr from = 0, Guest::uptr to = 0);

private:
    std::string pattern;
};

// Reversible code patch
// Owns original/replacement bytes and callbacks
class Patch : public Guest::Patch {
public:
    enum class Preserve : u32 {
        None, // Skips both source instructions for the whole chain; resumes at source + 8.
        Source, // Replays displaced execution once, unless any enabled detour requests None.
    };

    struct Storage {
        Guest::uptr address;
        Guest::usize capacity;
    };

    using Callback = Hypercall::Callback;

    constexpr Patch() noexcept = default;
    ~Patch() override;
    Patch(const Patch&) = delete;
    Patch& operator=(const Patch&) = delete;
    Patch(Patch&& other) noexcept;
    Patch& operator=(Patch&& other) noexcept;

    // Creation leaves the patch disabled; call Enable to install it
    // Inline replaces source execution
    bool Create_Inline(Guest::uptr source, std::string_view assembly);
    bool Create_Inline(Guest::uptr source, std::span<const u8> bytes);
    bool Create_Inline(Guest::uptr source, Callback callback);

    bool Create_Redirect(Guest::uptr source, Guest::uptr destination);
    // Preserved leading J/JAL instructions execute their link/delay slot before the payloads
    // Allocates generated code in the heap
    bool Create_Detour(Guest::uptr source, Guest::Heap& heap, Preserve preserve, std::string_view assembly);
    bool Create_Detour(Guest::uptr source, Guest::Heap& heap, Preserve preserve, Callback callback);
    bool Create_Detour(Guest::uptr source, Storage storage, Preserve preserve, std::string_view assembly);
    bool Create_Detour(Guest::uptr source, Storage storage, Preserve preserve, Callback callback);
    bool Enable() override; // Installs code and pins heap storage
    bool Disable() override; // Restores/unlinks the source; storage stays pinned until Allow_Relocation
    bool Allow_Relocation();
    bool Destroy() override;
    bool Is_Created() const override;
    bool Is_Enabled() const override;
    Guest::uptr Address() const; // Source address, or 0 before creation
    Guest::uptr Destination() const; // Entry address, may change after heap relocation
    Guest::uptr Trampoline() const; // Generated entry address; 0 for inline/direct-destination patches
    // Owned by the patch
    Guest::Heap::Handle Allocation() const;
    std::vector<u8> Original_Bytes() const;
    std::vector<u8> Patched_Bytes() const;
    std::vector<u8> Destination_Bytes() const;

private:
    bool Finish_Create(Detail::Patch_State* created, Guest::Heap* heap = nullptr, Storage storage = {});
    bool Create_Callback(Guest::uptr source, Guest::Heap* heap, Storage storage, Preserve preserve, Callback callback);
    bool Create_Assembly(Guest::uptr source, Guest::Heap* heap, Storage storage, Preserve preserve, std::string_view assembly);

    Detail::Patch_State* state = nullptr;
};

} // namespace ModLoader::N64
