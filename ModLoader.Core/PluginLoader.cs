using ModLoader.API;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text.Json;

namespace ModLoader.Core;

public class PluginLoader

{
    public Dictionary<string, PluginContext> plugins = new Dictionary<string, PluginContext>();

    public PluginLoader() {
        if (!Directory.Exists("./config"))
        {
            Directory.CreateDirectory("./config");
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public string[] GetModIdentifiers()
    {
        List<string> str = new List<string>();
        foreach (var plugin in plugins)
        {
            str.Add(plugin.Value.identifier);
        }
        return str.ToArray();
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public void InitPlugins()
    {
        foreach (var plugin in plugins)
        {
            plugin.Value.Init();
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public void LoadPlugin(string file, CustomAssemblyContext c)
    {
        var curFile = Path.GetFullPath(file);
        if (Path.GetExtension(curFile) == ".dll")
        {
            Console.WriteLine($"Scanning {curFile}");
            var s = new FileStream(curFile, FileMode.Open);
            var data = c.LoadFromStream(s);
            s.Close();
            if (data != null)
            {
                var types = data.GetTypes();
                foreach (var type in types)
                {
                    EventSystem.HookUpAttributedDelegates(type, null);
                    if (Attribute.GetCustomAttribute(type, typeof(PluginAttribute)) != null)
                    {
                        Console.WriteLine(data.ToString());
                        var context = new PluginContext(curFile, type, c, this, data.ToString());
                        var fields = type.GetRuntimeFields();
                        foreach (var field in fields)
                        {
                            if (Attribute.GetCustomAttribute(field.FieldType, typeof(ConfigurationAttribute)) != null)
                            {
                                Console.WriteLine($"Found [Configuration] in {field}");
                                var configFile = Path.GetFullPath($"./config/{context.attribute.Name}.json");
                                if (!File.Exists(configFile))
                                {
                                    Console.WriteLine("Config file does not exist for this context. Creating...");
                                    field.SetValue(null, Activator.CreateInstance(field.FieldType));
                                    var options = new JsonSerializerOptions { WriteIndented = true };
                                    string json = JsonSerializer.Serialize(field.GetValue(null), options);
                                    File.WriteAllText(configFile, json);
                                    Console.WriteLine(json);
                                }
                                else
                                {
                                    Console.WriteLine("Loading config file for context...");
                                    string json = File.ReadAllText(configFile);
                                    field.SetValue(null, JsonSerializer.Deserialize(json, field.FieldType));
                                }
                            }
                        }
                        if (!plugins.TryAdd(context.attribute.Name, context))
                        {
                            plugins.Remove(context.attribute.Name);
                            plugins.Add(context.attribute.Name, context);
                        }
                        Console.WriteLine($"Constructing mod: {context.attribute.Name}");
                        context.Create();
                    }
                }
            }
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public void LoadPlugins()
    {
        string pluginDir = "./mods";
        if (!Directory.Exists(pluginDir))
        {
            Console.WriteLine("Created plugin directory");
            Directory.CreateDirectory(pluginDir);
        }
        var folders = Directory.GetDirectories(pluginDir);
        if (folders.Length == 0)
        {
            Console.WriteLine("No plugins found.");
            return;
        }
        Console.WriteLine("Starting plugin scan...");
        foreach ( var folder in folders ) {
            var files = Directory.GetFiles(folder);
            if (files.Length == 0) continue;
            Console.WriteLine($"Creating context for {Path.GetFullPath(folder)}");
            var c = new CustomAssemblyContext();
            foreach ( var file in files )
            {
                LoadPlugin(file, c);
            }
        }
    }

}
