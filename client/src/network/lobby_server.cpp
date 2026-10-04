#include "network/lobby_server.h"
#include <modloader_bytes.h>

#include <string.h>

namespace {

constexpr u32 gMaxConnections = 1024;
constexpr u64 gHelloTimeout = 15000;
constexpr u64 gMaxPendingOutput = 256ull * 1024 * 1024;
constexpr u32 gReceiveChunk = 64 * 1024;
constexpr u32 gPollInterval = 10;
constexpr char gKeyName[] = "server.key";

enum class Connection_Stage : u32 {
    Hello,
    Identity,
    Ready,
};

}

struct Lobby_Server::Connection {
    Socket socket;
    Connection_Stage stage = Connection_Stage::Hello;
    u64 connectedAt = Time_Monotonic_Milliseconds();
    std::vector<u8> input;
    std::vector<u8> output;
    u64 outputSent = 0;
    Secure_Channel channel = {};
    Datagram_Channel datagrams;
    Socket_Address udpAddress;
    u8 transcript[64] = {};
    Lobby_Member member;
    std::array<u8, 32> publicKey = {};
    std::array<u8, 32> signingKey = {}; // only this connection's key is shared with peers
    std::string idText = "(unproven)";
    Lobby* lobby = nullptr;
    bool closing = false;
    bool dead = false;

    ~Connection() {
        Wipe(&channel, sizeof(channel));
        Wipe(&datagrams, sizeof(datagrams));
    }

    void Send(const Message_Writer& message) {
        if (dead || closing) {
            return;
        }

        if (output.size() - outputSent > gMaxPendingOutput || !channel.Seal(message.bytes, output)) {
            Log_Warning("server", "client %s dropped: send queue or encryption failure", idText.c_str());
            dead = true;
        }
    }

    void Goodbye(const char* reason) {
        Send(Message_Writer().U8(Protocol_Goodbye).Text(reason));
        closing = true;
    }
};

struct Lobby_Server::Lobby {
    u32 id;
    std::string name;
    u8 salt[gSaltSize];
    u8 verifier[32];
    u8 publicKey[32];
    u8 sealedSecret[gSealedSecretSize];
    std::vector<Connection*> members;

    ~Lobby() {
        Wipe(verifier, sizeof(verifier));
        Wipe(sealedSecret, sizeof(sealedSecret));
    }
};

std::unique_ptr<Lobby_Server> Lobby_Server::Start(const Config& config) {
    std::unique_ptr<Lobby_Server> server(new Lobby_Server());

    if (!server->Load_Key(config.dataDirectory)) {
        return nullptr;
    }

    server->listener = Socket::Listen(!config.address.empty() ? config.address.c_str() : nullptr, config.port);
    if (!server->listener.Is_Open()) {
        Log_Error("server", "cannot listen on %s port %u", config.address.empty() ? "*" : config.address.c_str(), config.port);
        return nullptr;
    }

    server->listener.Set_Blocking(false);
    server->udp = server->listener.Udp_Bound();
    if (!server->udp.Is_Open()) {
        Log_Error("server", "cannot bind UDP port %u", server->listener.Port());
        return nullptr;
    }
    if (!server->thread.Start([raw = server.get()] {
            raw->Main();
        })) {
        Log_Error("server", "cannot start server thread");
        return nullptr;
    }

    Log_Info("server", "listening on %s port %u", config.address.empty() ? "*" : config.address.c_str(), config.port);
    return server;
}

Lobby_Server::~Lobby_Server() {
    stop = true;
    thread.Join();
    while (!connections.empty()) {
        Close_Connection(connections.size() - 1);
    }
    Wipe(staticSecret, sizeof(staticSecret));
}

std::optional<Server_Event> Lobby_Server::Next_Event() {
    std::lock_guard guard(lock);
    std::optional<Server_Event> event;

    if (!events.empty()) {
        event = std::move(events.front());
        events.pop_front();
    }
    return event;
}

bool Lobby_Server::Send(u32 lobby, const Member_Id* member, const Member_Id* except, std::string_view topic, std::span<const u8> data, bool sealed, Network_Transport transport) {
    if ((transport != Network_Transport::Tcp && transport != Network_Transport::Udp) ||
        (transport == Network_Transport::Udp && data.size() > MODLOADER_LOBBY_MAX_UDP_MESSAGE) ||
        topic.empty() || topic.size() > gMaxTopic || data.size() > gMaxMessage + gSealedMessageOverhead) {
        return false;
    }
    Outgoing item = { lobby, std::nullopt, std::nullopt, std::string(topic), std::vector<u8>(data.begin(), data.end()), sealed, transport };

    if (member != nullptr) {
        item.member = *member;
    }

    if (except != nullptr) {
        item.except = *except;
    }

    std::lock_guard guard(lock);
    outgoing.push_back(std::move(item));
    return true;
}

