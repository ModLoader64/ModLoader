#pragma once

#include <modloader/net/lobby.h>
#include <modloader/types.h>

#include <array>
#include <string_view>

namespace ModLoader::Server {

bool Hosting(); // This session was started with server support

struct Lobby {
    using Handle = u32;

    Handle id;
    std::string_view name;
};

struct Member {
    Lobby lobby;
    ModLoader::Lobby::Member member;
    std::array<u8, 32> publicKey; // public identity
};

} // namespace ModLoader::Server
