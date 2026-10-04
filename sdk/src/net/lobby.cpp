#include "socket.h"
#include "../internal.h"

#include <modloader_bytes.h>

void ModLoader::Net::Detail::Copy_Member(const ModLoader_Lobby_Member& record, Lobby::Member* out_member) {
    __builtin_memcpy(out_member->id.bytes, record.id, sizeof(out_member->id.bytes));
    __builtin_memcpy(out_member->nickname, record.nickname, sizeof(out_member->nickname));
    out_member->nickname[sizeof(out_member->nickname) - 1] = '\0';
}

ModLoader::Lobby::State ModLoader::Lobby::Get_State() {
    ModLoader_Lobby_Status status = {};
    ModLoader_Host_Lobby_Status(&status, sizeof(status));
    return static_cast<State>(status.state);
}

ModLoader::Lobby::Member ModLoader::Lobby::Self() {
    ModLoader_Lobby_Status status = {};
    Member member = {};
    ModLoader_Host_Lobby_Status(&status, sizeof(status));
    Net::Detail::Copy_Member(status.self, &member);
    return member;
}

std::vector<ModLoader::Lobby::Member> ModLoader::Lobby::Members() {
    ModLoader_Lobby_Status status = {};
    std::vector<Member> members;
    ModLoader_Host_Lobby_Status(&status, sizeof(status));
    for (u32 index = 0; index < status.memberCount; ++index) {
        ModLoader_Lobby_Member record = {};
        if (ModLoader_Host_Lobby_Member(index, &record, sizeof(record)) != 0) {
            Net::Detail::Copy_Member(record, &members.emplace_back());
        }
    }
    return members;
}

std::string ModLoader::Lobby::Client_Id::Text() const {
    std::string text(sizeof(bytes) * 2, '\0');
    Bytes::Hex_Encode(bytes, sizeof(bytes), text.data());
    return text;
}

bool ModLoader::Server::Hosting() {
    return (Runtime::gSession & MODLOADER_SESSION_HOSTING) != 0;
}

bool ModLoader::Net::Configure(u16 port) {
    return ModLoader_Host_Network_Configure(port) != 0;
}
