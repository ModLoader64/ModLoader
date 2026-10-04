#include "network/lobby_client.h"

#include "base/file.h"
#include "base/json.h"

#include <errno.h>
#include <string.h>

namespace {

constexpr u32 gConnectTimeout = 5000; // milliseconds
constexpr u32 gReplyTimeout = 15000;
constexpr u32 gPollInterval = 20;
constexpr u64 gDatagramProbeInterval = 15000;
constexpr u64 gMaxQueued = 64ull * 1024 * 1024;
constexpr u32 gReceiveChunk = 64 * 1024;
constexpr char gIdentityName[] = "identity.key";
constexpr char gKnownServersName[] = "known_servers.json";

std::string Reason(Message_Reader& reader) {
    return reader.Text(255).value_or("no reason given");
}

} // namespace

std::unique_ptr<Lobby_Client> Lobby_Client::Start(const Config& config) {
    std::unique_ptr<Lobby_Client> client;

    if (config.address.empty()) {
        return nullptr;
    }

    if (config.lobby.empty()) {
        Log_Warning("network", "no lobby configured");
        return nullptr;
    }

    if (!Text_Is_Valid(config.nickname, gMaxNickname) || !Text_Is_Valid(config.lobby, gMaxLobbyName)) {
        Log_Error("network", "invalid nickname or lobby name");
        return nullptr;
    }

    client.reset(new Lobby_Client());
    client->host = config.address;
    client->port = config.port != 0 ? config.port : gDefaultPort;
    client->server = Text_Format(client->host.find(':') != std::string::npos ? "[%s]:%u" : "%s:%u", client->host.c_str(), client->port);
    client->nickname = config.nickname;
    client->lobby = config.lobby;
    client->password = config.password;
    client->dataDirectory = config.dataDirectory;

    if (!Identity_Load(Path_Join(config.dataDirectory, gIdentityName), client->identity)) {
        return nullptr;
    }

    if (!client->thread.Start([raw = client.get()] {
            raw->Main();
        })) {
        Log_Error("network", "cannot start connection thread");
        return nullptr;
    }

    return client;
}

Lobby_Client::~Lobby_Client() {
    stop = true;
    thread.Join();
    Wipe(password.data(), password.size());
    Wipe(&identity, sizeof(identity));
    Wipe(&signingKey, sizeof(signingKey));
    Wipe(lobbySecret, sizeof(lobbySecret));
}

Lobby_State Lobby_Client::State() const {
    std::lock_guard guard(lock);

    return state;
}

Lobby_Member Lobby_Client::Self() const {
    return { identity.id, nickname };
}

void Lobby_Client::Lobby_Key(u8 out_key[32]) const {
    std::lock_guard guard(lock);

    memcpy(out_key, lobbyPublic, 32);
}

u32 Lobby_Client::Member_Count() const {
    std::lock_guard guard(lock);
    return static_cast<u32>(peers.size());
}

std::optional<Lobby_Member> Lobby_Client::Member_At(u32 index) const {
    std::lock_guard guard(lock);
    return index < peers.size() ? std::optional(peers[index].member) : std::nullopt;
}

bool Lobby_Client::Queue(std::string_view topic, std::span<const u8> data, bool to_server, bool sealed, Network_Transport transport, const Member_Id* member) {
    std::lock_guard guard(lock);

    if ((transport != Network_Transport::Tcp && transport != Network_Transport::Udp) ||
        (transport == Network_Transport::Udp && data.size() > MODLOADER_LOBBY_MAX_UDP_MESSAGE) ||
        !Text_Is_Valid(topic, gMaxTopic) || data.size() > gMaxMessage || state == Lobby_State::Failed ||
        state == Lobby_State::Disconnected || outgoingBytes + data.size() > gMaxQueued) {
        return false;
    }

    outgoing.push_back({ std::string(topic), std::vector<u8>(data.begin(), data.end()), to_server, sealed,
        member != nullptr ? std::optional<Member_Id>(*member) : std::nullopt, transport });
    outgoingBytes += data.size();
    return true;
}

