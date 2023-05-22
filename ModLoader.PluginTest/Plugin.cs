namespace ModLoader.PluginTest;

[Plugin("PluginTest")]
public class Plugin : IPlugin
{

    public static Configuration? Configuration { get; set; }

    public static void Init()
    {
        Console.WriteLine("Init");
    }

    public static void Destroy()
    {
        Console.WriteLine("Destroy");
    }

}