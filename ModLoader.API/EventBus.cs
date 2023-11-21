namespace ModLoader.API;

public class EventRegistration
{
    public string Id { get; set; }
    public Delegate Action { get; set; }
    public string parentId { get; set; }

    public EventRegistration(string id, Delegate action, string parentId)
    {
        Id = id;
        Action = action;
        this.parentId = parentId;
    }
}

public interface IEvent
{
    public string Id { get; set; }
}

public interface IEventBus
{
    public void RegisterEventHandler(EventRegistration reg);

    public void UnregisterEventHandler(EventRegistration reg);

    public void PushEvent(IEvent evt);

    public void UnregisterAllByParentId(string parentId);

}

public delegate void EventDelegate(IEvent evt);

public class EventBusImpl : IEventBus
{
    private Dictionary<string, List<EventRegistration>> handlers = new Dictionary<string, List<EventRegistration>>();

    public void PushEvent(IEvent evt)
    {
        if (handlers.TryGetValue(evt.Id, out List<EventRegistration> value))
        {
            foreach (var reg in value)
            {
                reg.Action.DynamicInvoke(evt);
            }
        }
        else
        {
            if (DebugFlags.IsDebugEnabled)
            {
                Console.WriteLine($"Cannot find handler for {evt.Id}.");
            }
        }
    }

    public void RegisterEventHandler(EventRegistration reg)
    {
        if (!handlers.ContainsKey(reg.Id))
        {
            handlers.Add(reg.Id, new List<EventRegistration>());
        }
        handlers[reg.Id].Add(reg);
    }

    public void UnregisterEventHandler(EventRegistration reg)
    {
        if (handlers.ContainsKey(reg.Id))
        {
            handlers[reg.Id].Remove(reg);
        }
    }

    public void UnregisterAllByParentId(string parentId) { 
        List<EventRegistration> eventRegistrations = new List<EventRegistration>();
        foreach (var kvp in handlers)
        {
            foreach(var g in kvp.Value)
            {
                if (g.parentId == parentId)
                {
                    eventRegistrations.Add(g);
                }
            }
        }
        foreach(var evt in eventRegistrations)
        {
            UnregisterEventHandler(evt);
        }
    }
}

public static class PubEventBus
{
    public static readonly IEventBus bus = new EventBusImpl();
}

[AttributeUsage(AttributeTargets.Method, Inherited = true, AllowMultiple = false)]
public class EventHandlerAttribute : Attribute, IEvent
{
    public string Id { get; set; }

    public EventHandlerAttribute(string id)
    {
        Id = id;
    }
}
