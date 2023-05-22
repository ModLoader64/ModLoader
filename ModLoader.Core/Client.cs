using Network;
using Network.Enums;
using System.Reflection;

namespace ModLoader.Core;

public class Client
{

    public PluginLoader? pluginLoader;
    private ClientConnectionContainer? container;

    public void StartClient(PluginLoader? pluginLoader, string address, int port)
    {
        this.pluginLoader = pluginLoader;
        container = ConnectionFactory.CreateClientConnectionContainer(address, port);
        container.ConnectionEstablished += ConnectionEstablished;
        container.ConnectionLost += ConnectionTerminated;
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
        connection.Send(new PacketClientHandshake($"{Assembly.GetExecutingAssembly()!.GetName()!.Version!}", pluginLoader!.GetModIdentifiers()));
    }

    private void OnServerLobbyJoinedResp(PacketServerLobbyJoinedResp packet, Connection connection)
    {
        Console.WriteLine($"Joined lobby {packet.lobby}");
        var evt = new EventClientNetworkLobbyJoined(packet.lobby, packet.patch);
        PubEventBus.bus.PushEvent(evt);
    }

    private void OnServerRequestJoinData(PacketServerRequestJoinData packet, Connection connection)
    {
        if (CoreConfigurationHandler.config.client.patch != string.Empty)
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
}
