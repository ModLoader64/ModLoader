#include "socket.h"
#include "../internal.h"

#include <modloader/net/server.h>
#include <modloader_net.h>

#include <algorithm>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

using namespace ModLoader;
using namespace ModLoader::Net;

namespace {

using Access = Net::Detail::Socket_Access;
using Endpoint = Access::Endpoint;
using Packet_Callback = Function<void(const Packet_Context&, std::span<const u8>)>;
using Socket_Callback = Function<void(const Socket&)>;

enum class Handler_Kind { Packet, Accept, Close, Connection };

struct Handler {
    u32 id = 0;
    Socket socket;
    Handler_Kind kind = Handler_Kind::Packet;
    std::string type;
    u32 version = 0;
    u64 minimumSize = 0;
    u64 maximumSize = 0;
    Packet_Callback packet;
    Socket_Callback lifecycle;
    u32 eventKind = 0;
    Function<void(const void*)> connection;
    std::weak_ptr<ModLoader::Detail::Subscription_State> subscription;
};

[[clang::no_destroy]] std::deque<Handler> sHandlers;
[[clang::no_destroy]] std::unordered_map<std::string, std::array<Socket, 2>> sOwners;
u32 sNextOwner;
u32 sNextHandler;
u32 sDispatchDepth;
u32 sRawHandlers;
bool sStopping;

constexpr u32 gEnvelopeMagic = 0x31504C4D;

struct Envelope_Header {
    u32 magic;
    u32 version;
    u64 size;
};

static_assert(sizeof(Envelope_Header) == 16);

bool Valid_Type(std::string_view type) {
    if (type.empty() || type.size() > 64) {
        return false;
    }
    for (u8 character : type) {
        if (character < 0x20 || character == 0x7F) {
            return false;
        }
    }
    return true;
}

class Envelope {
public:
    Envelope(std::string_view type, u32 version, std::span<const u8> payload, Transport transport) {
        if (!Valid_Type(type) || payload.size() > gMaxLobbyPacketSize ||
            (transport != Transport::Tcp && transport != Transport::Udp) ||
            (transport == Transport::Udp && payload.size() > gMaxLobbyUdpPacketSize)) {
            return;
        }

        size = sizeof(Envelope_Header) + payload.size();
        bytes = static_cast<u8*>(malloc(size));
        if (bytes == nullptr) {
            return;
        }

        Envelope_Header header = {gEnvelopeMagic, version, payload.size()};
        __builtin_memcpy(bytes, &header, sizeof(header));
        if (!payload.empty()) {
            __builtin_memcpy(bytes + sizeof(header), payload.data(), payload.size());
        }
    }

    Envelope(const Envelope&) = delete;
    Envelope& operator=(const Envelope&) = delete;

    ~Envelope() {
        free(bytes);
    }

