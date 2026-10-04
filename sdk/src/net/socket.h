#pragma once

#include <modloader/net/socket.h>

namespace ModLoader::Net::Detail {

struct Socket_Access {
    using Endpoint = Socket::Endpoint;

    static Socket Make(u32 id, Endpoint endpoint);
    static u64 Key(const Socket& socket);

    static Socket Transport(Endpoint endpoint) {
        Socket socket;
        socket.endpoint = endpoint;
        return socket;
    }

    static Subscription Scope(const Socket& socket, u32 token) {
        return socket.Own_Registration(token);
    }

    static u32 Id(const Socket& socket) {
        return socket.Id();
    }

    static Endpoint Kind(const Socket& socket) {
        return socket.endpoint;
    }

    static bool Is_Raw(const Socket& socket) {
        return socket.endpoint == Endpoint::Tcp || socket.endpoint == Endpoint::Udp;
    }
};

void Remove_Socket(const Socket& socket);
void Copy_Member(const ModLoader_Lobby_Member& record, Lobby::Member* out_member);

}
