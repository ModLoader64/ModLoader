
using Network.Packets;

namespace ModLoader.Core
{
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
        public u8[] patch { get; set; }
        public bool ok { get; set; }

        public PacketServerLobbyJoinedResp(string lobby, byte[] patch, bool ok)
        {
            this.lobby = lobby;
            this.patch = patch;
            this.ok = ok;
        }
    }
}
