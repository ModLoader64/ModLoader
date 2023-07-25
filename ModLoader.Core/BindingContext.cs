namespace ModLoader.Core;

public class BindingContext
{

    Type type;
    string parentDir;
    public IBindingDelegates? plugin;

    public BindingContext(Type type, string parentDir)
    {
        this.type = type;
        this.parentDir = parentDir;
    }

    public void Create()
    {
        Environment.SetEnvironmentVariable("PATH", Environment.GetEnvironmentVariable("PATH") + ";" + parentDir);
        plugin = (IBindingDelegates) new BindingDelegates(type);
    }

}