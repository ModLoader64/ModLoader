#pragma once

#include "base.h"
#include "modloader_lobby.h"
#include "network/protocol.h"
#include "network/socket.h"

#include <atomic>
#include <deque>
#include <memory>

enum class Lobby_State : u32 {
    Disconnected = MODLOADER_LOBBY_STATUS_DISCONNECTED,
    Connecting = MODLOADER_LOBBY_STATUS_CONNECTING,
    Joined = MODLOADER_LOBBY_STATUS_JOINED,
    Failed = MODLOADER_LOBBY_STATUS_FAILED,
};

enum class Lobby_Event_Kind : u32 {
    Joined = MODLOADER_LOBBY_JOINED,
    Member_Joined = MODLOADER_LOBBY_MEMBER_JOINED,
    Member_Left = MODLOADER_LOBBY_MEMBER_LEFT,
    Message = MODLOADER_LOBBY_MESSAGE,
    Left = MODLOADER_LOBBY_LEFT,
    Server_Message = MODLOADER_LOBBY_SERVER_MESSAGE,
};

struct Lobby_Event {
    Lobby_Event_Kind kind;
    Lobby_Member member;
    std::string topic;
    std::vector<u8> payload;
    bool sealed = false;
    Network_Transport transport = Network_Transport::Tcp;
};

class Lobby_Client {
public:
    struct Config {
        std::string address;
        u16 port = 0;
        std::string nickname;
        std::string lobby;
        std::string password;
        std::string dataDirectory;
    };

    static std::unique_ptr<Lobby_Client> Start(const Config& config);
    Lobby_Client(const Lobby_Client&) = delete;
    Lobby_Client& operator=(const Lobby_Client&) = delete;
    ~Lobby_Client();

    Lobby_State State() const;
    Lobby_Member Self() const;

    const std::string& Lobby_Name() const {
        return lobby;
    }

    void Lobby_Key(u8 out_key[32]) const;
    u32 Member_Count() const;
    std::optional<Lobby_Member> Member_At(u32 index) const;

    bool Send(std::string_view topic, std::span<const u8> data, Network_Transport transport);
    bool Send_To(const Member_Id& member, std::string_view topic, std::span<const u8> data, Network_Transport transport);
    bool Send_Server(std::string_view topic, std::span<const u8> data, bool sealed, Network_Transport transport);
    std::optional<Lobby_Event> Next_Event();

private:
    struct Peer {
        Lobby_Member member;
        std::array<u8, 32> signingKey;
    };

    struct Outgoing {
        std::string topic;
        std::vector<u8> data;
        bool toServer;
        bool sealed;
        std::optional<Member_Id> member;
        Network_Transport transport;
    };

    Lobby_Client() = default;
    void Main();
    bool Fail(const char* format, ...) __attribute__((format(printf, 2, 3)));
    void Push_Event(Lobby_Event_Kind kind, const Lobby_Member& member = {}, std::string_view topic = {}, std::span<const u8> payload = {}, bool sealed = false, Network_Transport transport = Network_Transport::Tcp);
    std::optional<Lobby_Member> Find_Member(const Member_Id& id) const;
    std::optional<Lobby_Member> Find_Signer(const Member_Id& id, const std::array<u8, 32>& signing_key) const;
    bool Add_Peer(const Peer& peer);
    bool Queue(std::string_view topic, std::span<const u8> data, bool to_server, bool sealed, Network_Transport transport, const Member_Id* member = nullptr);
    bool Wait_Ready(bool for_write, u32 timeout_milliseconds);
    bool Send_All(const void* data, u64 size);
    bool Receive_Exact(void* out, u64 size);
    bool Send_Message(const Message_Writer& message);
    std::optional<Message_Reader> Receive_Message(std::vector<u8>& frame);
    bool Trust_Server(const u8 key[32]);
    bool Connect();
    bool Join_Lobby();
    bool Read_Members(Message_Reader& reader);
    bool Queue_Outgoing();
    bool Handle_Message(Message_Reader& reader, Network_Transport transport = Network_Transport::Tcp);
    void Send_Datagram(const Message_Writer& message);
    bool Handle_Datagrams();
    bool Handle_Input();
    bool Pump();

    std::string server; // host:port key in known_servers.json
    std::string host;
    u16 port = gDefaultPort;
    std::string nickname;
    std::string lobby;
    std::string password;
    std::string dataDirectory;
    Identity identity = {};
    Signing_Key signingKey = {};
    Thread thread;
    std::atomic<bool> stop = false;
    mutable std::mutex lock;
    Lobby_State state = Lobby_State::Connecting;
    u8 lobbyPublic[32] = {};
    std::vector<Peer> peers; // only the connection thread writes
    std::deque<Outgoing> outgoing;
    u64 outgoingBytes = 0;
    std::deque<Lobby_Event> events;
    Socket socket;
    Secure_Channel channel = {};
    Socket udp;
    Datagram_Channel datagrams;
    u8 lobbySecret[32] = {};
    std::vector<u8> input;
    std::vector<u8> output;
    u64 outputSent = 0;
    std::string failure;
};
