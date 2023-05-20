using System.Linq.Expressions;
using System.Reflection;

namespace ModLoader.API;

public static class DelegateHelper
{
    public static Delegate CreateDelegate(MethodInfo methodInfo, object? target)
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
    public static T Cast<T>(Delegate source) where T : class
    {
        return Cast(source, typeof(T)) as T;
    }

    public static Delegate Cast(Delegate source, Type type)
    {
        if (source == null)
            return null;
        Delegate[] delegates = source.GetInvocationList();
        if (delegates.Length == 1)
            return Delegate.CreateDelegate(type,
                delegates[0].Target, delegates[0].Method);
        Delegate[] delegatesDest = new Delegate[delegates.Length];
        for (int nDelegate = 0; nDelegate < delegates.Length; nDelegate++)
            delegatesDest[nDelegate] = Delegate.CreateDelegate(type,
                delegates[nDelegate].Target, delegates[nDelegate].Method);
        return Delegate.Combine(delegatesDest);
    }
}
