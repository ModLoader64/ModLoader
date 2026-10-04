#pragma once

#include "patch_code.h"

namespace ModLoader::N64::Detail {

class Patch_Chain {
public:
    static constexpr Guest::usize Header_Size = 64;

    ~Patch_Chain();
    static bool Capture(Guest::uptr source, Guest::usize proposed_span, std::vector<u8>& canonical);
    bool Bind(Patch_Code& code, Guest::uptr storage, Guest::usize capacity);
    bool Prepare_Relocation(Guest::uptr storage, Guest::usize capacity);
    bool Commit_Relocation();
    void Cancel_Relocation();
    bool Enable();
    bool Disable();
    bool Destroy();
    bool Is_Enabled() const;
    std::vector<u8> Destination_Bytes() const;

private:
    struct Relocation;

    Patch_Code* code = nullptr;
    Relocation* relocation = nullptr;
    Guest::uptr storage = 0;
    Guest::usize capacity = 0;
    bool enabled = false;
};

} // namespace ModLoader::N64::Detail