    u8* bytes = nullptr;
    usize size = 0;
};

bool Same_Transport(const Socket& left, const Socket& right) {
    return Access::Kind(left) == Access::Kind(right) && (!Access::Is_Raw(left) || Access::Id(left) == Access::Id(right));
}

u32 Lobby_Event_Id(const Socket& socket) {
    return Access::Kind(socket) == Endpoint::Server ? MODLOADER_EVENT_SERVER : MODLOADER_EVENT_LOBBY;
}

bool Has_Topic(const Socket& socket, std::string_view type) {
    return std::any_of(sHandlers.begin(), sHandlers.end(), [&](const Handler& handler) {
        return handler.id != 0 && handler.kind == Handler_Kind::Packet && Same_Transport(handler.socket, socket) && handler.type == type;
    });
}

void Compact() {
    if (sDispatchDepth != 0) {
        return;
    }

    ++sDispatchDepth;
    while (true) {
        std::vector<Handler> retired;
        for (auto& handler : sHandlers) {
            if (handler.id == 0) {
                retired.push_back(std::move(handler));
            }
        }

        if (retired.empty()) {
            break;
        }

        std::erase_if(sHandlers, [](const Handler& handler) {
            return handler.id == 0;
        });
    }
    --sDispatchDepth;
}

template<typename Visit>
void Visit_Handlers(Visit visit) {
    const usize count = sHandlers.size();
    ++sDispatchDepth;
    for (usize index = 0; index < count; ++index) {
        Handler& handler = sHandlers[index];
        if (handler.id != 0) {
            visit(handler);
        }
    }
    --sDispatchDepth;
    Compact();
}

u32 Add(Handler&& handler) {
    if (sStopping || Access::Id(handler.socket) == 0 || sNextHandler == UINT32_MAX) {
        return 0;
    }

    if (Access::Is_Raw(handler.socket)) {
        if (ModLoader_Host_Socket_Packets(Access::Id(handler.socket)) == 0) {
            return 0;
        }
        if (sRawHandlers == 0 && !Runtime::Follow_Event(MODLOADER_EVENT_NET, true)) {
            return 0;
        }
        ++sRawHandlers;
    }
    else if (handler.kind == Handler_Kind::Connection) {
        if (!Runtime::Follow_Event(Lobby_Event_Id(handler.socket), true)) {
            return 0;
        }
    }
    else if (!Has_Topic(handler.socket, handler.type) &&
        ModLoader_Host_Lobby_Packet_Follow(Lobby_Event_Id(handler.socket), handler.type.data(), handler.type.size(), 1) == 0) {
        return 0;
    }

    handler.id = ++sNextHandler;
    sHandlers.push_back(std::move(handler));
    return sNextHandler;
}

void Remove(Handler& handler) {
    handler.id = 0;
    if (auto subscription = handler.subscription.lock()) {
        subscription->Dismiss();
    }

    if (Access::Is_Raw(handler.socket)) {
        if (--sRawHandlers == 0) {
            Runtime::Follow_Event(MODLOADER_EVENT_NET, false);
        }
    }
    else if (handler.kind == Handler_Kind::Connection) {
        Runtime::Follow_Event(Lobby_Event_Id(handler.socket), false);
    }
    else if (!Has_Topic(handler.socket, handler.type)) {
        ModLoader_Host_Lobby_Packet_Follow(Lobby_Event_Id(handler.socket), handler.type.data(), handler.type.size(), 0);
    }
}

void Dispatch_Packet(const Packet_Context& context, std::string_view type, u32 version, std::span<const u8> data) {
    Visit_Handlers([&](Handler& handler) {
        if (handler.kind != Handler_Kind::Packet ||
            !Same_Transport(handler.socket, context.socket) || handler.type != type ||
            handler.version != version || data.size() < handler.minimumSize || data.size() > handler.maximumSize) {
            return;
        }

        Packet_Context owned_context = context;
        owned_context.socket = handler.socket;
        handler.packet(owned_context, data);
    });
}

void Dispatch_Lifecycle(const Socket& owner, Handler_Kind kind, const Socket& socket) {
    Visit_Handlers([&](Handler& handler) {
        if (handler.kind == kind && Same_Transport(handler.socket, owner)) {
            handler.lifecycle(Access::Is_Raw(socket) ? socket : handler.socket);
        }
    });
}

void Dispatch_Connection(Endpoint endpoint, u32 kind, const void* value) {
    Visit_Handlers([&](Handler& handler) {
        if (handler.kind == Handler_Kind::Connection &&
            Access::Kind(handler.socket) == endpoint && handler.eventKind == kind) {
            handler.connection(value);
        }
    });
}

void Dispatch_Envelope(const Packet_Context& context, std::string_view type, std::span<const u8> data) {
    if (data.size() < sizeof(Envelope_Header)) {
        return;
    }

    Envelope_Header header;
    __builtin_memcpy(&header, data.data(), sizeof(header));
    if (header.magic != gEnvelopeMagic || header.size != data.size() - sizeof(header) || header.size > gMaxPacketSize) {
        return;
    }
    Dispatch_Packet(context, type, header.version, data.subspan(sizeof(header)));
}

void Read_Envelope(const Packet_Context& context, std::string_view type, u64 size) {
    if (!Has_Topic(context.socket, type) || size < sizeof(Envelope_Header) || size > gMaxPacketSize) {
        return;
    }

    std::vector<u8> data(static_cast<usize>(size));
    if (ModLoader_Host_Lobby_Read(0, data.data(), size) == size) {
        Dispatch_Envelope(context, type, data);
    }
}

} // namespace

