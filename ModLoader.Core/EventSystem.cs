using System.Reflection;

namespace ModLoader.Core;

public class EventSystem
{
    public static void HookUpAttributedDelegates(string modId, Type t, object? instance)
    {
        var m = t.GetRuntimeMethods();

        List<Type> EventStuff = new List<Type> { typeof(EventHandlerAttribute), typeof(OnFrameAttribute), typeof(OnViUpdateAttribute), typeof(OnEmulatorStartAttribute), typeof(OnInitAttribute)};
        List<Type> ClientNetwork = new List<Type> { typeof(ClientNetworkHandlerAttribute) };
        List<Type> ServerNetwork = new List<Type> { typeof(ServerNetworkHandlerAttribute) };

        foreach (var m2 in m)
        {
            foreach(var e in EventStuff)
            {
                var attr = (IEvent) Attribute.GetCustomAttribute(m2, e)!;
                if (attr != null && m2.IsStatic)
                {
                    Console.WriteLine($"Found [{e}] in {t} method {m2.Name}");
                    PubEventBus.bus.RegisterEventHandler(new EventRegistration(attr.Id, DelegateHelper.CreateDelegate(m2, instance), modId));
                }
            }

            foreach (var e in ClientNetwork)
            {
                var attr = (IEvent)Attribute.GetCustomAttribute(m2, e)!;
                if (attr != null && m2.IsStatic)
                {
                    Console.WriteLine($"Found [{e}] in {t} method {m2.Name}");
                    InternalEventBus.bus.PushEvent(new EventSetupClientNetworkHandler(m2, (ClientNetworkHandlerAttribute)attr, modId));
                }
            }

            foreach (var e in ServerNetwork)
            {
                var attr = (IEvent)Attribute.GetCustomAttribute(m2, e)!;
                if (attr != null && m2.IsStatic)
                {
                    Console.WriteLine($"Found [{e}] in {t} method {m2.Name}");
                    InternalEventBus.bus.PushEvent(new EventSetupServerNetworkHandler(m2, (ServerNetworkHandlerAttribute)attr, modId));
                }
            }
        }
    }
    public static void RemoveModHandlers(string modId)
    {
        PubEventBus.bus.UnregisterAllByParentId(modId);
        InternalEventBus.bus.PushEvent(new EventDisposeModNetworkHandlers(modId));
    }
}
