using Network.Packets;
using Network;
using Network.Converter;
using System.Text.Json;

namespace ModLoader.Core;

public static class DefaultModPacketHandler
{

    public static void OnClientModPacket(RawData packet, Connection connection)
    {
        var str = RawDataConverter.ToASCIIString(packet);
        InternalEventBus.client.PushEvent(new EventDecodedPacket(str));
    }

    public static void OnServerModPacket(RawData packet, Connection connection) {
        var str = RawDataConverter.ToASCIIString(packet);
        InternalEventBus.server.PushEvent(new EventDecodedPacket(str));
    }
}
