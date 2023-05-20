using ModLoader.API;
using ModLoader.API.EventBus;
using ModLoader.Core;
using System.Reflection;

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
        Service.loader.LoadPlugins();

        if (CoreConfigurationHandler.config.multiplayer.isServer)
        {
            Service.server.StartServer(Service.loader);
        }
        if (CoreConfigurationHandler.config.multiplayer.isClient)
        {
            Service.client.StartClient(Service.loader, CoreConfigurationHandler.config.multiplayer.server_ip, CoreConfigurationHandler.config.multiplayer.port);
            EventSystem.HookUpAttributedDelegates(typeof(ModLoader_CLI), null);
        }
        Service.bindingLoader.plugins.FirstOrDefault()!.Value.plugin.SetGameFile(Path.GetFullPath(Path.Join("./roms", CoreConfigurationHandler.config.client.rom)));
        Service.bindingLoader.plugins.FirstOrDefault()!.Value.plugin.InitBinding();
        Service.bindingLoader.plugins.FirstOrDefault()!.Value.plugin.StartBinding();

    }
}