void Lobby_Server::Push_Event(
    Server_Event_Kind kind,
    const Lobby& lobby,
    const Connection* connection,
    std::string_view topic,
    std::span<const u8> payload,
    bool sealed,
    Network_Transport transport
) {
    std::lock_guard guard(lock);

    events.push_back({
        kind, lobby.id, lobby.name, connection != nullptr ? connection->member : Lobby_Member{},
        std::string(topic), std::vector<u8>(payload.begin(), payload.end()), sealed,
        connection != nullptr ? connection->publicKey : std::array<u8, 32>{}, transport
    });
}

Lobby_Server::Lobby* Lobby_Server::Find_Lobby(std::string_view name) const {
    for (const std::unique_ptr<Lobby>& lobby : lobbies) {
        if (lobby->name == name) {
            return lobby.get();
        }
    }
    return nullptr;
}

Lobby_Server::Lobby* Lobby_Server::Lobby_Of(u32 id) const {
    for (const std::unique_ptr<Lobby>& lobby : lobbies) {
        if (lobby->id == id) {
            return lobby.get();
        }
    }
    return nullptr;
}

void Lobby_Server::Enter_Lobby(Lobby& lobby, Connection& connection) {
    Message_Writer joined;

    joined.U8(Protocol_Lobby_Joined)
        .Bytes(lobby.publicKey, 32)
        .Bytes(lobby.sealedSecret, sizeof(lobby.sealedSecret))
        .U16(static_cast<u16>(lobby.members.size()));

    for (const Connection* member : lobby.members) {
        joined.Bytes(member->member.id).Text(member->member.nickname).Bytes(member->signingKey);
    }

    connection.Send(joined);
    Message_Writer arrived = Message_Writer().U8(Protocol_Member_Joined).Bytes(connection.member.id).Text(connection.member.nickname).Bytes(connection.signingKey);
    for (Connection* member : lobby.members) {
        member->Send(arrived);
    }

    lobby.members.push_back(&connection);
    connection.lobby = &lobby;
    Push_Event(Server_Event_Kind::Member_Joined, lobby, &connection);
    Log_Info(
        "server",
        "%s (%s) joined %s (%zu members)",
        connection.member.nickname.c_str(),
        connection.idText.c_str(),
        lobby.name.c_str(),
        lobby.members.size()
    );
}

void Lobby_Server::Leave_Lobby(Connection& connection) {
    Lobby* lobby = connection.lobby;

    if (lobby == nullptr) {
        return;
    }

    connection.lobby = nullptr;
    std::erase(lobby->members, &connection);
    Push_Event(Server_Event_Kind::Member_Left, *lobby, &connection);
    Log_Info("server", "%s (%s) left %s", connection.member.nickname.c_str(), connection.idText.c_str(), lobby->name.c_str());
    if (!lobby->members.empty()) {
        Message_Writer left = Message_Writer().U8(Protocol_Member_Left).Bytes(connection.member.id);
        for (Connection* member : lobby->members) {
            member->Send(left);
        }
        return;
    }

    Push_Event(Server_Event_Kind::Lobby_Closed, *lobby);
    std::erase_if(lobbies, [&](const std::unique_ptr<Lobby>& other) {
        return other.get() == lobby;
    });
}

void Lobby_Server::Handle_Hello(Connection& connection) {
    Client_Hello hello;
    u8 ephemeral_secret[gKeySize];
    Server_Hello reply;

    memcpy(&hello, connection.input.data(), sizeof(hello));
    memcpy(reply.magic, gProtocolMagic, sizeof(reply.magic));
    memcpy(reply.staticKey, staticPublic, sizeof(reply.staticKey));
    if (memcmp(hello.magic, gProtocolMagic, sizeof(hello.magic)) != 0 || !Key_Pair_New(ephemeral_secret, reply.ephemeralKey) ||
        !Handshake_Server(ephemeral_secret, reply.ephemeralKey, staticSecret, staticPublic, hello.ephemeralKey, connection.channel, connection.transcript)) {
        connection.dead = true;
    }
    else {
        const u8* bytes = reinterpret_cast<const u8*>(&reply);
        connection.output.insert(connection.output.end(), bytes, bytes + sizeof(reply));
    }

    Wipe(ephemeral_secret, sizeof(ephemeral_secret));
    connection.stage = Connection_Stage::Identity;
}

