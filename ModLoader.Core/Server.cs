using ModLoader.API;
using ModLoader.API.EventBus;
using Network;
using Network.Enums;
using System.Reflection;

namespace ModLoader.Core;

public class Server
{

    private ServerConnectionContainer? container;
    public string serverAddr = "127.0.0.1";
    public int port = 25565;
    public PluginLoader? pluginLoader;
    public string[]? modSigs;

    public Dictionary<string, Lobby> lobbies = new Dictionary<string, Lobby>();

    public void StartServer(PluginLoader? pluginLoader)
    {

        this.pluginLoader = pluginLoader;
        modSigs = this.pluginLoader!.GetModIdentifiers();
        container = ConnectionFactory.CreateServerConnectionContainer(port, false);
        container.ConnectionEstablished += ConnectionEstablished;
        container.ConnectionLost += ConnectionTerminated;
        container.AllowUDPConnections = true;
        container.UDPConnectionLimit = 2;

        container!.Start();
    }

    public void StopServer() {
        container!.Stop();
    }

    private void ConnectionTerminated(Connection connection, ConnectionType type, CloseReason reason)
    {
        var evt = new EventServerNetworkDisconnect(connection, type, reason);
        PubEventBus.bus.PushEvent(evt);
        List<string> deadLobbies = new List<string>();
        foreach (var lobby in lobbies)
        {
            if (lobby.Value.players.Count == 0)
            {
                deadLobbies.Add(lobby.Value.name);
            }
            else
            {
                bool areAllRemainingPlayersDead = true;
                List<NetworkPlayer> needsRemoved = new List<NetworkPlayer>();
                foreach (var player in lobby.Value.players) { 
                    if (ReferenceEquals(player.connection, connection))
                    {
                        needsRemoved.Add(player);
                    }
                    if (player.connection.IsAlive)
                    {
                        areAllRemainingPlayersDead = false;
                    }
                }
                if (needsRemoved.Count > 0)
                {
                    lobby.Value.players.Remove(needsRemoved.FirstOrDefault()!);
                }
                if (areAllRemainingPlayersDead)
                {
                    deadLobbies.Add(lobby.Key);
                }
            }
            if (deadLobbies.Count > 0)
            {
                foreach (var death in deadLobbies)
                {
                    lobbies.Remove(death);
                }
            }
        }
    }

    private void ConnectionEstablished(Connection connection, ConnectionType type)
    {
        Console.WriteLine($"{container!.Count} {connection.GetType()} connected on port {connection.IPRemoteEndPoint.Port}");
        if (type == ConnectionType.TCP)
        {
            connection.RegisterPacketHandler<PacketClientHandshake>(OnHandshakePacket, this);
            connection.RegisterPacketHandler<PacketClientJoinDataResp>(OnClientJoinDataResp, this);
        }
        else
        {
            var evt = new EventServerNetworkConnection(connection, type);
            PubEventBus.bus.PushEvent(evt);
        }
    }

    private void OnClientJoinDataResp(PacketClientJoinDataResp packet, Connection connection)
    {

        if (!lobbies.Keys.Contains(packet.lobby))
        {
            // Lobby does not exist.
            var lobby = new Lobby(packet.lobby, packet.patch);
            lobby.players.Add(new NetworkPlayer(connection, Guid.NewGuid().ToString(), packet.nickname));
            lobbies.Add(lobby.name, lobby);
        }

        connection.Send(new PacketServerLobbyJoinedResp(packet.lobby, lobbies[packet.lobby].patch, true));
        var evt = new EventServerNetworkLobbyJoined(packet.lobby, packet.patch);
        PubEventBus.bus.PushEvent(evt);
    }

    private void OnHandshakePacket(PacketClientHandshake packet, Connection connection)
    {
        bool failed = false;
        foreach (string mod in modSigs!)
        {
            if (!packet.mods.Contains(mod))
            {
                failed = true;
            }
        }
        connection.Send(new PacketServerHandshakeResp($"{Assembly.GetExecutingAssembly()!.GetName()!.Version!}", modSigs, !failed));
        connection.Send(new PacketServerRequestJoinData());
        if (!failed)
        {
            var evt = new EventServerNetworkConnection(connection, ConnectionType.TCP);
            PubEventBus.bus.PushEvent(evt);
        }
    }
}
