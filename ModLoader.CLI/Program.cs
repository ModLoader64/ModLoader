namespace ModLoader.CLI;
class ModLoader_CLI
{
    static void Main(string[] args)
    {
        Console.WriteLine("ModLoader");
        Console.WriteLine("V0.0.1");
        Console.WriteLine("Authors: Drahsid, denoflions");

        Service.bindingLoader.scanBindingsFolder();
        Service.loader.loadPlugins();

        while (true) { }
    }
}