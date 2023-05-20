using ModLoader.API;

namespace ModLoader.PluginTest;

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
        if (MemoryAccess.ram == null) return;
        Console.WriteLine(MemoryAccess.ram.ReadU32(0).ToString());
    }

}