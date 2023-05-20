namespace ModLoader.API.EventBus
{

    public class EventRegistration
    {
        public string Id { get; set; }
        public Delegate Action { get; set; }

        public EventRegistration(string id, Delegate action)
        {
            this.Id = id;
            this.Action = action;
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

    }

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
        }

        public void RegisterEventHandler(EventRegistration reg)
        {
            if (!handlers.ContainsKey(reg.Id))
            {
                Console.WriteLine($"{reg.Id} connected to bus.");
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
    }

    public static class PubEventBus
    {
        public static readonly IEventBus bus = new EventBusImpl();
    }

    [AttributeUsage(AttributeTargets.Method, Inherited = true, AllowMultiple = false)]
    public class EventHandlerAttribute : Attribute
    {
        public string Id { get; set; }

        public EventHandlerAttribute(string id)
        {
            Id = id;
        }
    }
}
