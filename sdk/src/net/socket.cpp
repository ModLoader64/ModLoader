#include "socket.h"
#include "../host_imports.h"
#include <modloader/async/task.h>
#include <atomic>
#include <string>

using namespace ModLoader;
using namespace ModLoader::Net;
using Access = Net::Detail::Socket_Access;
using Endpoint = Access::Endpoint;

struct Socket::State {
    State(u32 id, bool raw) : id(id), key(id), raw(raw) {
    }

    ~State() {
        Close();
    }

    void Close() {
        if (u32 closing = id.exchange(0); closing != 0 && raw) {
            ModLoader_Host_Socket_Close(closing);
        }
    }

    std::atomic<u32> id;
    const u32 key;
    const bool raw;
};

Socket::Socket(u32 id) : state(id != 0 ? std::make_shared<State>(id, true) : nullptr) {
}

u32 Socket::Id() const {
    return state ? state->id.load() : 0;
}

Socket Net::Detail::Socket_Access::Make(u32 id, Endpoint endpoint) {
    Socket socket;
    socket.endpoint = endpoint;
    if (id != 0) {
        socket.state = std::make_shared<Socket::State>(id, Is_Raw(socket));
    }
    return socket;
}

u64 Net::Detail::Socket_Access::Key(const Socket& socket) {
    return (static_cast<u64>(socket.endpoint) << 32) | (socket.state ? socket.state->key : 0);
}

Socket Net::Tcp_Connect(std::string_view host, u16 port, u32 timeout_milliseconds) {
    return Socket(ModLoader_Host_Tcp_Connect(host.data(), host.size(), port, timeout_milliseconds));
}

Socket Net::Tcp_Listen(u16 port, std::string_view address) {
    return Socket(ModLoader_Host_Tcp_Listen(address.data(), address.size(), port));
}

Socket Net::Udp_Open(u16 port) {
    return Access::Make(ModLoader_Host_Udp_Open(port), Endpoint::Udp);
}

Socket Socket::Accept(u32 timeout_milliseconds) const {
    return endpoint == Endpoint::Tcp ? Socket(ModLoader_Host_Tcp_Accept(Id(), timeout_milliseconds)) : Socket();
}

s64 Socket::Send(const void* data, u64 size) const {
    return endpoint == Endpoint::Tcp ? ModLoader_Host_Socket_Send(Id(), data, size) : gError;
}

s64 Socket::Receive(void* buffer, u64 size, u32 timeout_milliseconds) const {
    return endpoint == Endpoint::Tcp ? ModLoader_Host_Socket_Receive(Id(), buffer, size, timeout_milliseconds) : gError;
}

s64 Socket::Send_To(std::string_view host, u16 port, const void* data, u64 size) const {
    return endpoint == Endpoint::Udp ? ModLoader_Host_Udp_Send_To(Id(), host.data(), host.size(), port, data, size) : gError;
}

s64 Socket::Receive_From(void* buffer, u64 size, Address* out_from, u32 timeout_milliseconds) const {
    if (endpoint != Endpoint::Udp) {
        return gError;
    }

    u32 sender[2] = {};
    s64 result = ModLoader_Host_Udp_Receive_From(Id(), buffer, size, sender, timeout_milliseconds);
    if (out_from != nullptr) {
        out_from->ipv4 = sender[0];
        out_from->port = static_cast<u16>(sender[1]);
    }

    return result;
}

u16 Socket::Port() const {
    return Access::Is_Raw(*this) ? static_cast<u16>(ModLoader_Host_Socket_Port(Id())) : 0;
}

bool Socket::Is_Open() const {
    return Id() != 0 && (!Access::Is_Raw(*this) || Port() != 0);
}

void Socket::Close() {
    if (Id() == 0) {
        return;
    }
    
    state->Close();
    Net::Detail::Remove_Socket(*this);
}

Promise<Socket> Net::Tcp_Connect_Async(std::string_view host, u16 port, u32 timeout_milliseconds) {
    Promise<Socket> connected = Promise<Socket>::Create();
    Task::Submit(connected.Handle(), [host = std::string(host), port, timeout_milliseconds, connected] {
        Socket socket = Tcp_Connect(host, port, timeout_milliseconds);
        if (!socket.Is_Open()) {
            connected.Reject(Error::Failed);
        }
        else {
            connected.Resolve(std::move(socket));
        }
    });
    return connected;
}

