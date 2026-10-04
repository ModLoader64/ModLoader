#pragma once

#include <modloader/net/lobby.h>
#include <modloader/net/server.h>
#include <modloader/async/promise.h>
#include <modloader/net/packet.h>
#include <modloader/subscription.h>
#include <modloader/detail/identity.h>

#include <span>
#include <array>
#include <memory>
#include <string_view>

namespace ModLoader::Net {

struct Packet_Context;
class Lobby_Connection;
class Server_Connection;
static inline Lobby_Connection Lobby_Socket();
static inline Server_Connection Server_Socket();

bool Configure(u16 port);

// Copies and registrations share ownership
// Close closes all copies and removes their handlers
class Socket {
public:
    Socket() = default;
    explicit Socket(u32 id);

    Socket Accept(u32 timeout_milliseconds) const;
    s64 Send(const void* data, u64 size) const;
    s64 Receive(void* buffer, u64 size, u32 timeout_milliseconds) const;
    s64 Send_To(std::string_view host, u16 port, const void* data, u64 size) const;
    s64 Receive_From(void* buffer, u64 size, Address* out_from, u32 timeout_milliseconds) const;
    u16 Port() const; // Local bound port; 0 for a managed endpoint or a closed socket
    // Managed endpoints remain available while disconnected; Lobby::Get_State reports the connection
    bool Is_Open() const;
    void Close();

    // Callback data is an aligned copy borrowed until the callback returns; returns 0 on failure
    template<typename T, typename Callable>
    u32 Register_Packet(const Packet_Type<T>& type, Callable callback) const {
        static_assert(std::is_invocable_r_v<void, Callable&, const Packet_Context&, const T*>);
        if constexpr (std::is_pointer_v<Callable>) {
            if (callback == nullptr) {
                return 0;
            }
        }

        return Register(type.name, type.version, sizeof(T), sizeof(T),
            [callback = std::move(callback)](const Packet_Context& context, std::span<const u8> bytes) mutable {
                T* value = static_cast<T*>(aligned_alloc(alignof(T), sizeof(T)));
                if (value == nullptr) {
                    return;
                }
                __builtin_memcpy(value, bytes.data(), sizeof(T));
                callback(context, static_cast<const T*>(value));
                free(value);
            });
    }

    // Raw packet data expires when the callback returns; names are copied; 0 means registration failed
    u32 Register_Packet(std::string_view type, u32 version, u64 maximum_size,
        Function<void(const Packet_Context&, std::span<const u8>)> callback) const;
    void Unregister(u32 handler) const; // Removes a registration made on this socket
    // Raw sockets only
    u32 On_Accept(Function<void(const Socket&)> callback) const;
    u32 On_Close(Function<void(const Socket&)> callback) const;

    // Keep the Subscription alive to keep its handler registered
    template<typename T, typename Callable>
    [[nodiscard]] Subscription Subscribe(const Packet_Type<T>& type, Callable callback) const {
        return Own_Registration(Register_Packet(type, std::move(callback)));
    }

    [[nodiscard]] Subscription Subscribe(std::string_view type, u32 version, u64 maximum_size,
        Function<void(const Packet_Context&, std::span<const u8>)> callback) const;
    [[nodiscard]] Subscription Subscribe_Accept(Function<void(const Socket&)> callback) const;
    [[nodiscard]] Subscription Subscribe_Close(Function<void(const Socket&)> callback) const;

