using Network;
using Network.Enums;
using Network.Packets;
using System.Text.Json.Serialization;

namespace ModLoader.API;

public static class NetworkEvents
{
    // Client
    public const string CLIENT_ON_NETWORK_CONNECT = "CLIENT_ON_NETWORK_CONNECT";
    public const string CLIENT_ON_NETWORK_DISCONNECT = "CLIENT_ON_NETWORK_DISCONNECT";
    public const string CLIENT_ON_NETWORK_LOBBY_JOIN = "CLIENT_ON_NETWORK_LOBBY_JOIN";
    public const string CLIENT_ON_NETWORK_LOBBY_DISCONNECT = "CLIENT_ON_NETWORK_LOBBY_DISCONNECT";
    // Server
    public const string SERVER_ON_NETWORK_CONNECT = "SERVER_ON_NETWORK_CONNECT";
    public const string SERVER_ON_NETWORK_DISCONNECT = "SERVER_ON_NETWORK_DISCONNECT";
    public const string SERVER_ON_NETWORK_LOBBY_CREATED = "SERVER_ON_NETWORK_LOBBY_CREATED";
    public const string SERVER_ON_NETWORK_LOBBY_JOIN = "SERVER_ON_NETWORK_LOBBY_JOIN";
    public const string SERVER_ON_NETWORK_LOBBY_DISCONNECT = "SERVER_ON_NETWORK_LOBBY_DISCONNECT";
}

public class EventClientNetworkConnection : IEvent
{
    public string Id { get; set; } = NetworkEvents.CLIENT_ON_NETWORK_CONNECT;
    public readonly Connection connection;
    public readonly ConnectionType type;

    public EventClientNetworkConnection(Connection connection, ConnectionType type)
    {
        this.connection = connection;
        this.type = type;
    }
}

public class EventClientNetworkDisconnect : IEvent
{
    public string Id { get; set; } = NetworkEvents.CLIENT_ON_NETWORK_DISCONNECT;
    public readonly Connection connection;
    public readonly ConnectionType type;
    public readonly CloseReason reason;

    public EventClientNetworkDisconnect(Connection connection, ConnectionType type, CloseReason reason)
    {
        this.connection = connection;
        this.type = type;
        this.reason = reason;
    }
}

public class EventClientNetworkLobbyJoined : IEvent
{
    public readonly string lobby;
    public readonly u8[] patch;
    public string Id { get; set; } = NetworkEvents.CLIENT_ON_NETWORK_LOBBY_JOIN;

    public EventClientNetworkLobbyJoined(string lobby, byte[] patch)
    {
        this.lobby = lobby;
        this.patch = patch;
    }
}

public class EventClientNetworkLobbyDisconnect : IEvent
{
    public readonly string lobby;
    public readonly u8[] patch;
    public string Id { get; set; } = NetworkEvents.CLIENT_ON_NETWORK_LOBBY_DISCONNECT;

    public EventClientNetworkLobbyDisconnect(string lobby, byte[] patch)
    {
        this.lobby = lobby;
        this.patch = patch;
    }
}


public class EventServerNetworkConnection : IEvent
{
    public string Id { get; set; } = NetworkEvents.SERVER_ON_NETWORK_CONNECT;
    public readonly Connection connection;
    public readonly ConnectionType type;

    public EventServerNetworkConnection(Connection connection, ConnectionType type)
    {
        this.connection = connection;
        this.type = type;
    }
}

public class EventServerNetworkDisconnect : IEvent
{
    public string Id { get; set; } = NetworkEvents.SERVER_ON_NETWORK_DISCONNECT;
    public readonly Connection connection;
    public readonly ConnectionType type;
    public readonly CloseReason reason;

    public EventServerNetworkDisconnect(Connection connection, ConnectionType type, CloseReason reason)
    {
        this.connection = connection;
        this.type = type;
        this.reason = reason;
    }
}

public class EventServerNetworkLobbyJoined : IEvent
{
    public readonly string lobby;
    public readonly NetworkPlayer player;
    public readonly u8[] patch;
    public string Id { get; set; } = NetworkEvents.SERVER_ON_NETWORK_LOBBY_JOIN;

    public EventServerNetworkLobbyJoined(string lobby, NetworkPlayer player, byte[] patch)
    {
        this.lobby = lobby;
        this.player = player;
        this.patch = patch;
    }
}

public class EventServerNetworkLobbyDisconnect: IEvent
{
    public readonly string lobby;
    public readonly NetworkPlayer player;
    public string Id { get; set; } = NetworkEvents.SERVER_ON_NETWORK_LOBBY_DISCONNECT;

    public EventServerNetworkLobbyDisconnect(string lobby, NetworkPlayer player)
    {
        this.lobby = lobby;
        this.player = player;
    }
}


public class EventServerNetworkLobbyCreated : IEvent
{
    public readonly string lobby;
    public string Id { get; set; } = NetworkEvents.SERVER_ON_NETWORK_LOBBY_CREATED;

    public EventServerNetworkLobbyCreated(string lobby)
    {
        this.lobby = lobby;
    }
}

public class NetworkPlayer
{
    public readonly string uuid;
    public readonly string nickname;
    public dynamic data { get; set; }

    public NetworkPlayer(string uuid, string nickname)
    {
        this.uuid = uuid;
        this.nickname = nickname;
    }
}

public class Lobby
{
    public readonly string name;
    public readonly List<NetworkPlayer> players;
    public byte[] patch;

    public Lobby(string name, byte[] patch)
    {
        this.name = name;
        this.players = new List<NetworkPlayer>();
        this.patch = patch;
    }
}

[AttributeUsage(AttributeTargets.Method)]
public class ClientNetworkHandlerAttribute : Attribute, IEvent
{
    public string Id { get; set; }
    public Type type { get; set; }

    public ClientNetworkHandlerAttribute(Type type)
    {
        this.type = type;
        Id = type.Name;
    }
}

[AttributeUsage(AttributeTargets.Method)]
public class ServerNetworkHandlerAttribute : Attribute, IEvent
{
    public string Id { get; set; }
    public Type type { get; set; }

    public ServerNetworkHandlerAttribute(Type type)
    {
        this.type = type;
        this.Id = type.Name;
    }
}

public interface INetworkingSender
{
    public void SendPacket<T>(T packet, string lobbytarget);
    public void SendPacketToSpecificPlayer<T>(T packet, NetworkPlayer player, string lobbytarget);
}

public static class NetworkSenders
{
    public static INetworkingSender Client;
    public static INetworkingSender Server;
}

public static class NetworkClientData
{
    public static string lobby;
    public static NetworkPlayer me;
}