bool Lobby_Client::Send(std::string_view topic, std::span<const u8> data, Network_Transport transport) {
    return Queue(topic, data, false, true, transport);
}

bool Lobby_Client::Send_To(const Member_Id& member, std::string_view topic, std::span<const u8> data, Network_Transport transport) {
    return Queue(topic, data, false, true, transport, &member);
}

bool Lobby_Client::Send_Server(std::string_view topic, std::span<const u8> data, bool sealed, Network_Transport transport) {
    return Queue(topic, data, true, sealed, transport);
}

std::optional<Lobby_Event> Lobby_Client::Next_Event() {
    std::lock_guard guard(lock);
    std::optional<Lobby_Event> event;

    if (!events.empty()) {
        event = std::move(events.front());
        events.pop_front();
    }
    return event;
}

bool Lobby_Client::Fail(const char* format, ...) {
    va_list arguments;

    if (stop) {
        failure.clear();
        return false;
    }

    va_start(arguments, format);
    failure = Text_Format_List(format, arguments);
    va_end(arguments);
    return false;
}

void Lobby_Client::Push_Event(Lobby_Event_Kind kind, const Lobby_Member& member, std::string_view topic, std::span<const u8> payload, bool sealed, Network_Transport transport) {
    std::lock_guard guard(lock);
    events.push_back({ kind, member, std::string(topic), std::vector<u8>(payload.begin(), payload.end()), sealed, transport });
}

std::optional<Lobby_Member> Lobby_Client::Find_Member(const Member_Id& id) const {
    for (const Peer& peer : peers) {
        if (peer.member.id == id) {
            return peer.member;
        }
    }
    return std::nullopt;
}

std::optional<Lobby_Member> Lobby_Client::Find_Signer(const Member_Id& id, const std::array<u8, 32>& signing_key) const {
    if (id == identity.id) {
        return Keys_Equal(signingKey.publicKey, signing_key.data()) ? std::optional(Self()) : std::nullopt;
    }

    for (const Peer& peer : peers) {
        if (peer.member.id == id && Keys_Equal(peer.signingKey.data(), signing_key.data())) {
            return peer.member;
        }
    }
    return std::nullopt;
}

bool Lobby_Client::Add_Peer(const Peer& peer) {
    std::lock_guard guard(lock);

    if (peer.member.id == identity.id || Keys_Equal(peer.signingKey.data(), signingKey.publicKey)) {
        return false;
    }

    for (const Peer& known : peers) {
        if (known.member.id == peer.member.id || Keys_Equal(known.signingKey.data(), peer.signingKey.data())) {
            return false;
        }
    }
    peers.push_back(peer);
    return true;
}

bool Lobby_Client::Wait_Ready(bool for_write, u32 timeout_milliseconds) {
    for (u32 waited = 0; !stop && waited < timeout_milliseconds; waited += 100) {
        if (socket.Wait(for_write, 100)) {
            return true;
        }
    }

    return false;
}

bool Lobby_Client::Send_All(const void* data, u64 size) {
    const u8* bytes = static_cast<const u8*>(data);
    s64 result;

    for (u64 sent = 0; sent < size; sent += static_cast<u64>(result)) {
        if (!Wait_Ready(true, gReplyTimeout)) {
            return Fail("send timeout: %s", server.c_str());
        }

        result = socket.Send_Some(bytes + sent, size - sent);
        if (result < 0) {
            return Fail("%s closed the connection", server.c_str());
        }
    }
    return true;
}

bool Lobby_Client::Receive_Exact(void* out, u64 size) {
    u8* bytes = static_cast<u8*>(out);
    s64 result;

    for (u64 received = 0; received < size; received += static_cast<u64>(result)) {
        if (!Wait_Ready(false, gReplyTimeout)) {
            return Fail("receive timeout: %s", server.c_str());
        }

        result = socket.Receive_Some(bytes + received, size - received);
        if (result < 0) {
            return Fail("%s closed the connection", server.c_str());
        }
    }
    return true;
}

