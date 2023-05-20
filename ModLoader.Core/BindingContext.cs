using ModLoader.API;

namespace ModLoader.Core;

public class BindingContext
{

    CustomAssemblyContext context;
    Type type;
    string parentDir;
    public BindingInterface? plugin;

    public BindingContext(CustomAssemblyContext context, Type type, string parentDir)
    {
        this.context = context;
        this.type = type;
        this.parentDir = parentDir;
    }

    public void Create()
    {
        Environment.SetEnvironmentVariable("PATH", Environment.GetEnvironmentVariable("PATH") + ";" + parentDir);
        plugin = (BindingInterface)Activator.CreateInstance(type)!;
    }

}
