using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text.Json;

namespace ModLoader.Core;

public class PluginLoader

{
    public Dictionary<string, PluginContext> plugins = new Dictionary<string, PluginContext>();

    public PluginLoader() {
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
    public void LoadPluginFromStream(Stream s, CustomAssemblyContext? c, string name)
    {
        Assembly data;
        if (c != null)
        {
            data = c.LoadFromStream(s);
        }
        else
        {
            data = Assembly.Load(new BinaryReader(s).ReadBytes((int)s.Length));
        }
        if (data != null)
        {
            Type[] types = data.GetTypes();
            foreach (var type in types)
            {
                foreach(var a in type.GetCustomAttributes())
                {
                }
                if (Attribute.GetCustomAttribute(type, typeof(PluginAttribute)) != null)
                {
                    Console.WriteLine(data.ToString());
                    var context = new PluginContext(name, type, c, this, data.ToString(), data);
                    if (Attribute.GetCustomAttribute(type, typeof(Hotloadable)) != null)
                    {
                        context.hotloadingEnabled = true;
                    }
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

    [MethodImpl(MethodImplOptions.NoInlining)]
    public void LoadPlugin(string file, CustomAssemblyContext? c)
    {
        var curFile = Path.GetFullPath(file);
        if (Path.GetExtension(curFile) == ".dll")
        {
            Console.WriteLine($"Scanning {curFile}");
            var s = new FileStream(curFile, FileMode.Open);
            LoadPluginFromStream(s, c, curFile);
            s.Close();
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public void LoadPlugins()
    {
        List<string> dlldirs = new List<string> { "./libs", "./mods" };
        int mode = 0;
        foreach (var dir in dlldirs)
        {
            string pluginDir = dir;
            if (!Directory.Exists(pluginDir))
            {
                Console.WriteLine("Created plugin directory");
                Directory.CreateDirectory(pluginDir);
            }
            var folders = Directory.GetDirectories(pluginDir);
            var files = Directory.GetFiles(pluginDir);
            if (folders.Length == 0 && files.Length == 0)
            {
                Console.WriteLine("No plugins found.");
                break;
            }
            Console.WriteLine("Starting plugin scan...");
            foreach (var folder in folders)
            {
                var _files = Directory.GetFiles(folder);
                if (_files.Length == 0) continue;
                CustomAssemblyContext? c = null;
                if (mode > 0)
                {
                    Console.WriteLine($"Creating context for {Path.GetFullPath(folder)}");
                    c = new CustomAssemblyContext();
                }
                foreach (var file in _files)
                {
                    LoadPlugin(file, c);
                }
            }
            mode++;
        }
    }

}
