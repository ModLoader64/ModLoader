using Network;
using Network.Converter;
using Network.Enums;
using Network.Packets;
using Newtonsoft.Json;
using System.Reflection;

namespace ModLoader.Core;

public class Server : INetworkingSender
{

    private ServerConnectionContainer? container;
    public string serverAddr = "127.0.0.1";
    public int port = 25565;
    public PluginLoader? pluginLoader;
    private Dictionary<string, EventSetupServerNetworkHandler> NetworkHandlerContainers = new Dictionary<string, EventSetupServerNetworkHandler>();
    public string[]? modSigs;
    private JsonSerializerSettings jsonSettings = new JsonSerializerSettings { TypeNameHandling = TypeNameHandling.Auto };

    public static Dictionary<string, Lobby> lobbies = new Dictionary<string, Lobby>();

    public void PreStartServer()
    {
        foreach (MethodInfo m in GetType().GetRuntimeMethods())
        {
            if (m.Name == "OnNetworkSetupEvent")
            {
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
    }

    public void StartServer(PluginLoader? pluginLoader)
    {

        Console.WriteLine("Starting server...");

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
        if (DebugFlags.IsDebugEnabled)
        {
            Console.WriteLine($"[SERVER]: Setting up {e.attr.Id} {e.ModId}");
        }
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
        var b1 = JsonConvert.DeserializeObject(e.payload, jsonSettings)!;
        var type = b1.GetType();
        if (DebugFlags.IsDebugEnabled)
        {
            Console.WriteLine($"[SERVER]: Got packet {type.Name}");
        }
        if (NetworkHandlerContainers.TryGetValue(type.Name, out EventSetupServerNetworkHandler? value))
        {
            if (value != null) {
                if (DebugFlags.IsDebugEnabled)
                {
                    Console.WriteLine($"[SERVER]: Invoking packet handler for {type.Name}.");
                }
                value.method.Invoke(null, new object[] { b1 });
            }
        }
        else
        {
            if (DebugFlags.IsDebugEnabled)
            {
                Console.WriteLine($"[SERVER]: No packet handler found for {type.Name}.");
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
            var evt = new EventServerNetworkConnection(connection, type);
            PubEventBus.bus.PushEvent(evt);
        }
    }

    private void OnClientJoinDataResp(PacketClientJoinDataResp packet, Connection connection)
    {
        NetworkPlayer player = new NetworkPlayer(connection, Guid.NewGuid().ToString(), packet.nickname);
        if (!lobbies.Keys.Contains(packet.lobby))
        {
            // Lobby does not exist.
            var lobby = new Lobby(packet.lobby, packet.patch);
            lobby.players.Add(player);
            lobbies.Add(lobby.name, lobby);
            var e = new EventServerNetworkLobbyCreated(packet.lobby);
            PubEventBus.bus.PushEvent(e);
        }
        else
        {
            lobbies.TryGetValue(packet.lobby, out var lobby);
            if (lobby == null) return;
            lobby.players.Add(player);
        }
        connection.Send(new PacketServerLobbyJoinedResp(packet.lobby, player, lobbies[packet.lobby].patch, true));
        var evt = new EventServerNetworkLobbyJoined(packet.lobby, player, packet.patch);
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
        var settings = new JsonSerializerSettings { TypeNameHandling = TypeNameHandling.Auto };
        var str = JsonConvert.SerializeObject(packet, typeof(object), settings);
        var raw = RawDataConverter.FromASCIIString("ModData", str);
        Console.WriteLine($"[SERVER]: {packet} {str}");
        foreach (var player in lobby.players)
        {
            if (player != null)
            {
                player.connection.SendRawData(raw);
            }
        }
    }

    public void SendPacketToSpecificPlayer<T>(T packet, NetworkPlayer dest, string lobbytarget)
    {
        if (packet == null) return;
        Lobby lobby;
        lobbies.TryGetValue(lobbytarget, out lobby);
        if (lobby == null) return;
        var settings = new JsonSerializerSettings { TypeNameHandling = TypeNameHandling.Auto };
        var str = JsonConvert.SerializeObject(packet, typeof(object), settings);
        var raw = RawDataConverter.FromASCIIString("ModData", str);
        if (DebugFlags.IsDebugEnabled)
        {
            Console.WriteLine($"[SERVER]: {dest.nickname} | {packet} {str}");
        }
        foreach (var player in lobby.players)
        {
            if (player == dest)
            {
                player.connection.SendRawData(raw);
            }
        }
    }
}
