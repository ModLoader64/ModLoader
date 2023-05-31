using System.Reflection;

namespace ModLoader.Core;

public static class InternalEventBus
{
    public static readonly IEventBus bus = new EventBusImpl();
    public static readonly IEventBus client = new EventBusImpl();
    public static readonly IEventBus server = new EventBusImpl();
}

public class EventSetupClientNetworkHandler : IEvent
{
    public string Id { get; set; } = "EventSetupClientNetworkHandler";
    public MethodInfo method { get; set; }
    public ClientNetworkHandlerAttribute attr { get; set; }
    public string ModId { get; set; }

    public EventSetupClientNetworkHandler(MethodInfo method, ClientNetworkHandlerAttribute attr, string modId)
    {
        this.method = method;
        this.attr = attr;
        ModId = modId;
    }
}

public class EventSetupServerNetworkHandler : IEvent
{
    public string Id { get; set; } = "EventSetupServerNetworkHandler";
    public MethodInfo method { get; set; }
    public ServerNetworkHandlerAttribute attr { get; set; }
    public string ModId { get; set; }

    public EventSetupServerNetworkHandler(MethodInfo method, ServerNetworkHandlerAttribute attr, string modId)
    {
        this.method = method;
        this.attr = attr;
        ModId = modId;
    }
}

public class EventDecodedPacket : IEvent
{
    public string Id { get; set; } = "EventDecodedPacket";
    public string payload { get; set; }

    public EventDecodedPacket(string payload)
    {
        this.payload = payload;
    }
}

public class EventDisposeModNetworkHandlers : IEvent
{
    public string Id { get; set; } = "EventDisposeModNetworkHandlers";
    public string ModId { get; set; }

    public EventDisposeModNetworkHandlers(string modId)
    {
        this.ModId = modId;
    }
}