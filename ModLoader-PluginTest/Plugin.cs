using ModLoader_API;

namespace ModLoader_PluginTest;

[Plugin("PluginTest")]
public class Plugin : PluginInterface
{

    public static Configuration? Configuration { get; set; }

    public Plugin() {
        Console.WriteLine("PluginTest is now loaded!");
        Console.WriteLine(Configuration!.test);
    }

    public void Init()
    {
        Console.WriteLine("Init");
    }

    public void Destroy()
    {
        Console.WriteLine("Destroy");
    }

    public void OnTick()
    {
        Console.WriteLine("OnTick");
    }

}