bool Lobby_Client::Send_Message(const Message_Writer& message) {
    std::vector<u8> frame;
    return channel.Seal(message.bytes, frame) && Send_All(frame.data(), frame.size());
}

std::optional<Message_Reader> Lobby_Client::Receive_Message(std::vector<u8>& frame) {
    u32 length;

    frame.resize(gFrameHeaderSize);
    if (!Receive_Exact(frame.data(), gFrameHeaderSize)) {
        return std::nullopt;
    }

    length = Frame_Length(frame.data());
    if (length < gTagSize || length > gMaxFrame + gTagSize) {
        Fail("invalid frame size from %s", server.c_str());
        return std::nullopt;
    }

    frame.resize(gFrameHeaderSize + length);
    if (!Receive_Exact(frame.data() + gFrameHeaderSize, length)) {
        return std::nullopt;
    }

    if (!channel.Open(frame.data(), frame.size())) {
        Fail("frame authentication failed: %s", server.c_str());
        return std::nullopt;
    }

    return Message_Reader(frame.data() + gFrameHeaderSize, length - gTagSize);
}

bool Lobby_Client::Trust_Server(const u8 key[32]) {
    std::string key_text = Hex_Text(key, 32);
    std::string path = Path_Join(Path_Join(dataDirectory, "config"), gKnownServersName);
    File_Handle file(fopen(path.c_str(), "rb"));
    Mutable_Json_Document document;

    if (file != nullptr) {
        Json_Document read(yyjson_read_fp(file.get(), 0, nullptr, nullptr));
        yyjson_val* root = yyjson_doc_get_root(read.get());
        if (!yyjson_is_obj(root)) {
            return Fail("cannot read %s as a JSON object", path.c_str());
        }

        bool pinned = false;
        yyjson_obj_iter iterator = yyjson_obj_iter_with(root);
        while (yyjson_val* name = yyjson_obj_iter_next(&iterator)) {
            if (yyjson_equals_str(name, server.c_str())) {
                if (!yyjson_equals_str(yyjson_obj_iter_get_val(name), key_text.c_str())) {
                    return Fail("server key changed for %s; verify before updating %s", server.c_str(), path.c_str());
                }
                pinned = true;
            }
        }

        if (pinned) {
            return true;
        }
        document.reset(yyjson_doc_mut_copy(read.get(), nullptr));
    }
    else {
        if (errno != ENOENT) {
            return Fail("cannot read %s", path.c_str());
        }

        document.reset(yyjson_mut_doc_new(nullptr));
        yyjson_mut_doc_set_root(document.get(), yyjson_mut_obj(document.get()));
    }
    if (!yyjson_mut_obj_add_strcpy(document.get(), yyjson_mut_doc_get_root(document.get()), server.c_str(), key_text.c_str())) {
        return Fail("cannot store server key for %s", server.c_str());
    }

    file.reset();
    if (!Json_Write_File(path, document.get())) {
        return Fail("cannot write %s", path.c_str());
    }

    return true;
}

