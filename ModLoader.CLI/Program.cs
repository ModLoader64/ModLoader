using System.Reflection;

namespace ModLoader.CLI;

class ModLoader_CLI
{

    private static string RomHash = "";
    static ManualResetEvent _quitEvent = new ManualResetEvent(false);

    /// <summary>
    /// Entry point.
    /// </summary>
    /// <param name="args"></param>
    static void Main(string[] args)
    {
        Console.CancelKeyPress += (sender, eArgs) => {
            _quitEvent.Set();
            eArgs.Cancel = true;
        };

        Console.WriteLine("ModLoader");
        Version version = Assembly.GetExecutingAssembly()!.GetName()!.Version!;
        string displayableVersion = $"{version}";
        Console.WriteLine(displayableVersion);
        Console.WriteLine("Authors: Drahsid, denoflions");

        CoreConfigurationHandler.SetupCoreConfiguration();

        Service.bindingLoader.ScanBindingsFolder();

        SetupNetworkingPreInitSystems();

        EventSystem.HookUpAttributedDelegates("ModLoader", typeof(ModLoader_CLI), null);

        var rom = File.ReadAllBytes(Path.GetFullPath(Path.Join("./roms", CoreConfigurationHandler.config!.client.rom)));
        RomHash = Utils.GetHashSHA1(rom);
        Service.loader.LoadPlugins(rom);
        Service.loader.InitPlugins();
        PubEventBus.bus.PushEvent(new EventRomLoaded(rom));

        //ThreadStart childref = new ThreadStart(StartNetworking);
        //Console.WriteLine("Starting networking thread...");
        //Thread childThread = new Thread(childref);
        //childThread.Start();

        StartNetworking();

        while (!Service.client.ReadyToOpenGame)
        {
        }

        //ThreadStart bindingref = new ThreadStart(StartEmulatorBinding);
        //Console.WriteLine("Starting emulator thread...");
        //Thread bindingThread = new Thread(bindingref);
        //bindingThread.Start();

        StartEmulatorBinding();

        _quitEvent.WaitOne();

    }

    private static bool firstFrame = false;
    private static bool bindingConstructed = false;

    private static void SetupNetworkingPreInitSystems()
    {
        if (CoreConfigurationHandler.config!.multiplayer.isServer)
        {
            Service.server.PreStartServer();
        }
        if (CoreConfigurationHandler.config.multiplayer.isClient)
        {
            Service.client.PreStartClient();
        }
    }

    private static void StartNetworking()
    {
        if (CoreConfigurationHandler.config!.multiplayer.isServer)
        {
            Service.server.StartServer(Service.loader);
        }
        if (CoreConfigurationHandler.config.multiplayer.isClient)
        {
            Service.client.StartClient(Service.loader, CoreConfigurationHandler.config.multiplayer.server_ip, CoreConfigurationHandler.config.multiplayer.port);
        }
    }

    private static void StartEmulatorBinding()
    {
        if ( bindingConstructed )
        {
            return;
        }
        bindingConstructed = true;
        if (CoreConfigurationHandler.config!.multiplayer.isClient)
        {
            Service.bindingLoader.plugins.FirstOrDefault()!.Value.plugin!.SetGameFile(Path.GetFullPath(Path.Join("./roms", CoreConfigurationHandler.config.client.rom)));
            Service.bindingLoader.plugins.FirstOrDefault()!.Value.plugin!.InitBinding();
            Service.bindingLoader.plugins.FirstOrDefault()!.Value.plugin!.StartBinding();
        }
    }

    [OnFrame]
    private static void onFrame(EventNewFrame e) { 
        if (!firstFrame)
        {
            foreach (var plugin in Service.loader.plugins)
            {
                Service.signatureManager.ScanPlugin(plugin.Value, RomHash);
            }
            PubEventBus.bus.PushEvent(new EventEmulatorStart());
            firstFrame = true;
        }
    }
}