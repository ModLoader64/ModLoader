using System.Reflection;

namespace ModLoader.Core;

public class PluginContext
{
    CustomAssemblyContext context;
    public readonly Assembly ass;
    IPluginDelegates? plugin;
    public PluginAttribute attribute;
    FileSystemWatcher? watcher;
    string assembly;
    PluginLoader parent;
    public bool hotloadingEnabled = false;
    Type type;
    public readonly string identifier;

    public PluginContext(string assemblyPath, Type type, CustomAssemblyContext context, PluginLoader parent, string identifier, Assembly ass)
    {
        this.context = context;
        this.type = type;
        attribute = (PluginAttribute)Attribute.GetCustomAttribute(type, typeof(PluginAttribute))!;
        assembly = assemblyPath;
        this.parent = parent;
        this.identifier = identifier;
        this.ass = ass;
    }

    private void Setup()
    {
        foreach (Type type in ass.GetTypes())
        {
            EventSystem.HookUpAttributedDelegates(attribute.Name, type, null);
        }
    }

    public void Create()
    {
        plugin = new PluginDelegates(type);
        watcher = new FileSystemWatcher(Path.GetDirectoryName(assembly)!);
        watcher.EnableRaisingEvents = hotloadingEnabled;
        watcher.Changed += OnHotLoad;
        Setup();
    }

    void OnHotLoad(object sender, EventArgs e)
    {
        Console.WriteLine($"Tearing down {plugin}");
        watcher!.EnableRaisingEvents = false;
        Destroy();
        EventSystem.RemoveModHandlers(attribute.Name);
        plugin = null!;
        attribute = null!;
        context.Unload();
        // Gotta be a better way to do this. Sleep is bad.
        Thread.Sleep(1000);
        context = null!;
        Console.WriteLine($"Reconstructing {plugin}");
        this.parent.LoadPlugin(assembly, new CustomAssemblyContext());
    }

    public void Destroy()
    {
        plugin!.Destroy();
        watcher!.Dispose();
    }

    public void Init()
    {
        plugin!.Init();
    }
}