bool Lobby_Client::Connect() {
    Client_Hello hello;
    u8 ephemeral_secret[gKeySize];
    Server_Hello reply;
    u8 transcript[64];
    u8 signature[64];
    Member_Id id;
    bool keyed;
    std::vector<u8> frame;
    std::optional<Message_Reader> reader;
    u8 type;

    socket = Socket::Connect(host.c_str(), port, gConnectTimeout);
    if (!socket.Is_Open()) {
        return Fail("cannot reach %s", server.c_str());
    }

    socket.Set_Blocking(false);
    memcpy(hello.magic, gProtocolMagic, sizeof(hello.magic));
    if (!Key_Pair_New(ephemeral_secret, hello.ephemeralKey)) {
        return Fail("connection key generation failed");
    }

    keyed = Send_All(&hello, sizeof(hello)) && Receive_Exact(&reply, sizeof(reply));
    if (keyed && memcmp(reply.magic, gProtocolMagic, sizeof(reply.magic)) != 0) {
        keyed = Fail("incompatible server: %s", server.c_str());
    }

    keyed = keyed && Trust_Server(reply.staticKey);
    if (keyed && !Handshake_Client(ephemeral_secret, hello.ephemeralKey, reply.ephemeralKey, reply.staticKey, channel, transcript)) {
        keyed = Fail("key exchange failed: %s", server.c_str());
    }

    Wipe(ephemeral_secret, sizeof(ephemeral_secret));
    if (!keyed) {
        return false;
    }

    if (!Signing_Key_New(signingKey)) {
        return Fail("signing key generation failed");
    }

    Identity_Prove(identity, transcript, signingKey.publicKey, signature);
    if (!Send_Message(Message_Writer().U8(Protocol_Hello).Bytes(identity.publicKey, 32).Bytes(signingKey.publicKey, 32).Bytes(signature, 64).Text(nickname)) ||
        !(reader = Receive_Message(frame))) {
        return false;
    }

    type = reader->U8();
    if (type == Protocol_Goodbye) {
        return Fail("%s rejected connection: %s", server.c_str(), Reason(*reader).c_str());
    }

    if (type != Protocol_Welcome || !reader->Copy(id) || id != identity.id || !reader->At_End()) {
        return Fail("invalid hello response from %s", server.c_str());
    }

    udp = socket.Udp_Peer();
    if (!udp.Is_Open()) {
        return Fail("cannot open UDP connection to %s", server.c_str());
    }
    datagrams.Initialize(channel);
    return Join_Lobby();
}

bool Lobby_Client::Read_Members(Message_Reader& reader) {
    u16 count = reader.U16();
    Peer peer;
    std::optional<std::string> name;

    for (u16 index = 0; index < count; index++) {
        if (!reader.Copy(peer.member.id) || !(name = reader.Text(gMaxNickname)) || !reader.Copy(peer.signingKey)) {
            return Fail("invalid member list from %s", server.c_str());
        }

        peer.member.nickname = *name;
        if (!Add_Peer(peer)) {
            return Fail("%s sent a duplicate member or signing key", server.c_str());
        }
    }
    return reader.At_End() || Fail("invalid member list from %s", server.c_str());
}

