using System.Reflection;

namespace ModLoader.CLI;

class ModLoader_CLI
{
    static void Main(string[] args)
    {
        Console.WriteLine("ModLoader");
        Version version = Assembly.GetExecutingAssembly()!.GetName()!.Version!;
        string displayableVersion = $"{version}";
        Console.WriteLine(displayableVersion);
        Console.WriteLine("Authors: Drahsid, denoflions");

        Service.bindingLoader.scanBindingsFolder();
        Service.loader.loadPlugins();

        while (true) { }
    }
}