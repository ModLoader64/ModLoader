namespace ModLoader.PluginTest;

[Plugin("PluginTest")]
public class Plugin : IPlugin
{

    public static Configuration? Configuration { get; set; }
    public static bool isOpen = true;

    public static void Init()
    {
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
    }

    [OnFrame]
    public static void OnTick(EventNewFrame e)
    {
    }

    [OnViUpdate]
    public static void OnViUpdate(EventNewVi e)
    {
    }

}