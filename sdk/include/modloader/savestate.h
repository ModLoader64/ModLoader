#pragma once

#include <modloader/detail/identity.h>
#include <modloader/subscription.h>

#include <string_view>

namespace ModLoader::Savestate {

namespace Detail {
Subscription Block(std::string_view owner, std::string_view reason);
} // namespace Detail

// block saving/loading while the token lives
[[nodiscard]] static inline Subscription Block(std::string_view reason) {
    return Detail::Block(ModLoader::Detail::gModuleName, reason);
}

} // namespace ModLoader::Savestate