bool Lobby_Client::Join_Lobby() {
    std::vector<u8> frame;
    std::optional<Message_Reader> reader;
    Message_Writer message;
    u8 type;
    u8 salt[gSaltSize];
    u8 created_public[32];
    u8 lobby_public[32];
    u8 sealed[gSealedSecretSize];
    Lobby_Keys keys;
    bool creating;
    bool answered;
    u32 others;

    for (u32 attempt = 0; attempt < 3 && !stop; attempt++) {
        if (!Send_Message(Message_Writer().U8(Protocol_Lobby_Query).Text(lobby)) || !(reader = Receive_Message(frame))) {
            return false;
        }

        type = reader->U8();
        creating = reader->U8() == 0;
        if (type != Protocol_Lobby_Info || !reader->Copy(salt, sizeof(salt))) {
            return Fail("invalid lobby response from %s", server.c_str());
        }

        if (creating && (!Random_Bytes(salt, sizeof(salt)) || !Key_Pair_New(lobbySecret, created_public))) {
            return Fail("lobby key generation failed");
        }

        if (!Lobby_Derive_Keys(password, salt, keys)) {
            return Fail("cannot allocate 64 MiB for lobby keys");
        }

        if (creating && !Lobby_Seal_Secret(keys, lobby, created_public, lobbySecret, sealed)) {
            Wipe(&keys, sizeof(keys));
            return Fail("lobby key generation failed");
        }

        message = Message_Writer().U8(creating ? Protocol_Lobby_Create : Protocol_Lobby_Join).Text(lobby);
        if (creating) {
            message.Bytes(salt, sizeof(salt)).Bytes(keys.authKey, 32).Bytes(created_public, 32).Bytes(sealed, sizeof(sealed));
        }
        else {
            message.Bytes(keys.authKey, 32);
        }

        answered = Send_Message(message) && (reader = Receive_Message(frame));
        Wipe(message.bytes.data(), message.bytes.size());
        type = answered ? reader->U8() : 0;
        if (type == Protocol_Lobby_Joined && (!reader->Copy(lobby_public, 32) || !reader->Copy(sealed, sizeof(sealed)))) {
            Fail("invalid lobby data from %s", server.c_str());
        }
        else if (type == Protocol_Lobby_Joined && creating && memcmp(lobby_public, created_public, 32) != 0) {
            Fail("%s changed the lobby key", server.c_str());
        }
        else if (type == Protocol_Lobby_Joined && !creating && !Lobby_Open_Secret(keys, lobby, lobby_public, sealed, lobbySecret)) {
            Fail("lobby %s key authentication failed: %s", lobby.c_str(), server.c_str());
        }
        else if (type == Protocol_Lobby_Joined && Read_Members(*reader)) {
            {
                std::lock_guard guard(lock);

                memcpy(lobbyPublic, lobby_public, 32);
                state = Lobby_State::Joined;
                others = static_cast<u32>(peers.size());
            }

            Wipe(&keys, sizeof(keys));
            Log_Info(
                "network",
                "%s lobby %s on %s (%u peers)",
                creating ? "created" : "joined",
                lobby.c_str(),
                server.c_str(),
                others
            );
            return true;
        }
        else if (type == Protocol_Lobby_Refused && reader->U8() == Protocol_Refusal_Password) {
            Fail("wrong password for lobby %s", lobby.c_str());
        }
        else if (type == Protocol_Goodbye) {
            Fail("%s closed the connection: %s", server.c_str(), Reason(*reader).c_str());
        }
        else if (answered && type != Protocol_Lobby_Refused && type != Protocol_Lobby_Joined) {
            Fail("unexpected message %u from %s", type, server.c_str());
        }

        Wipe(&keys, sizeof(keys));
        if (!answered || !failure.empty()) {
            return false;
        }
    }

    return failure.empty() ? Fail("could not join lobby %s", lobby.c_str()) : false;
}

bool Lobby_Client::Queue_Outgoing() {
    std::deque<Outgoing> queued;
    Message_Writer message;
    {
        std::lock_guard guard(lock);

        queued.swap(outgoing);
        outgoingBytes = 0;
    }

    for (const Outgoing& item : queued) {
        message = Message_Writer().U8(item.toServer ? Protocol_Server_Send : item.member ? Protocol_Lobby_Send_To : Protocol_Lobby_Send);
        if (item.member) {
            message.Bytes(*item.member);
        }

        message.U8(item.toServer && item.sealed ? gProtocolSealed : 0).Text(item.topic);
        if (item.sealed) {
            if (!Lobby_Seal_Message(signingKey, identity.id, lobbyPublic, item.topic, item.data, message.bytes)) {
                return Fail("cannot seal outgoing message");
            }
        }
        else {
            message.Bytes(item.data);
        }
        
        if (item.transport == Network_Transport::Udp) {
            Send_Datagram(message);
        }
        else if (!channel.Seal(message.bytes, output)) {
            return Fail("cannot seal outgoing frame");
        }
    }
    return true;
}

