﻿using Network;
using Network.Converter;
using Network.Enums;
using Newtonsoft.Json;
using System.Numerics;
using System.Reflection;

namespace ModLoader.Core;

public class Client : INetworkingSender
{
    public PluginLoader? pluginLoader;
    private ClientConnectionContainer? container;
    private Dictionary<string, EventSetupClientNetworkHandler> NetworkHandlerContainers = new Dictionary<string, EventSetupClientNetworkHandler>();
    private JsonSerializerSettings jsonSettings =  new JsonSerializerSettings { TypeNameHandling = TypeNameHandling.Auto };
    public bool ReadyToOpenGame = false;

    public void PreStartClient()
    {
        foreach (MethodInfo m in GetType().GetRuntimeMethods())
        {
            if (m.Name == "OnNetworkSetupEvent")
            {
                InternalEventBus.bus.RegisterEventHandler(new EventRegistration("EventSetupClientNetworkHandler", DelegateHelper.CreateDelegate(m, this), ""));
            }
            else if (m.Name == "OnNetworkDisposeEvent")
            {
                InternalEventBus.bus.RegisterEventHandler(new EventRegistration("EventDisposeModNetworkHandlers", DelegateHelper.CreateDelegate(m, this), ""));
            }
            else if (m.Name == "OnPacketDecoded")
            {
                InternalEventBus.client.RegisterEventHandler(new EventRegistration("EventDecodedPacket", DelegateHelper.CreateDelegate(m, this), ""));
            }
        }
    }

    public void StartClient(PluginLoader? pluginLoader, string address, int port)
    {

        Console.WriteLine("Starting client...");

        this.pluginLoader = pluginLoader;

        container = ConnectionFactory.CreateClientConnectionContainer(address, port);
        container.ConnectionEstablished += ConnectionEstablished;
        container.ConnectionLost += ConnectionTerminated;
    }

    private void OnNetworkSetupEvent(EventSetupClientNetworkHandler e) {
        if (DebugFlags.IsDebugEnabled)
        {
            Console.WriteLine($"[CLIENT]: Setting up {e.attr.Id} {e.ModId}");
        }
        NetworkHandlerContainers.Add(e.attr.Id, e);
    }

    private void OnNetworkDisposeEvent(EventDisposeModNetworkHandlers e) {
        List<string> disposees = new List<string>();
        foreach(var pair in NetworkHandlerContainers)
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

    private void OnPacketDecoded(EventDecodedPacket e){
        var b1 = JsonConvert.DeserializeObject(e.payload, jsonSettings)!;
        var type = b1.GetType();
        if (DebugFlags.IsDebugEnabled)
        {
            Console.WriteLine($"[CLIENT]: Got packet {type.Name}");
        }
        if (NetworkHandlerContainers.TryGetValue(type.Name, out EventSetupClientNetworkHandler? value))
        {
            if (value != null)
            {
                value.method.Invoke(null, new object[] { b1 });
            }
        }
    }

    private void ConnectionTerminated(Connection connection, ConnectionType type, CloseReason reason)
    {
        var evt = new EventClientNetworkDisconnect(connection, type, reason);
        PubEventBus.bus.PushEvent(evt);
    }

    private void ConnectionEstablished(Connection connection, ConnectionType type)
    {
        Console.WriteLine($"{type} Connection established");
        connection.RegisterPacketHandler<PacketServerHandshakeResp>(OnServerHandshake, this);
        connection.RegisterPacketHandler<PacketServerRequestJoinData>(OnServerRequestJoinData, this);
        connection.RegisterPacketHandler<PacketServerLobbyJoinedResp>(OnServerLobbyJoinedResp, this);
        connection.RegisterRawDataHandler("ModData", DefaultModPacketHandler.OnClientModPacket);

        connection.Send(new PacketClientHandshake($"{Assembly.GetExecutingAssembly()!.GetName()!.Version!}", pluginLoader!.GetModIdentifiers()));
    }

    private void OnServerLobbyJoinedResp(PacketServerLobbyJoinedResp packet, Connection connection)
    {
        Console.WriteLine($"Joined lobby {packet.lobby}");
        NetworkSenders.Client = this;
        NetworkClientData.lobby = packet.lobby;
        NetworkClientData.me = new NetworkPlayer(packet.uuid, packet.nickname);
        var evt = new EventClientNetworkLobbyJoined(packet.lobby, packet.patch);
        ReadyToOpenGame = true;
        PubEventBus.bus.PushEvent(evt);
    }

    private void OnServerRequestJoinData(PacketServerRequestJoinData packet, Connection connection)
    {
        if (CoreConfigurationHandler.config!.client.patch != string.Empty)
        {
            var data = File.ReadAllBytes(Path.GetFullPath(Path.Combine("./patches", CoreConfigurationHandler.config.client.patch)));
            if (data != null)
            {
                connection.Send(new PacketClientJoinDataResp(CoreConfigurationHandler.config.multiplayer.lobby, CoreConfigurationHandler.config.multiplayer.nickname, data));
            }
        }
        else
        {
            connection.Send(new PacketClientJoinDataResp(CoreConfigurationHandler.config.multiplayer.lobby, CoreConfigurationHandler.config.multiplayer.nickname, Array.Empty<u8>()));
        }
    }

    private void OnServerHandshake(PacketServerHandshakeResp packet, Connection connection)
    {
        if (!packet.accepted)
        {
            connection.Close(CloseReason.DifferentVersion, true);
            return;
        }
        Console.WriteLine("Handshake accepted!");
        var evt = new EventClientNetworkConnection(connection, ConnectionType.TCP);
        PubEventBus.bus.PushEvent(evt);
    }

    public void SendPacket<T>(T packet, string lobbytarget)
    {
        if (packet == null) return;
        var settings = new JsonSerializerSettings { TypeNameHandling = TypeNameHandling.Auto };
        var str = JsonConvert.SerializeObject(packet, typeof(object), settings);
        var raw = RawDataConverter.FromASCIIString("ModData", str);
        if (DebugFlags.IsDebugEnabled)
        {
            Console.WriteLine($"[CLIENT]: {packet} {str}");
        }
        container!.TcpConnection.SendRawData(raw);
    }

    public void SendPacketToSpecificPlayer<T>(T packet, NetworkPlayer player, string lobbytarget)
    {
        throw new NotImplementedException();
    }
}
