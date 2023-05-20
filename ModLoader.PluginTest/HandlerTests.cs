using ModLoader.API.EventBus;
using ModLoader.API;

namespace ModLoader.PluginTest;

public class HandlerTests
{
    [EventHandler(NetworkEvents.CLIENT_ON_NETWORK_LOBBY_JOIN)]
    public static void OnLobbyJoin(EventClientNetworkLobbyJoined e)
    {
        Console.WriteLine("We connected to the lobby!");
    }

    [OnTick]
    public static void OnTick(EventNewFrame e)
    {
    }

    [OnViUpdate]
    public static void OnViUpdate(EventNewVi e) 
    {
    }
}
