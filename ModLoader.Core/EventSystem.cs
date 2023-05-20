using ModLoader.API;
using ModLoader.API.EventBus;
using System.Linq.Expressions;
using System.Reflection;

namespace ModLoader.Core
{
    public class EventSystem
    {
        private static Delegate CreateDelegate(MethodInfo methodInfo, object? target)
        {
            Func<Type[], Type> getType;
            var isAction = methodInfo.ReturnType.Equals((typeof(void)));
            var types = methodInfo.GetParameters().Select(p => p.ParameterType);

            if (isAction)
            {
                getType = Expression.GetActionType;
            }
            else
            {
                getType = Expression.GetFuncType;
                types = types.Concat(new[] { methodInfo.ReturnType });
            }

            if (methodInfo.IsStatic)
            {
                return Delegate.CreateDelegate(getType(types.ToArray()), methodInfo);
            }

            return Delegate.CreateDelegate(getType(types.ToArray()), target!, methodInfo.Name);
        }

        public static void HookUpAttributedDelegates(Type t, object? instance)
        {
            var m = t.GetRuntimeMethods();
            foreach (var m2 in m)
            {
                var attr = m2.GetCustomAttribute<EventHandlerAttribute>();
                if (attr != null && m2.IsStatic)
                {
                    Console.WriteLine($"Found [EventHandler] in {t} method {m2.Name}");
                    PubEventBus.bus.RegisterEventHandler(new EventRegistration(attr.Id, CreateDelegate(m2, instance)));
                }
                var attr2 = m2.GetCustomAttribute<OnTickAttribute>();
                if (attr2 != null && m2.IsStatic)
                {
                    Console.WriteLine($"Found [OnTick] in {t} method {m2.Name}");
                    PubEventBus.bus.RegisterEventHandler(new EventRegistration(attr2.Id, CreateDelegate(m2, instance)));
                }
                var attr3 = m2.GetCustomAttribute<OnViUpdateAttribute>();
                if (attr3 != null && m2.IsStatic)
                {
                    Console.WriteLine($"Found [OnViUpdate] in {t} method {m2.Name}");
                    PubEventBus.bus.RegisterEventHandler(new EventRegistration(attr3.Id, CreateDelegate(m2, instance)));
                }
            }
        }
    }
}
