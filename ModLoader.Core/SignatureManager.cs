using System.Reflection;
using System.Reflection.Metadata;
using System.Text.Json;

namespace ModLoader.Core;

public class SignatureTarget
{
    public SignatureAttributes attr { get; set; }

}

public class ByteOrWildcard
{
    public byte Byte { get; set; }
    public string Wildcard { get; set; }

    public ByteOrWildcard(byte Byte, string Wildcard)
    {
        this.Byte = Byte;
        this.Wildcard = Wildcard;
    }

    public bool Equals(byte Byte)
    {
        return Byte == this.Byte || Wildcard == "??";
    }

}


public class SignatureManager
{

    public void ScanPlugin(PluginContext plugin, string gameHash)
    {
        ScanAssembly(plugin.ass, gameHash);
    }

    public IEnumerable<IntPtr> PatternAt(byte[] source, ByteOrWildcard[] pattern, IntPtr startAt = 0)
    {
        List<IntPtr> result = new List<IntPtr>();
        for (IntPtr i = startAt; i < source.Length; i++)
        {
            if (pattern[0].Equals(source[i]))
            {
                var potential = true;
                for (int k = 0; k < pattern.Length; k++)
                {
                    if (i + k >= source.Length) break;
                    if (!pattern[k].Equals(source[i + k]))
                    {
                        potential = false;
                        break;
                    }
                }
                if (potential)
                {
                    result.Add(i);
                    break;
                }
            }
        }
        return result;
    }

    public void ScanAssembly(Assembly ass, string gameHash)
    {
        if (ass == null) return;

        if (!Directory.Exists("./cache"))
        {
            Directory.CreateDirectory("./cache");
        }
        var name = ass.FullName!.Split(",")[0].Trim();
        var cache = Path.Join("./cache", name);
        if (!Directory.Exists(cache))
        {
            Directory.CreateDirectory(cache);
        }
        cache = Path.Join(cache, gameHash);
        if (!Directory.Exists(cache))
        {
            Directory.CreateDirectory(cache);
        }
        cache = Path.Join(cache, "sigcache.json");
        Dictionary<string, string> keyValuePairs = new Dictionary<string, string>();
        if (File.Exists(cache))
        {
            keyValuePairs = JsonSerializer.Deserialize<Dictionary<string, string>>(File.ReadAllText(cache));
        }

        // Get this some binding mutable way.
        var maxRam = 8 * 1024 * 1024;
        var ram = new byte[maxRam];
        for (int i = 0; i < maxRam; i++)
        {
            ram[i] = Memory.RAM.ReadU8((uint)i);
        }
        foreach (Type type in ass.GetTypes())
        {
            foreach (FieldInfo field in type.GetRuntimeFields())
            {
                if (field.GetCustomAttribute<Signature>() != null)
                {
                    if (field.GetValue(null) != null && ((IntPtr)field.GetValue(null)) > 0)
                    {
                        continue;
                    }
                    if (keyValuePairs.ContainsKey(field.Name))
                    {
                        field.SetValue(null, (IntPtr)Convert.ToUInt64(keyValuePairs[field.Name], 16));
                        continue;
                    }
                    var sig = field.GetCustomAttribute<Signature>()!.bytes;
                    var split = sig.Split(" ");
                    var bytes = new ByteOrWildcard[split.Length];
                    for (int i = 0; i < split.Length; i++)
                    {
                        if (split[i] == "??")
                        {
                            bytes[i] = new ByteOrWildcard(byte.Parse("FF", System.Globalization.NumberStyles.HexNumber), split[i]);
                        }
                        else
                        {
                            bytes[i] = new ByteOrWildcard(byte.Parse(split[i], System.Globalization.NumberStyles.HexNumber), split[i]);
                        }
                    }
                    Ptr pointer = PatternAt(ram, bytes).FirstOrDefault();
                    if (pointer > 0)
                    {
                        Console.WriteLine($"Resolved [Signature] {field.Name} to {pointer.ToString("X2")}");
                    }
                    else
                    {
                        Console.WriteLine($"Failed to resolve [Signature] {field.Name}");
                    }
                    field.SetValue(null, pointer);
                    keyValuePairs.Add(field.Name, $"0x{pointer.ToString("X2")}");
                }
            }
        }
        File.WriteAllText(cache, JsonSerializer.Serialize(keyValuePairs));
    }

}
