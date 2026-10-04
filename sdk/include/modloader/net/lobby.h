#pragma once

#include <modloader/types.h>

#include <modloader_lobby.h>
#include <string>
#include <vector>

namespace ModLoader::Lobby {

struct Client_Id {
    u8 bytes[16];

    bool Same(const Client_Id& other) const {
        return __builtin_memcmp(bytes, other.bytes, sizeof(bytes)) == 0;
    }

    std::string Text() const;
};

struct Member {
    Client_Id id;
    char nickname[33];
};

enum class State : u32 {
    Disconnected = MODLOADER_LOBBY_STATUS_DISCONNECTED,
    Connecting = MODLOADER_LOBBY_STATUS_CONNECTING,
    Joined = MODLOADER_LOBBY_STATUS_JOINED,
    Failed = MODLOADER_LOBBY_STATUS_FAILED,
};

State Get_State();
Member Self(); // Local identity and nickname; zero initialized when no connection exists
std::vector<Member> Members(); // Current remote members; excludes Self().

} // namespace ModLoader::Lobby