bool Lobby_Client::Handle_Message(Message_Reader& reader, Network_Transport transport) {
    u8 type = reader.U8();
    Lobby_Member member;
    std::optional<std::string> text;
    std::optional<Lobby_Member> known;
    std::span<u8> data;
    std::span<const u8> payload;
    Member_Id signer;
    std::array<u8, 32> signing_key;
    bool sealed;

    if (transport == Network_Transport::Udp && type != Protocol_Lobby_Message && type != Protocol_Server_Message) {
        return true;
    }

    if (type == Protocol_Member_Joined) {
        if (!reader.Copy(member.id) || !(text = reader.Text(gMaxNickname)) || !reader.Copy(signing_key) || !reader.At_End()) {
            return Fail("invalid member data from %s", server.c_str());
        }

        member.nickname = *text;
        if (!Add_Peer({member, signing_key})) {
            return Fail("%s sent a duplicate member or signing key", server.c_str());
        }
        Log_Info("network", "%s (%s) joined %s", member.nickname.c_str(), Client_Id_Text(member.id).c_str(), lobby.c_str());
        Push_Event(Lobby_Event_Kind::Member_Joined, member);
        return true;
    }
    if (type == Protocol_Member_Left) {
        if (!reader.Copy(member.id) || !reader.At_End()) {
            return Fail("invalid member data from %s", server.c_str());
        }

        known = Find_Member(member.id);
        if (known) {
            {
                std::lock_guard guard(lock);

                std::erase_if(peers, [&](const Peer& other) {
                    return other.member.id == member.id;
                });
            }
            Log_Info("network", "%s (%s) left %s", known->nickname.c_str(), Client_Id_Text(known->id).c_str(), lobby.c_str());
            Push_Event(Lobby_Event_Kind::Member_Left, *known);
        }

        return true;
    }
    if (type == Protocol_Lobby_Message) {
        reader.Copy(member.id);
        reader.U8(); // flags
        text = reader.Text(gMaxTopic);
        if (!text) {
            return Fail("invalid message from %s", server.c_str());
        }

        data = reader.Rest();
        if (!Lobby_Open_Message(lobbySecret, lobbyPublic, *text, data, payload, signer, signing_key)) {
            Log_Warning("network", "dropped %s: message authentication failed", text->c_str());
            return true;
        }

        if (signer != member.id || !(known = Find_Signer(signer, signing_key))) {
            Log_Warning("network", "dropped %s: sender or signing key mismatch", text->c_str());
            return true;
        }

        Push_Event(Lobby_Event_Kind::Message, *known, *text, payload, false, transport);
        return true;
    }
    if (type == Protocol_Server_Message) {
        sealed = (reader.U8() & gProtocolSealed) != 0;
        text = reader.Text(gMaxTopic);
        if (!text) {
            return Fail("invalid message from %s", server.c_str());
        }

        data = reader.Rest();
        payload = data;
        if (sealed && (!Lobby_Open_Message(lobbySecret, lobbyPublic, *text, data, payload, signer, signing_key) ||
            !(known = Find_Signer(signer, signing_key)))) {
            Log_Warning("network", "dropped server message %s: authentication failed", text->c_str());
            return true;
        }

        if (sealed) {
            member = *known;
        }

        Push_Event(Lobby_Event_Kind::Server_Message, member, *text, payload, sealed, transport);
        return true;
    }

    if (type == Protocol_Goodbye) {
        return Fail("%s closed the connection: %s", server.c_str(), Reason(reader).c_str());
    }
    return Fail("unexpected message %u from %s", type, server.c_str());
}

