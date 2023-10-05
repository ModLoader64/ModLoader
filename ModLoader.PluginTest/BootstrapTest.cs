namespace ModLoader.PluginTest;

[BootstrapFilter]
public class BootstrapTest : IBootstrapFilter
{

    public static bool DoesLoad()
    {
        return true;
    }

    [OnInit]
    public static void OnInit(EventPluginsLoaded evt) {
        Console.WriteLine("This got init!");
    }

}
