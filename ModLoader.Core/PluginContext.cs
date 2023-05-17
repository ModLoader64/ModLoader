using ModLoader.API;
using System.Formats.Tar;

namespace ModLoader.Core;

public class PluginContext : PluginInterface
{
    CustomAssemblyContext context;
    PluginInterface? plugin;
    public PluginAttribute attribute;
    FileSystemWatcher? watcher;
    string assembly;
    PluginLoader parent;
    bool hotloadingEnabled = false;
    Type type;

    public PluginContext(string assemblyPath, Type type, CustomAssemblyContext context, PluginLoader parent)
    {
        this.context = context;
        this.type = type;
        attribute = (PluginAttribute)Attribute.GetCustomAttribute(type, typeof(PluginAttribute))!;
        assembly = assemblyPath;
        this.parent = parent;
    }

    public void Create()
    {
        plugin = (PluginInterface)Activator.CreateInstance(type)!;
        watcher = new FileSystemWatcher(Path.GetDirectoryName(assembly)!);
        watcher.EnableRaisingEvents = hotloadingEnabled;
        watcher.Changed += OnHotLoad;
    }

    void OnHotLoad(object sender, EventArgs e)
    {
        Console.WriteLine($"Tearing down {plugin}");
        watcher!.EnableRaisingEvents = false;
        Destroy();
        plugin = null!;
        attribute = null!;
        context.Unload();
        Thread.Sleep(1000);
        context = null!;
        Console.WriteLine($"Reconstructing {plugin}");
        this.parent.loadPlugin(assembly, new CustomAssemblyContext());
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

    public void OnTick()
    {
        plugin!.OnTick();
    }
}