void Net::Detail::Remove_Socket(const Socket& socket) {
    u64 key = Access::Key(socket);
    Visit_Handlers([&](Handler& handler) {
        if (Access::Key(handler.socket) == key) {
            Remove(handler);
        }
    });
}

Socket Net::Detail::Shared_Socket(bool server, std::string_view owner) {
    if (sStopping || (server && !Server::Hosting())) {
        return {};
    }

    Socket& socket = sOwners[std::string(owner)][server ? 1 : 0];
    if (!socket.Is_Open()) {
        if (sNextOwner == UINT32_MAX) {
            return {};
        }
        socket = Access::Make(++sNextOwner, server ? Endpoint::Server : Endpoint::Lobby);
    }
    return socket;
}

Subscription Net::Detail::Subscribe_Connection(const Socket& socket, u32 kind, Function<void(const void*)> callback) {
    if (!callback || Access::Is_Raw(socket)) {
        return {};
    }

    Handler handler;
    handler.socket = socket;
    handler.kind = Handler_Kind::Connection;
    handler.eventKind = kind;
    handler.connection = std::move(callback);
    return Access::Scope(socket, Add(std::move(handler)));
}

u32 Socket::Register(std::string_view type, u32 version, u64 minimum_size, u64 maximum_size, Packet_Callback callback) const {
    u64 limit = Access::Is_Raw(*this) ? gMaxPacketSize : gMaxLobbyPacketSize;
    if (!callback || !Valid_Type(type) || minimum_size > maximum_size || maximum_size > limit) {
        return 0;
    }

    Handler handler;
    handler.socket = *this;
    handler.type = type;
    handler.version = version;
    handler.minimumSize = minimum_size;
    handler.maximumSize = maximum_size;
    handler.packet = std::move(callback);
    return Add(std::move(handler));
}

u32 Socket::Register_Packet(std::string_view type, u32 version, u64 maximum_size, Packet_Callback callback) const {
    return Register(type, version, 0, maximum_size, std::move(callback));
}

void Socket::Unregister(u32 token) const {
    for (Handler& handler : sHandlers) {
        if (handler.id == token && token != 0 && Access::Key(handler.socket) == Access::Key(*this)) {
            Remove(handler);
            break;
        }
    }
    Compact();
}

u32 Socket::On_Accept(Socket_Callback callback) const {
    if (!callback || endpoint != Endpoint::Tcp) {
        return 0;
    }

    Handler handler;
    handler.socket = *this;
    handler.kind = Handler_Kind::Accept;
    handler.lifecycle = std::move(callback);
    return Add(std::move(handler));
}

u32 Socket::On_Close(Socket_Callback callback) const {
    if (!callback || !Access::Is_Raw(*this)) {
        return 0;
    }

    Handler handler;
    handler.socket = *this;
    handler.kind = Handler_Kind::Close;
    handler.lifecycle = std::move(callback);
    return Add(std::move(handler));
}

Subscription Socket::Own_Registration(u32 token) const {
    for (auto& handler : sHandlers) {
        if (token != 0 && handler.id == token && Access::Key(handler.socket) == Access::Key(*this)) {
            auto state = std::make_shared<ModLoader::Detail::Subscription_State>();
            state->cleanup = [socket = *this, token] {
                socket.Unregister(token);
            };
            handler.subscription = state;
            return Subscription(std::move(state));
        }
    }
    return {};
}

Subscription Socket::Subscribe(std::string_view type, u32 version, u64 maximum_size, Packet_Callback callback) const {
    return Own_Registration(Register_Packet(type, version, maximum_size, std::move(callback)));
}

Subscription Socket::Subscribe_Accept(Socket_Callback callback) const {
    return Own_Registration(On_Accept(std::move(callback)));
}

Subscription Socket::Subscribe_Close(Socket_Callback callback) const {
    return Own_Registration(On_Close(std::move(callback)));
}

