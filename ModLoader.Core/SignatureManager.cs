using System.Reflection;
using System.Reflection.Metadata;

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

    public void ScanPlugin(PluginContext plugin)
    {
        ScanAssembly(plugin.ass);
    }

    public IEnumerable<int> PatternAt(byte[] source, ByteOrWildcard[] pattern)
    {
        List<int> result = new List<int>();
        for (int i = 0; i < source.Length; i++)
        {
            if (pattern[0].Equals(source[i]))
            {
                var potential = true;
                for (int k = 0; k < pattern.Length; k++)
                {
                    if (!pattern[k].Equals(source[i + k]))
                    {
                        potential = false;
                        break;
                    }
                }
                if (potential)
                {
                    result.Add(i);
                }
            }
        }
        return result;
    }


    public void ScanAssembly(Assembly ass)
    {
        if (ass == null) return;
        foreach (Type type in ass.GetTypes())
        {
            foreach (FieldInfo field in type.GetRuntimeFields())
            {
                if (field.GetCustomAttribute<Signature>() != null)
                {
                    Console.WriteLine($"Found [Signature] in {type} {field}");
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
                    // Get this some binding mutable way.
                    var maxRam = 8 * 1024 * 1024;
                    var ram = new byte[maxRam];
                    for (int i = 0; i < maxRam; i++)
                    {
                        ram[i] = Memory.RAM.ReadU8((uint)i);
                    }
                    Ptr pointer = PatternAt(ram, bytes).FirstOrDefault();
                    field.SetValue(null, pointer);
                }
            }
        }
    }

}
