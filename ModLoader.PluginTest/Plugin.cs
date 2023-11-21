namespace ModLoader.PluginTest;

[Plugin("PluginTest")]
public class Plugin : IPlugin
{

    public static Configuration? Configuration { get; set; }
    public static bool isOpen = true;

    public static void Init()
    {
        DebugFlags.IsDebugEnabled = true;
        Console.WriteLine("Init");
    }

    public static void Destroy()
    {
        Console.WriteLine("Destroy");
    }

    [EventHandler(NetworkEvents.CLIENT_ON_NETWORK_LOBBY_JOIN)]
    public static void OnLobbyJoin(EventClientNetworkLobbyJoined e)
    {
        Console.WriteLine("We connected to the lobby!");
        Console.WriteLine("TRYING TO SEND PACKET");
        NetworkSenders.Client.SendPacket(new ExamplePacket("???"), NetworkClientData.lobby);
    }

    [OnFrame]
    public static void OnTick(EventNewFrame e)
    {
    }

    [OnViUpdate]
    public static void OnViUpdate(EventNewVi e)
    {
    }

    [ClientNetworkHandler(typeof(ExamplePacket))]
    public static void OnPacketClient(ExamplePacket packet) {
        Console.WriteLine("Client!");
    }

    [ServerNetworkHandler(typeof(ExamplePacket))]
    public static void OnPacketServer(ExamplePacket packet)
    {
        Console.WriteLine("Server!");
    }

}