void Lobby_Server::Handle_Identity(Connection& connection, Message_Reader& reader) {
    u8 identity[32];
    std::array<u8, 32> signing_key;
    u8 signature[64];
    std::optional<std::string> nickname;

    if (reader.U8() != Protocol_Hello || !reader.Copy(identity, 32) || !reader.Copy(signing_key) ||
        !reader.Copy(signature, 64) || !(nickname = reader.Text(gMaxNickname)) || !reader.At_End()) {
        connection.Goodbye("expected hello");
        return;
    }

    if (!Identity_Check(identity, connection.transcript, signing_key.data(), signature)) {
        connection.Goodbye("identity authentication failed");
        return;
    }

    for (const auto& other : connections) {
        if (other.get() != &connection && other->stage == Connection_Stage::Ready && !other->dead && !other->closing &&
            Keys_Equal(other->signingKey.data(), signing_key.data())) {
            connection.Goodbye("signing key already in use");
            return;
        }
    }

    connection.member = { Client_Id(identity), *nickname };
    memcpy(connection.publicKey.data(), identity, sizeof(identity));
    connection.signingKey = signing_key;
    connection.idText = Client_Id_Text(connection.member.id);
    for (const std::unique_ptr<Connection>& other : connections) {
        if (other.get() != &connection && other->stage == Connection_Stage::Ready && !other->dead && other->member.id == connection.member.id) {
            other->Goodbye("identity reconnected");
            Leave_Lobby(*other);
        }
    }

    connection.stage = Connection_Stage::Ready;
    connection.datagrams.Initialize(connection.channel);
    connection.Send(Message_Writer().U8(Protocol_Welcome).Bytes(connection.member.id));
}

void Lobby_Server::Handle_Lobby_Request(Connection& connection, u8 type, Message_Reader& reader) {
    static const u8 no_salt[gSaltSize] = {};
    std::optional<std::string> name = reader.Text(gMaxLobbyName);
    Lobby* lobby = name ? Find_Lobby(*name) : nullptr;
    u8 auth_key[32];
    u8 verifier[32];

    if (connection.lobby != nullptr || !name) {
        connection.Goodbye("invalid lobby request");
        return;
    }

    if (type == Protocol_Lobby_Query) {
        connection.Send(Message_Writer().U8(Protocol_Lobby_Info).U8(lobby != nullptr ? 1 : 0).Bytes(lobby != nullptr ? lobby->salt : no_salt, gSaltSize));
    }
    else if (type == Protocol_Lobby_Create) {
        auto created = std::make_unique<Lobby>();
        if (!reader.Copy(created->salt, gSaltSize) || !reader.Copy(auth_key, 32) || !reader.Copy(created->publicKey, 32) ||
            !reader.Copy(created->sealedSecret, gSealedSecretSize) || !reader.At_End()) {
            connection.Goodbye("invalid lobby data");
        }
        else if (lobby != nullptr) {
            connection.Send(Message_Writer().U8(Protocol_Lobby_Refused).U8(Protocol_Refusal_Exists));
        }
        else {
            created->id = ++nextLobbyId;
            created->name = *name;
            Lobby_Verifier(auth_key, created->verifier);
            lobby = lobbies.emplace_back(std::move(created)).get();
            Push_Event(Server_Event_Kind::Lobby_Opened, *lobby, &connection);
            Enter_Lobby(*lobby, connection);
        }
    }
    else if (!reader.Copy(auth_key, 32)) {
        connection.Goodbye("invalid join request");
    }
    else if (lobby == nullptr) {
        connection.Send(Message_Writer().U8(Protocol_Lobby_Refused).U8(Protocol_Refusal_Missing));
    }
    else {
        Lobby_Verifier(auth_key, verifier);
        if (Keys_Equal(verifier, lobby->verifier)) {
            Enter_Lobby(*lobby, connection);
        }
        else {
            Log_Warning(
                "server",
                "%s (%s): wrong password for %s",
                connection.member.nickname.c_str(),
                connection.idText.c_str(),
                name->c_str()
            );
            connection.Send(Message_Writer().U8(Protocol_Lobby_Refused).U8(Protocol_Refusal_Password));
            connection.closing = true;
        }
    }

    Wipe(auth_key, sizeof(auth_key));
}