bool Lobby_Client::Handle_Input() {
    u64 offset = 0;
    u32 length;

    while (input.size() - offset >= gFrameHeaderSize) {
        length = Frame_Length(input.data() + offset);
        if (length < gTagSize || length > gMaxFrame + gTagSize) {
            return Fail("invalid frame size from %s", server.c_str());
        }

        if (input.size() - offset < gFrameHeaderSize + length) {
            break;
        }

        if (!channel.Open(input.data() + offset, gFrameHeaderSize + length)) {
            return Fail("frame authentication failed: %s", server.c_str());
        }

        Message_Reader reader(input.data() + offset + gFrameHeaderSize, length - gTagSize);
        if (!Handle_Message(reader)) {
            return false;
        }

        offset += gFrameHeaderSize + length;
    }

    input.erase(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}

void Lobby_Client::Send_Datagram(const Message_Writer& message) {
    std::vector<u8> packet;
    if (datagrams.Seal(identity.id, message.bytes, packet)) {
        udp.Send_Some(packet.data(), packet.size());
    }
}

bool Lobby_Client::Handle_Datagrams() {
    u8 buffer[gMaxDatagram + 1];
    for (u32 index = 0; index < gDatagramsPerPoll && !stop; index++) {
        s64 count = udp.Receive_Some(buffer, sizeof(buffer));
        if (count <= 0) {
            break;
        }
        if (static_cast<u64>(count) < gDatagramOverhead) {
            continue;
        }
        Datagram_Header header;
        memcpy(&header, buffer, sizeof(header));
        if (header.client != identity.id || !datagrams.Open({buffer, static_cast<usize>(count)})) {
            continue;
        }
        Message_Reader reader(buffer + sizeof(header), static_cast<usize>(count) - gDatagramOverhead);
        if (!Handle_Message(reader, Network_Transport::Udp)) {
            return false;
        }
    }
    return true;
}

bool Lobby_Client::Pump() {
    Socket_Poll_Entry entries[2];
    s64 moved;
    usize received;
    u64 next_probe = 0;

    while (!stop) {
        u64 now = Time_Monotonic_Milliseconds();
        if (now >= next_probe) {
            // Keep the authenticated UDP return address reachable through NAT.
            Send_Datagram(Message_Writer().U8(Protocol_Datagram_Ready));
            next_probe = now + gDatagramProbeInterval;
        }
        if (!Queue_Outgoing()) {
            return false;
        }

        entries[0] = { socket.Native(), outputSent < output.size(), false, false, false };
        entries[1] = { udp.Native(), false, false, false, false };
        if (Socket_Poll(entries, gPollInterval) < 0) {
            return Fail("socket poll failed");
        }

        if (entries[0].writable && outputSent < output.size()) {
            moved = socket.Send_Some(output.data() + outputSent, output.size() - outputSent);
            if (moved < 0) {
                return Fail("connection lost: %s", server.c_str());
            }

            outputSent += static_cast<u64>(moved);
            if (outputSent == output.size()) {
                output.clear();
                outputSent = 0;
            }
        }

        if (entries[0].readable || entries[0].failed) {
            received = input.size();
            input.resize(received + gReceiveChunk);
            moved = socket.Receive_Some(input.data() + received, gReceiveChunk);
            input.resize(received + static_cast<usize>(moved > 0 ? moved : 0));
            if (moved < 0) {
                return Fail("connection lost: %s", server.c_str());
            }

            if (!Handle_Input()) {
                return false;
            }
        }
        if ((entries[1].readable || entries[1].failed) && !Handle_Datagrams()) {
            return false;
        }
    }

    return true;
}

void Lobby_Client::Main() {
    if (Connect()) {
        Push_Event(Lobby_Event_Kind::Joined, Self());
        for (const Peer& peer : peers) {
            Push_Event(Lobby_Event_Kind::Member_Joined, peer.member);
        }
        Pump();
    }

    socket.Close();
    udp.Close();
    Wipe(&channel, sizeof(channel));
    Wipe(&datagrams, sizeof(datagrams));
    {
        std::lock_guard guard(lock);

        state = failure.empty() ? Lobby_State::Disconnected : Lobby_State::Failed;
        peers.clear();
        Wipe(lobbyPublic, sizeof(lobbyPublic));
    }
    
    if (!failure.empty()) {
        Log_Warning("network", "%s", failure.c_str());
        Push_Event(Lobby_Event_Kind::Left);
    }
}