bool Socket::Send(std::string_view type, u32 version, std::span<const u8> data, Transport transport) const {
    u32 id = Id();
    if (id == 0 || !Valid_Type(type) || data.size() > gMaxPacketSize) {
        return false;
    }

    if (endpoint == Endpoint::Tcp) {
        return transport == Transport::Tcp && ModLoader_Host_Socket_Packets(id) != 0 &&
            ModLoader_Host_Socket_Packet_Send(id, type.data(), type.size(), version, data.data(), data.size()) != 0;
    }

    if (endpoint != Endpoint::Lobby) {
        return false;
    }

    Envelope envelope(type, version, data, transport);
    return envelope.bytes != nullptr && ModLoader_Host_Lobby_Send(type.data(), type.size(), envelope.bytes, envelope.size, static_cast<u32>(transport)) != 0;
}

bool Socket::Send_To(const Address& address, std::string_view type, u32 version, std::span<const u8> data) const {
    u32 id = Id();
    return id != 0 && endpoint == Endpoint::Udp && Valid_Type(type) && data.size() <= gMaxPacketSize && ModLoader_Host_Socket_Packets(id) != 0
        && ModLoader_Host_Socket_Packet_Send_To(id, type.data(), type.size(), version, data.data(), data.size(), address.ipv4, address.port) != 0;
}

bool Socket::Send_Server(std::string_view type, u32 version, std::span<const u8> data, Transport transport) const {
    if (Id() == 0 || endpoint != Endpoint::Lobby) {
        return false;
    }

    Envelope envelope(type, version, data, transport);
    return envelope.bytes != nullptr && ModLoader_Host_Lobby_Send_Server(type.data(), type.size(), envelope.bytes, envelope.size, 0, static_cast<u32>(transport)) != 0;
}

bool Socket::Send_To(const Lobby::Client_Id& member, std::string_view type, u32 version, std::span<const u8> data, Transport transport) const {
    if (Id() == 0 || endpoint != Endpoint::Lobby) {
        return false;
    }
    Envelope envelope(type, version, data, transport);
    return envelope.bytes != nullptr && ModLoader_Host_Lobby_Send_To(member.bytes, type.data(), type.size(), envelope.bytes, envelope.size, static_cast<u32>(transport)) != 0;
}

bool Socket::Send_To(Server::Lobby::Handle lobby, const Lobby::Client_Id& member, std::string_view type, u32 version, std::span<const u8> data, Transport transport) const {
    if (Id() == 0 || endpoint != Endpoint::Server) {
        return false;
    }

    Envelope envelope(type, version, data, transport);
    return envelope.bytes != nullptr && ModLoader_Host_Server_Send(lobby, member.bytes, nullptr, type.data(), type.size(), envelope.bytes, envelope.size, 0, static_cast<u32>(transport)) != 0;
}

bool Socket::Broadcast(Server::Lobby::Handle lobby, std::string_view type, u32 version, std::span<const u8> data, const Lobby::Client_Id* except, Transport transport) const {
    if (Id() == 0 || endpoint != Endpoint::Server) {
        return false;
    }

    Envelope envelope(type, version, data, transport);
    return envelope.bytes != nullptr && ModLoader_Host_Server_Send(lobby, nullptr, except != nullptr ? except->bytes : nullptr, type.data(), type.size(), envelope.bytes, envelope.size, 0, static_cast<u32>(transport)) != 0;
}

bool Packet_Context::Reply(std::string_view type, u32 version, std::span<const u8> data, Transport transport) const {
    switch (Access::Kind(socket)) {
    case Endpoint::Tcp:
        return socket.Send(type, version, data, transport);
    case Endpoint::Udp:
        return transport == Transport::Udp && socket.Send_To(address, type, version, data);
    case Endpoint::Lobby:
        return fromServer ? socket.Send_Server(type, version, data, transport) : socket.Send_To(sender.id, type, version, data, transport);
    case Endpoint::Server:
        return socket.Send_To(lobby, sender.id, type, version, data, transport);
    }
    return false;
}

