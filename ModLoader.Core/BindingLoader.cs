using System.Reflection;
using System.Runtime.CompilerServices;

namespace ModLoader.Core;

public class BindingLoader
{

    public Dictionary<string, BindingContext> plugins = new Dictionary<string, BindingContext>();

    public BindingLoader()
    {
        Memory.RAM = (IMemoryDelegates)new MemoryDelegates(typeof(DefaultMemory));
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public bool LoadPlugin(string file, CustomAssemblyContext c)
    {
        var curFile = Path.GetFullPath(file);
        if (Path.GetExtension(curFile) == ".dll" && Path.GetFileNameWithoutExtension(curFile).ToLowerInvariant().Contains("ModLoader".ToLowerInvariant()))
        {
            FileStream? s = null;
            Assembly? data = null;
            try
            {
                s = new FileStream(curFile, FileMode.Open);
                data = c.LoadFromStream(s);
                s.Close();
            }
            catch (BadImageFormatException)
            {

            }
            catch (IOException)
            {

            }
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
                        context.Create();
                    }else if (Attribute.GetCustomAttribute(type, typeof(BoundMemoryAttribute)) != null)
                    {
                        Console.WriteLine($"Found [BoundMemory] on {type}");
                        Memory.RAM = new MemoryDelegates(type);
                    }
                }
            }
        }
        return false;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public void ScanBindingsFolder()
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
                LoadPlugin(file, c);
            }
        }
    }

}
