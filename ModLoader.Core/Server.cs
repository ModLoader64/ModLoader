using Network;
using Network.Enums;
using System.Reflection;
using System.Text.Json;

namespace ModLoader.Core;

public class Server : INetworkingSender
{

    private ServerConnectionContainer? container;
    public string serverAddr = "127.0.0.1";
    public int port = 25565;
    public PluginLoader? pluginLoader;
    private Dictionary<string, EventSetupServerNetworkHandler> NetworkHandlerContainers = new Dictionary<string, EventSetupServerNetworkHandler>();
    public string[]? modSigs;

    public static Dictionary<string, Lobby> lobbies = new Dictionary<string, Lobby>();

    public void StartServer(PluginLoader? pluginLoader)
    {

        foreach (MethodInfo m in GetType().GetRuntimeMethods())
        {
            if (m.Name == "OnNetworkSetupEvent")
            {
                Console.WriteLine("SERVER FUCKING FUCK");
                InternalEventBus.bus.RegisterEventHandler(new EventRegistration("EventSetupServerNetworkHandler", DelegateHelper.CreateDelegate(m, this), ""));
            }
            else if (m.Name == "OnNetworkDisposeEvent")
            {
                InternalEventBus.bus.RegisterEventHandler(new EventRegistration("EventDisposeModNetworkHandlers", DelegateHelper.CreateDelegate(m, this), ""));
            }
            else if (m.Name == "OnPacketDecoded")
            {
                InternalEventBus.server.RegisterEventHandler(new EventRegistration("EventDecodedPacket", DelegateHelper.CreateDelegate(m, this), ""));
            }
        }

        this.pluginLoader = pluginLoader;
        modSigs = this.pluginLoader!.GetModIdentifiers();
        container = ConnectionFactory.CreateServerConnectionContainer(port, false);
        container.ConnectionEstablished += ConnectionEstablished;
        container.ConnectionLost += ConnectionTerminated;
        container.AllowUDPConnections = true;
        container.UDPConnectionLimit = 2;

        NetworkSenders.Server = this;

        container!.Start();
    }

    public void StopServer() {
        container!.Stop();
    }

    private void OnNetworkSetupEvent(EventSetupServerNetworkHandler e)
    {
        NetworkHandlerContainers.Add(e.attr.Id, e);
    }

    private void OnNetworkDisposeEvent(EventDisposeModNetworkHandlers e)
    {
        List<string> disposees = new List<string>();
        foreach (var pair in NetworkHandlerContainers)
        {
            if (pair.Value.ModId == e.ModId)
            {
                disposees.Add(pair.Key);
            }
        }
        foreach (var dispose in disposees)
        {
            NetworkHandlerContainers.Remove(dispose);
        }
    }

    private void OnPacketDecoded(EventDecodedPacket e)
    {
        var settings = new Newtonsoft.Json.JsonSerializerSettings { TypeNameHandling = Newtonsoft.Json.TypeNameHandling.Auto };
        var b1 = Newtonsoft.Json.JsonConvert.DeserializeObject(e.payload, settings)!;
        var type = b1.GetType();
        if (NetworkHandlerContainers.TryGetValue(type.Name, out EventSetupServerNetworkHandler? value))
        {
            if (value != null) {
                value.method.Invoke(null, new object[] { b1 });
            }
        }
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
            connection.RegisterRawDataHandler("ModData", DefaultModPacketHandler.OnServerModPacket);
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

    public void SendPacket<T>(T packet, string lobbytarget)
    {
        if (packet == null) return;
        Lobby lobby;
        lobbies.TryGetValue(lobbytarget, out lobby);
        if (lobby == null) return;
        foreach(var player in lobby.players)
        {
            if (player != null)
            {
                
            }
        }
    }
}
