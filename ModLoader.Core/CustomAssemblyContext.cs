using System.Reflection;
using System.Runtime.Loader;

namespace ModLoader.Core;

public class CustomAssemblyContext : AssemblyLoadContext
{
    public CustomAssemblyContext() : base(isCollectible: true)
    {
    }

    protected override Assembly? Load(AssemblyName name)
    {
        return null;
    }
}