void ModLoader::Runtime::Net_Event(const void* bytes, u64 size) {
    if (size < sizeof(ModLoader_Net_Record)) {
        return;
    }

    ModLoader_Net_Record record;
    __builtin_memcpy(&record, bytes, sizeof(record));
    if (record.size != size - sizeof(record) || record.size > gMaxPacketSize) {
        return;
    }

    Socket socket;
    for (const Handler& handler : sHandlers) {
        if (handler.id != 0 && Access::Is_Raw(handler.socket) && Access::Id(handler.socket) == record.socket) {
            socket = handler.socket;
            break;
        }
    }

    if (record.kind == MODLOADER_NET_PACKET) {
        const char* end = static_cast<const char*>(__builtin_memchr(record.type, '\0', sizeof(record.type)));
        if (end == nullptr) {
            return;
        }
        Packet_Context context;
        context.socket = socket;
        context.transport = static_cast<Net::Transport>(record.transport);
        context.address = {record.address, static_cast<u16>(record.port)};
        Dispatch_Packet(context, {record.type, static_cast<usize>(end - record.type)}, record.version,
            {static_cast<const u8*>(bytes) + sizeof(record), static_cast<usize>(record.size)});
    }
    else if (record.kind == MODLOADER_NET_ACCEPTED) {
        Socket accepted(record.accepted);
        Dispatch_Lifecycle(socket, Handler_Kind::Accept, accepted);
    }
    else if (record.kind == MODLOADER_NET_CLOSED) {
        Dispatch_Lifecycle(socket, Handler_Kind::Close, socket);
        socket.Close();
    }
}

void ModLoader::Runtime::Net_Shutdown() {
    sStopping = true;
    Visit_Handlers(Remove);
    sOwners.clear();
}

void ModLoader::Runtime::Lobby_Event(const void* bytes, u64 size) {
    if (size < sizeof(ModLoader_Lobby_Record)) {
        return;
    }

    ModLoader_Lobby_Record record;
    __builtin_memcpy(&record, bytes, sizeof(record));
    Lobby::Member member;
    Net::Detail::Copy_Member(record.member, &member);

    switch (record.kind) {
    case MODLOADER_LOBBY_JOINED:
    case MODLOADER_LOBBY_MEMBER_JOINED:
    case MODLOADER_LOBBY_MEMBER_LEFT:
        Dispatch_Connection(Endpoint::Lobby, record.kind, &member);
        break;
    case MODLOADER_LOBBY_LEFT:
        Dispatch_Connection(Endpoint::Lobby, record.kind, nullptr);
        break;
    case MODLOADER_LOBBY_MESSAGE:
    case MODLOADER_LOBBY_SERVER_MESSAGE: {
        record.topic[sizeof(record.topic) - 1] = '\0';
        Packet_Context context;
        context.socket = Access::Transport(Endpoint::Lobby);
        context.transport = static_cast<Net::Transport>(record.transport);
        context.sender = member;
        context.fromServer = record.kind == MODLOADER_LOBBY_SERVER_MESSAGE;
        Read_Envelope(context, record.topic, record.size);
        break;
    }
    }
}

void ModLoader::Runtime::Server_Event(const void* bytes, u64 size) {
    if (size < sizeof(ModLoader_Server_Record)) {
        return;
    }

    ModLoader_Server_Record record;
    __builtin_memcpy(&record, bytes, sizeof(record));
    record.lobbyName[sizeof(record.lobbyName) - 1] = '\0';
    Server::Lobby lobby{record.lobby, record.lobbyName};
    
    switch (record.kind) {
    case MODLOADER_SERVER_LOBBY_OPENED:
    case MODLOADER_SERVER_LOBBY_CLOSED:
        Dispatch_Connection(Endpoint::Server, record.kind, &lobby);
        break;
    case MODLOADER_SERVER_MEMBER_JOINED:
    case MODLOADER_SERVER_MEMBER_LEFT: {
        Server::Member member{lobby};
        Net::Detail::Copy_Member(record.member, &member.member);
        __builtin_memcpy(member.publicKey.data(), record.publicKey, member.publicKey.size());
        Dispatch_Connection(Endpoint::Server, record.kind, &member);
        break;
    }
    case MODLOADER_SERVER_MESSAGE: {
        if (record.sealed != 0) {
            break;
        }
        record.topic[sizeof(record.topic) - 1] = '\0';
        Packet_Context context;
        context.socket = Access::Transport(Endpoint::Server);
        context.transport = static_cast<Net::Transport>(record.transport);
        context.lobby = record.lobby;
        Net::Detail::Copy_Member(record.member, &context.sender);
        __builtin_memcpy(context.publicKey.data(), record.publicKey, context.publicKey.size());
        Read_Envelope(context, record.topic, record.size);
        break;
    }
    }
}
