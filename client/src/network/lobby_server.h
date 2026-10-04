#pragma once

#include "base.h"
#include "modloader_lobby.h"
#include "network/protocol.h"
#include "network/socket.h"

#include <atomic>
#include <deque>
#include <memory>

enum class Server_Event_Kind : u32 {
    Lobby_Opened = MODLOADER_SERVER_LOBBY_OPENED,
    Lobby_Closed = MODLOADER_SERVER_LOBBY_CLOSED,
    Member_Joined = MODLOADER_SERVER_MEMBER_JOINED,
    Member_Left = MODLOADER_SERVER_MEMBER_LEFT,
    Message = MODLOADER_SERVER_MESSAGE,
};

struct Server_Event {
    Server_Event_Kind kind;
    u32 lobby;
    std::string lobbyName;
    Lobby_Member member;
    std::string topic;
    std::vector<u8> payload;
    bool sealed = false;
    std::array<u8, 32> publicKey = {};
    Network_Transport transport = Network_Transport::Tcp;
};

class Lobby_Server {
public:
    struct Config {
        u16 port;
        std::string address; // empty: every interface
        std::string dataDirectory; // server.key
    };

    static std::unique_ptr<Lobby_Server> Start(const Config& config);
    Lobby_Server(const Lobby_Server&) = delete;
    Lobby_Server& operator=(const Lobby_Server&) = delete;
    ~Lobby_Server();

    std::optional<Server_Event> Next_Event();
    bool Send(u32 lobby, const Member_Id* member, const Member_Id* except, std::string_view topic, std::span<const u8> data, bool sealed, Network_Transport transport);

private:
    struct Connection;
    struct Lobby;

    struct Outgoing {
        u32 lobby;
        std::optional<Member_Id> member;
        std::optional<Member_Id> except;
        std::string topic;
        std::vector<u8> data;
        bool sealed;
        Network_Transport transport;
    };

    Lobby_Server() = default;
    void Main();
    bool Load_Key(const std::string& data_directory);
    void Push_Event(
        Server_Event_Kind kind,
        const Lobby& lobby,
        const Connection* connection = nullptr,
        std::string_view topic = {},
        std::span<const u8> payload = {},
        bool sealed = false,
        Network_Transport transport = Network_Transport::Tcp
    );
    Lobby* Find_Lobby(std::string_view name) const;
    Lobby* Lobby_Of(u32 id) const;
    void Enter_Lobby(Lobby& lobby, Connection& connection);
    void Leave_Lobby(Connection& connection);
    void Handle_Hello(Connection& connection);
    void Handle_Identity(Connection& connection, Message_Reader& reader);
    void Handle_Lobby_Request(Connection& connection, u8 type, Message_Reader& reader);
    void Handle_Ready(Connection& connection, Message_Reader& reader, Network_Transport transport = Network_Transport::Tcp);
    void Handle_Input(Connection& connection);
    void Send_Message(Connection& connection, const Message_Writer& message, Network_Transport transport);
    void Handle_Datagrams();
    void Send_Outgoing();
    void Accept_Connections();
    void Close_Connection(usize index);

    Socket listener;
    Socket udp;
    u8 staticSecret[32] = {};
    u8 staticPublic[32] = {};
    Thread thread;
    std::atomic<bool> stop = false;
    std::vector<std::unique_ptr<Connection>> connections;
    std::vector<std::unique_ptr<Lobby>> lobbies;
    u32 nextLobbyId = 0;
    std::mutex lock;
    std::deque<Server_Event> events;
    std::deque<Outgoing> outgoing;
};