    // Packet sends copy the payload before returning
    // Lobby UDP is unreliable, server relayed, and limited to gMaxLobbyUdpPacketSize bytes
    bool Send(std::string_view type, u32 version, std::span<const u8> data, Transport transport = Transport::Tcp) const; // peer or all other lobby members
    bool Send_To(const Address& address, std::string_view type, u32 version, std::span<const u8> data) const; // Raw UDP
    bool Send_Server(std::string_view type, u32 version, std::span<const u8> data, Transport transport = Transport::Tcp) const; // Lobby client to server handlers
    bool Send_To(const Lobby::Client_Id& member, std::string_view type, u32 version, std::span<const u8> data, Transport transport = Transport::Tcp) const; // Lobby member via server
    bool Send_To(Server::Lobby::Handle lobby, const Lobby::Client_Id& member, std::string_view type, u32 version, std::span<const u8> data, Transport transport = Transport::Tcp) const; // Server to one member
    // Server to every member of the lobby, optionally excluding one
    bool Broadcast(Server::Lobby::Handle lobby, std::string_view type, u32 version, std::span<const u8> data,
        const Lobby::Client_Id* except = nullptr, Transport transport = Transport::Tcp) const;

    template<typename T>
    bool Send(const Packet_Type<T>& type, const T* value, Transport transport = Transport::Tcp) const {
        return Detail::Send_Value(value, [&](std::span<const u8> bytes) {
            return Send(type.name, type.version, bytes, transport);
        });
    }

    template<typename T>
    bool Send_To(const Address& address, const Packet_Type<T>& type, const T* value) const {
        return Detail::Send_Value(value, [&](std::span<const u8> bytes) {
            return Send_To(address, type.name, type.version, bytes);
        });
    }

    template<typename T>
    bool Send_Server(const Packet_Type<T>& type, const T* value, Transport transport = Transport::Tcp) const {
        return Detail::Send_Value(value, [&](std::span<const u8> bytes) {
            return Send_Server(type.name, type.version, bytes, transport);
        });
    }

    template<typename T>
    bool Send_To(const Lobby::Client_Id& member, const Packet_Type<T>& type, const T* value, Transport transport = Transport::Tcp) const {
        return Detail::Send_Value(value, [&](std::span<const u8> bytes) {
            return Send_To(member, type.name, type.version, bytes, transport);
        });
    }

    template<typename T>
    bool Send_To(Server::Lobby::Handle lobby, const Lobby::Client_Id& member, const Packet_Type<T>& type, const T* value, Transport transport = Transport::Tcp) const {
        return Detail::Send_Value(value, [&](std::span<const u8> bytes) {
            return Send_To(lobby, member, type.name, type.version, bytes, transport);
        });
    }

    template<typename T>
    bool Broadcast(Server::Lobby::Handle lobby, const Packet_Type<T>& type, const T* value, const Lobby::Client_Id* except = nullptr, Transport transport = Transport::Tcp) const {
        return Detail::Send_Value(value, [&](std::span<const u8> bytes) {
            return Broadcast(lobby, type.name, type.version, bytes, except, transport);
        });
    }

private:
    friend struct Detail::Socket_Access;
    enum class Endpoint : u32 { Tcp, Udp, Lobby, Server };
    struct State;

    u32 Register(std::string_view type, u32 version, u64 minimum_size, u64 maximum_size,
        Function<void(const Packet_Context&, std::span<const u8>)> callback) const;
    Subscription Own_Registration(u32 handler) const;
    u32 Id() const;

    std::shared_ptr<State> state;
    Endpoint endpoint = Endpoint::Tcp;
};

struct Packet_Context {
    Socket socket;
    Server::Lobby::Handle lobby = 0;
    Transport transport = Transport::Tcp;
    Lobby::Member sender = {};
    std::array<u8, 32> publicKey = {}; // server receives only
    Address address = {};
    bool fromServer = false;

    // Replies use the received transport unless overridden
    bool Reply(std::string_view type, u32 version, std::span<const u8> data, Transport transport) const;

    bool Reply(std::string_view type, u32 version, std::span<const u8> data) const {
        return Reply(type, version, data, transport);
    }

    template<typename T>
    bool Reply(const Packet_Type<T>& type, const T* value) const {
        return Reply(type, value, transport);
    }

