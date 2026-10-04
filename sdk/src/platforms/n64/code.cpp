#include "../../internal.h"

#include <modloader/platforms/n64/code.h>

using namespace ModLoader;

Guest::uptr ModLoader::N64::Signature::Scan(std::string_view pattern, Space space, Guest::uptr from, Guest::uptr to) {
    return static_cast<Guest::uptr>(ModLoader_Host_Sig_Scan(static_cast<u32>(space), pattern.data(), pattern.size(), from, to));
}

ModLoader::N64::Signature::Signature(std::string_view text) : pattern(text) {
}

Guest::uptr ModLoader::N64::Signature::Find(Space space, Guest::uptr from, Guest::uptr to) const {
    return Scan(pattern, space, from, to);
}

Guest::uptr ModLoader::N64::Signature::Find_Unique(Space space, Guest::uptr from, Guest::uptr to) const {
    return static_cast<Guest::uptr>(ModLoader_Host_Sig_Scan_Unique(static_cast<u32>(space), pattern.data(), pattern.size(), from, to));
}