void Lobby_Server::Handle_Ready(Connection& connection, Message_Reader& reader, Network_Transport transport) {
    u8 type = reader.U8();
    u8 flags;
    std::optional<std::string> topic;
    std::span<u8> data;
    Lobby* lobby = connection.lobby;
    bool sealed;
    Message_Writer message;

    if (transport == Network_Transport::Udp &&
        type != Protocol_Lobby_Send && type != Protocol_Server_Send && type != Protocol_Lobby_Send_To) {
        return;
    }

    if (type == Protocol_Lobby_Query || type == Protocol_Lobby_Create || type == Protocol_Lobby_Join) {
        Handle_Lobby_Request(connection, type, reader);
        return;
    }

    if (type != Protocol_Lobby_Send && type != Protocol_Server_Send && type != Protocol_Lobby_Send_To) {
        connection.Goodbye("unknown message type");
        return;
    }

    Member_Id recipient = {};
    if (type == Protocol_Lobby_Send_To && !reader.Copy(recipient)) {
        connection.Goodbye("invalid recipient");
        return;
    }

    flags = reader.U8();
    topic = reader.Text(gMaxTopic);
    data = reader.Rest();
    sealed = type != Protocol_Server_Send || (flags & gProtocolSealed) != 0;
    if (lobby == nullptr || !topic || reader.Failed() || data.size() > gMaxMessage + gSealedMessageOverhead ||
        (sealed && data.size() < gSealedMessageOverhead)) {
        connection.Goodbye("invalid message");
        return;
    }

    if (type == Protocol_Server_Send) {
        Push_Event(Server_Event_Kind::Message, *lobby, &connection, *topic, data, sealed, transport);
        return;
    }

    message.U8(Protocol_Lobby_Message).Bytes(connection.member.id).U8(0).Text(*topic).Bytes(data);
    for (Connection* member : lobby->members) {
        if (type == Protocol_Lobby_Send_To ? member->member.id == recipient : member != &connection) {
            Send_Message(*member, message, transport);
        }
    }
}

void Lobby_Server::Handle_Input(Connection& connection) {
    u64 offset = 0;
    u64 available;
    u8* frame;
    u32 length;

    while (!connection.dead && !connection.closing) {
        available = connection.input.size() - offset;
        if (connection.stage == Connection_Stage::Hello) {
            if (available < gClientHelloSize) {
                break;
            }
            Handle_Hello(connection);
            offset += gClientHelloSize;
            continue;
        }

        if (available < gFrameHeaderSize) {
            break;
        }

        frame = connection.input.data() + offset;
        length = Frame_Length(frame);
        if (length < gTagSize || length > gMaxFrame + gTagSize) {
            connection.dead = true;
            break;
        }

        if (available < gFrameHeaderSize + length) {
            break;
        }

        if (!connection.channel.Open(frame, gFrameHeaderSize + length)) {
            connection.dead = true;
            break;
        }

        Message_Reader reader(frame + gFrameHeaderSize, length - gTagSize);
        if (connection.stage == Connection_Stage::Identity) {
            Handle_Identity(connection, reader);
        }
        else {
            Handle_Ready(connection, reader);
        }
        offset += gFrameHeaderSize + length;
    }

    connection.input.erase(connection.input.begin(), connection.input.begin() + static_cast<std::ptrdiff_t>(offset));
}

void Lobby_Server::Send_Outgoing() {
    std::deque<Outgoing> queued;
    Lobby* lobby;
    Message_Writer message;

    {
        std::lock_guard guard(lock);

        queued.swap(outgoing);
    }

    for (const Outgoing& item : queued) {
        lobby = Lobby_Of(item.lobby);
        if (lobby == nullptr) {
            continue;
        }

        message = Message_Writer().U8(Protocol_Server_Message).U8(item.sealed ? gProtocolSealed : 0).Text(item.topic).Bytes(item.data);
        for (Connection* member : lobby->members) {
            if (item.member ? member->member.id == *item.member : !item.except || member->member.id != *item.except) {
                Send_Message(*member, message, item.transport);
            }
        }
    }
}

void Lobby_Server::Send_Message(Connection& connection, const Message_Writer& message, Network_Transport transport) {
    if (transport == Network_Transport::Tcp) {
        connection.Send(message);
        return;
    }
    std::vector<u8> packet;
    if (!connection.dead && !connection.closing && connection.udpAddress.port != 0 &&
        connection.datagrams.Seal(connection.member.id, message.bytes, packet)) {
        udp.Send_Datagram(connection.udpAddress, packet.data(), packet.size());
    }
}

