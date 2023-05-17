using ModLoader_API;
using System.Reflection;
using System.Runtime.CompilerServices;

namespace ModLoader_Core;

public class BindingLoader
{

    public Dictionary<string, BindingContext> plugins = new Dictionary<string, BindingContext>();

    [MethodImpl(MethodImplOptions.NoInlining)]
    public bool loadPlugin(string file, CustomAssemblyContext c)
    {
        var curFile = Path.GetFullPath(file);
        if (Path.GetExtension(curFile) == ".dll")
        {
            var s = new FileStream(curFile, FileMode.Open);
            Assembly? data = null;
            try
            {
                data = c.LoadFromStream(s);
            }
            catch (BadImageFormatException)
            {

            }
            s.Close();
            if (data != null)
            {
                var types = data.GetTypes();
                foreach (var type in types)
                {
                    if (Attribute.GetCustomAttribute(type, typeof(BindingAttribute)) != null)
                    {
                        Console.WriteLine(data);
                        var context = new BindingContext(c, type, Path.GetDirectoryName(Path.GetFullPath(file))!);
                        if (!plugins.TryAdd(curFile, context))
                        {
                            plugins.Remove(curFile);
                            plugins.Add(curFile, context);
                        }
                        Console.WriteLine($"Constructing Binding: {curFile}");
                        context.Create();
                        return true;
                    }
                }
            }
        }
        return false;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public void scanBindingsFolder()
    {
        string pluginDir = "./bindings";
        if (!Directory.Exists(pluginDir))
        {
            Console.WriteLine("Created bindings directory");
            Directory.CreateDirectory(pluginDir);
        }
        var folders = Directory.GetDirectories(pluginDir);
        if (folders.Length == 0)
        {
            Console.WriteLine("No bindings found.");
            return;
        }
        Console.WriteLine("Starting binding scan...");
        foreach (var folder in folders)
        {
            var files = Directory.GetFiles(folder);
            if (files.Length == 0) continue;
            Console.WriteLine($"Creating context for {Path.GetFullPath(folder)}");
            var c = new CustomAssemblyContext();
            foreach (var file in files)
            {
                var result = loadPlugin(file, c);
                if (result) break;
            }
        }
    }

}
