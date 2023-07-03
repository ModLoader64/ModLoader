using System.Reflection;
using System.Runtime.CompilerServices;

namespace ModLoader.CLI;

class ModLoader_CLI
{
    /// <summary>
    /// Entry point.
    /// </summary>
    /// <param name="args"></param>
    static void Main(string[] args)
    {
        Console.WriteLine("ModLoader");
        Version version = Assembly.GetExecutingAssembly()!.GetName()!.Version!;
        string displayableVersion = $"{version}";
        Console.WriteLine(displayableVersion);
        Console.WriteLine("Authors: Drahsid, denoflions");

        CoreConfigurationHandler.SetupCoreConfiguration();

        Service.bindingLoader.ScanBindingsFolder();

        if (CoreConfigurationHandler.config!.multiplayer.isServer)
        {
            Service.server.StartServer(Service.loader);
        }
        if (CoreConfigurationHandler.config.multiplayer.isClient)
        {
            Service.client.StartClient(Service.loader, CoreConfigurationHandler.config.multiplayer.server_ip, CoreConfigurationHandler.config.multiplayer.port);
            EventSystem.HookUpAttributedDelegates("ModLoader", typeof(ModLoader_CLI), null);
        }
        Service.loader.LoadPlugins();
        Service.loader.InitPlugins();
        Service.bindingLoader.plugins.FirstOrDefault()!.Value.plugin!.SetGameFile(Path.GetFullPath(Path.Join("./roms", CoreConfigurationHandler.config.client.rom)));
        Service.bindingLoader.plugins.FirstOrDefault()!.Value.plugin!.InitBinding();
        Service.bindingLoader.plugins.FirstOrDefault()!.Value.plugin!.StartBinding();
    }

    private static bool firstFrame = false;

    [OnViUpdate]
    private static void OnViUpdate() {
        Service.ImGuiManager.ProcessEvents();
    }

    [OnFrame]
    private static void onFrame(EventNewFrame e) { 
        if (!firstFrame)
        {
            foreach (var plugin in Service.loader.plugins)
            {
                Service.signatureManager.ScanPlugin(plugin.Value);
            }
            PubEventBus.bus.PushEvent(new EventEmulatorStart());
            firstFrame = true;
        }
    }
}