void Lobby_Server::Handle_Datagrams() {
    u8 buffer[gMaxDatagram + 1];
    Socket_Address address;
    for (u32 index = 0; index < gDatagramsPerPoll && !stop; index++) {
        s64 count = udp.Receive_Datagram(buffer, sizeof(buffer), address);
        if (count < 0) {
            break;
        }
        if (static_cast<u64>(count) < gDatagramOverhead) {
            continue;
        }
        Datagram_Header header;
        memcpy(&header, buffer, sizeof(header));
        for (const auto& connection : connections) {
            if (connection->stage != Connection_Stage::Ready || connection->dead || connection->closing ||
                connection->lobby == nullptr || connection->member.id != header.client) {
                continue;
            }
            if (connection->datagrams.Open({buffer, static_cast<usize>(count)})) {
                // Reordered packets must not restore an old NAT mapping.
                if (ModLoader::Bytes::Read_Integer(header.sequence, sizeof(header.sequence), false) == connection->datagrams.receiveCount) {
                    connection->udpAddress = address;
                }
                Message_Reader reader(buffer + sizeof(header), static_cast<usize>(count) - gDatagramOverhead);
                Handle_Ready(*connection, reader, Network_Transport::Udp);
            }
            break;
        }
    }
}

void Lobby_Server::Accept_Connections() {
    Socket socket;

    while ((socket = listener.Accept()).Is_Open()) {
        if (connections.size() < gMaxConnections) {
            socket.Set_Blocking(false);
            connections.push_back(std::make_unique<Connection>());
            connections.back()->socket = std::move(socket);
        }
    }
}

void Lobby_Server::Close_Connection(usize index) {
    Connection& connection = *connections[index];

    Leave_Lobby(connection);
    connections[index] = std::move(connections.back());
    connections.pop_back();
}

void Lobby_Server::Main() {
    std::vector<Socket_Poll_Entry> entries;
    usize count;
    usize received;
    s64 moved;
    u64 now;

    while (!stop) {
        Send_Outgoing();
        count = connections.size();
        entries.assign(1, { listener.Native(), false, false, false, false });
        for (const std::unique_ptr<Connection>& connection : connections) {
            entries.push_back({ connection->socket.Native(), connection->outputSent < connection->output.size(), false, false, false });
        }
        entries.push_back({ udp.Native(), false, false, false, false });

        if (Socket_Poll(entries, gPollInterval) < 0) {
            Log_Error("server", "socket poll failed");
            break;
        }

        now = Time_Monotonic_Milliseconds();
        for (usize index = 0; index < count; index++) {
            Connection& connection = *connections[index];

            if (!connection.dead && (entries[index + 1].readable || entries[index + 1].failed)) {
                received = connection.input.size();
                connection.input.resize(received + gReceiveChunk);
                moved = connection.socket.Receive_Some(connection.input.data() + received, gReceiveChunk);
                connection.input.resize(received + static_cast<usize>(moved > 0 ? moved : 0));
                connection.dead = moved < 0;
                if (!connection.dead) {
                    Handle_Input(connection);
                }
            }

            if (connection.stage != Connection_Stage::Ready && now - connection.connectedAt > gHelloTimeout) {
                connection.dead = true;
            }
        }

        if (entries.back().readable || entries.back().failed) {
            Handle_Datagrams();
        }

        for (const std::unique_ptr<Connection>& connection : connections) {
            while (!connection->dead && connection->outputSent < connection->output.size()) {
                moved = connection->socket.Send_Some(connection->output.data() + connection->outputSent, connection->output.size() - connection->outputSent);
                if (moved <= 0) {
                    connection->dead = moved < 0;
                    break;
                }
                connection->outputSent += static_cast<u64>(moved);
            }

            if (connection->outputSent == connection->output.size()) {
                connection->output.clear();
                connection->outputSent = 0;
                connection->dead = connection->dead || connection->closing;
            }
        }

        for (usize index = connections.size(); index > 0; index--) {
            if (connections[index - 1]->dead) {
                Close_Connection(index - 1);
            }
        }

        if (entries[0].readable) {
            Accept_Connections();
        }
    }
}

bool Lobby_Server::Load_Key(const std::string& data_directory) {
    std::string path = Path_Join(data_directory, gKeyName);
    std::optional<std::vector<u8>> bytes = File_Read(path);
    bool loaded = false;

    if (bytes && bytes->size() == 32) {
        memcpy(staticSecret, bytes->data(), 32);
        loaded = true;
    }
    else if (bytes) {
        Log_Error("server", "invalid server key: %s (expected 32 bytes)", path.c_str());
    }
    else if (Random_Bytes(staticSecret, 32) && File_Write(path, staticSecret)) {
        loaded = true;
    }
    else {
        Log_Error("server", "cannot create server key: %s", path.c_str());
    }

    if (bytes) {
        Wipe(bytes->data(), bytes->size());
    }

    if (loaded) {
        Public_Key_Of(staticSecret, staticPublic);
    }

    return loaded;
}
