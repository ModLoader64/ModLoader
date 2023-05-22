using System.Linq.Expressions;
using System.Reflection;

namespace ModLoader.Core
{
    public class EventSystem
    {
        public static void HookUpAttributedDelegates(Type t, object? instance)
        {
            var m = t.GetRuntimeMethods();
            foreach (var m2 in m)
            {
                var attr = m2.GetCustomAttribute<EventHandlerAttribute>();
                if (attr != null && m2.IsStatic)
                {
                    Console.WriteLine($"Found [EventHandler] in {t} method {m2.Name}");
                    PubEventBus.bus.RegisterEventHandler(new EventRegistration(attr.Id, DelegateHelper.CreateDelegate(m2, instance)));
                }
                var attr2 = m2.GetCustomAttribute<OnTickAttribute>();
                if (attr2 != null && m2.IsStatic)
                {
                    Console.WriteLine($"Found [OnTick] in {t} method {m2.Name}");
                    PubEventBus.bus.RegisterEventHandler(new EventRegistration(attr2.Id, DelegateHelper.CreateDelegate(m2, instance)));
                }
                var attr3 = m2.GetCustomAttribute<OnViUpdateAttribute>();
                if (attr3 != null && m2.IsStatic)
                {
                    Console.WriteLine($"Found [OnViUpdate] in {t} method {m2.Name}");
                    PubEventBus.bus.RegisterEventHandler(new EventRegistration(attr3.Id, DelegateHelper.CreateDelegate(m2, instance)));
                }
            }
        }
    }
}
