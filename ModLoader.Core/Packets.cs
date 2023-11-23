
using Network;
using Network.Packets;
using System.Net.Sockets;
using System.Text.Json;

namespace ModLoader.Core;

public class PacketClientHandshake : Packet
{
    public string version { get; set; }
    public string[] mods { get; set; }

    public PacketClientHandshake(string version, string[] mods)
    {
        this.version = version;
        this.mods = mods;
    }
}

public class PacketServerHandshakeResp : Packet
{
    public string version { get; set; }
    public string[] mods { get; set; }
    public bool accepted { get; set; }

    public PacketServerHandshakeResp(string version, string[] mods, bool accepted)
    {
        this.version = version;
        this.mods = mods;
        this.accepted = accepted;
    }
}

public class PacketServerRequestJoinData : Packet
{
    public PacketServerRequestJoinData() { }
}

public class PacketClientJoinDataResp : Packet
{
    public string lobby { get; set; }
    public string nickname { get; set; }
    public u8[] patch { get; set; }

    public PacketClientJoinDataResp(string lobby, string nickname, u8[] patch)
    {
        this.lobby = lobby;
        this.nickname = nickname;
        this.patch = patch;
    }
}

public class PacketServerLobbyJoinedResp : Packet
{
    public string lobby { get; set; }
    public string nickname { get; set; }

    public string uuid { get; set; }
    public u8[] patch { get; set; }
    public bool ok { get; set; }

    public PacketServerLobbyJoinedResp(string lobby, NetworkPlayer player, byte[] patch, bool ok)
    {
        this.lobby = lobby;
        this.nickname = player.nickname;
        this.uuid = player.uuid;
        this.patch = patch;
        this.ok = ok;
    }
}

public class PacketServerLobbyDisconnectedResp : Packet
{
    public string lobby { get; set; }
    public string nickname { get; set; }

    public string uuid { get; set; }
    public u8[] patch { get; set; }
    public bool ok { get; set; }

    public PacketServerLobbyDisconnectedResp(string lobby, NetworkPlayer player, byte[] patch, bool ok)
    {
        this.lobby = lobby;
        this.nickname = player.nickname;
        this.uuid = player.uuid;
        this.patch = patch;
        this.ok = ok;
    }
}