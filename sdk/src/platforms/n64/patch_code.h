#pragma once

#include <modloader/platforms/n64/code.h>

#include <span>
#include <string>
#include <vector>

namespace ModLoader::N64::Detail {

constexpr Guest::uptr Physical_Address(Guest::uptr address) {
    return address & 0x1FFFFFFFu;
}

bool Ram_Range(Guest::uptr address, Guest::usize size);
bool Overlaps(Guest::uptr left, Guest::usize left_size, Guest::uptr right, Guest::usize right_size);
bool Read_Code(Guest::uptr address, std::span<u8> bytes);
bool Write_Code(Guest::uptr address, std::span<const u8> bytes);
Guest::usize Preserved_Size(std::span<const u8> bytes, Guest::uptr address);

enum class Patch_Mode : u32 {
    Inline,
    Detour,
};

class Patch_Code {
public:
    struct Relocation {
        Guest::uptr address = 0;
        Guest::usize capacity = 0;
        std::vector<u8> patched;
        std::vector<u8> destination;
        Guest::usize payloadSize = 0;
    };

    Patch_Code() = default;
    Patch_Code(const Patch_Code&) = delete;
    Patch_Code& operator=(const Patch_Code&) = delete;

    bool Create_Destination(Guest::uptr source, Guest::uptr destination);
    bool Create_Hypercall(Guest::uptr source, Hypercall::Handle id, Patch_Mode mode, Patch::Preserve preserve, std::span<const u8> canonical = {});
    bool Create_Assembly(Guest::uptr source, std::string_view assembly, Patch_Mode mode, Patch::Preserve preserve, std::span<const u8> canonical = {});
    bool Create_Bytes(Guest::uptr source, std::span<const u8> bytes);
    bool Prepare_Relocation(Guest::uptr destination, Guest::usize capacity, Relocation& relocation) const;
    void Commit_Relocation(Relocation&& relocation) noexcept;
    bool Relocate(Guest::uptr destination, Guest::usize capacity);
    bool Enable();
    bool Disable();
    bool Destroy();

    bool Is_Created() const {
        return created;
    }

    bool Is_Enabled() const {
        return enabled;
    }

    Guest::uptr Address() const {
        return address;
    }

    Guest::uptr Destination() const {
        return direct ? destination : trampoline;
    }

    Guest::uptr Trampoline() const {
        return trampoline;
    }

    Guest::usize Capacity() const {
        return destinationCapacity;
    }

    Guest::usize Payload_Size() const {
        return payloadSize;
    }

    Patch::Preserve Preservation() const {
        return preserve;
    }

    Guest::usize Affected_Size() const {
        return static_cast<Guest::usize>(original.size());
    }
    
    std::span<const u8> Original_Bytes() const {
        return original;
    }

    std::span<const u8> Patched_Bytes() const {
        return patched;
    }

    std::span<const u8> Destination_Bytes() const {
        return destinationBytes;
    }

private:
    bool Begin(Guest::uptr source, Patch_Mode mode, Patch::Preserve preserve, std::span<const u8> payload, bool direct = false, Guest::uptr destination = 0, std::span<const u8> canonical = {});
    bool Read_Context(std::vector<u8>& source) const;
    bool Prepare(std::span<const u8> source, Guest::uptr cave, Guest::usize capacity, Relocation& prepared) const;
    Guest::usize Installed_Size(std::span<const u8> bytes) const;
    bool Write_Changed(Guest::uptr destination, std::span<const u8> bytes) const;
    void Clear();

    Patch_Mode mode = Patch_Mode::Inline;
    Patch::Preserve preserve = Patch::Preserve::None;
    Guest::uptr address = 0;
    Guest::uptr destination = 0;
    Guest::uptr trampoline = 0;
    Guest::usize patchSize = 0;
    Guest::usize payloadSize = 0;
    Guest::usize destinationCapacity = 0;
    Guest::usize boundCapacity = 0;
    std::vector<u8> original;
    std::vector<u8> payload;
    std::vector<u8> patched;
    std::vector<u8> destinationBytes;
    std::string assembly;
    bool direct = false;
    bool created = false;
    bool enabled = false;
};

} // namespace ModLoader::N64::Detail