    template<typename T>
    bool Reply(const Packet_Type<T>& type, const T* value, Transport transport) const {
        return Detail::Send_Value(value, [&](std::span<const u8> bytes) {
            return Reply(type.name, type.version, bytes, transport);
        });
    }
};

// Raw TCP/UDP endpoints are independent of the managed lobby connection
Socket Tcp_Connect(std::string_view host, u16 port, u32 timeout_milliseconds);
Socket Tcp_Listen(u16 port, std::string_view address = {});
Socket Udp_Open(u16 port = 0);
Promise<Socket> Tcp_Connect_Async(std::string_view host, u16 port, u32 timeout_milliseconds); // Copies host; rejects on connection failure

namespace Detail {
Socket Shared_Socket(bool server, std::string_view owner);
Subscription Subscribe_Connection(const Socket& socket, u32 kind, Function<void(const void*)> callback);

template<typename T>
class Connection_Event {
public:
    [[nodiscard]] Subscription Subscribe(Function<void(const T&)> callback) const {
        if (!callback) {
            return {};
        }

        return Subscribe_Connection(socket, kind, [callback = std::move(callback)](const void* value) {
            callback(*static_cast<const T*>(value));
        });
    }

private:
    friend class Net::Lobby_Connection;
    friend class Net::Server_Connection;

    Connection_Event(const Socket& socket, u32 kind) : socket(socket), kind(kind) {
    }

    Socket socket;
    u32 kind;
};

template<>
class Connection_Event<void> {
public:
    [[nodiscard]] Subscription Subscribe(Function<void()> callback) const {
        if (!callback) {
            return {};
        }

        return Subscribe_Connection(socket, kind, [callback = std::move(callback)](const void*) {
            callback();
        });
    }

private:
    friend class Net::Lobby_Connection;

    Connection_Event(const Socket& socket, u32 kind) : socket(socket), kind(kind) {
    }

    Socket socket;
    u32 kind;
};
} // namespace Detail

class Lobby_Connection : public Socket {
public:
    Lobby_Connection() = default;

    // Event_Joined supplies Self(); member events describe another player
    Detail::Connection_Event<Lobby::Member> Event_Joined{*this, MODLOADER_LOBBY_JOINED};
    Detail::Connection_Event<Lobby::Member> Event_Member_Joined{*this, MODLOADER_LOBBY_MEMBER_JOINED};
    Detail::Connection_Event<Lobby::Member> Event_Member_Left{*this, MODLOADER_LOBBY_MEMBER_LEFT};
    Detail::Connection_Event<void> Event_Left{*this, MODLOADER_LOBBY_LEFT};

private:
    friend Lobby_Connection Lobby_Socket();

    explicit Lobby_Connection(Socket socket) : Socket(std::move(socket)) {
    }
};

class Server_Connection : public Socket {
public:
    Server_Connection() = default;

    Detail::Connection_Event<Server::Lobby> Event_Lobby_Opened{*this, MODLOADER_SERVER_LOBBY_OPENED};
    Detail::Connection_Event<Server::Lobby> Event_Lobby_Closed{*this, MODLOADER_SERVER_LOBBY_CLOSED};
    Detail::Connection_Event<Server::Member> Event_Member_Joined{*this, MODLOADER_SERVER_MEMBER_JOINED};
    Detail::Connection_Event<Server::Member> Event_Member_Left{*this, MODLOADER_SERVER_MEMBER_LEFT};

private:
    friend Server_Connection Server_Socket();

    explicit Server_Connection(Socket socket) : Socket(std::move(socket)) {
    }
};

// Managed endpoints share the client connection; each plugin owns its own registrations
// Closing an endpoint removes that plugin's handlers
static inline Lobby_Connection Lobby_Socket() {
    return Lobby_Connection(Detail::Shared_Socket(false, ModLoader::Detail::gModuleName));
}

static inline Server_Connection Server_Socket() {
    return Server_Connection(Detail::Shared_Socket(true, ModLoader::Detail::gModuleName));
}

} // namespace ModLoader::Net
