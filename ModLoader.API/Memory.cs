using System.Reflection;
using static ModLoader.API.MemoryAccess;

namespace ModLoader.API;

public interface IMemory
{
    /// <summary>
    /// Read a u328.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract u8 ReadU8(u32 address);

    /// <summary>
    /// Read a u3216.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract u16 ReadU16(u32 address);

    /// <summary>
    /// Read a u3232.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract u32 ReadU32(u32 address);

    /// <summary>
    /// Read a u3264.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract u64 ReadU64(u32 address);

    /// <summary>
    /// Read a SInt8
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract s8 ReadS8(u32 address);

    /// <summary>
    /// Read a SInt16
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract s16 ReadS16(u32 address);

    /// <summary>
    /// Read a SInt32
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract s32 ReadS32(u32 address);

    /// <summary>
    /// Read a SInt64.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract s64 ReadS64(u32 address);

    /// <summary>
    /// Read a f32 32.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract f32 ReadF32(u32 address);

    /// <summary>
    /// Read a f32 64.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract f64 ReadF64(u32 address);

    /// <summary>
    /// Write a u328.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteU8(u32 address, u8 value);

    /// <summary>
    /// Write a u3216.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteU16(u32 address, u16 value);

    /// <summary>
    /// Write a u3232.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteU32(u32 address, u32 value);

    /// <summary>
    /// Write a u3264.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteU64(u32 address, u64 value);

    /// <summary>
    /// Write a SInt8.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteS8(u32 address, s8 value);

    /// <summary>
    /// Write a SInt16.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteS16(u32 address, s16 value);

    /// <summary>
    /// Write a SInt32.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteS32(u32 address, s32 value);

    /// <summary>
    /// Write a SInt64.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteS64(u32 address, s64 value);

    /// <summary>
    /// Write a f32 32.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteF32(u32 address, f32 value);

    /// <summary>
    /// Write a f32 64.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteF64(u32 address, f64 value);

}

public static class MemoryBinding
{
    private static Type boundClass;

    public static void SetBoundClass(Type type)
    {
        Console.WriteLine("Generating memory delegates...");
        var start = DateTimeOffset.Now.ToUnixTimeMilliseconds();
        boundClass = type;
        // Reads
        Memory.RAM.ReadF32 = DelegateHelper.Cast<ReadF32_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("ReadF32", new Type[] { typeof(u32) })!, null));
        Memory.RAM.ReadF64 = DelegateHelper.Cast<ReadF64_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("ReadF64", new Type[] { typeof(u32) })!, null));
        Memory.RAM.ReadS16 = DelegateHelper.Cast<ReadS16_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("ReadS16", new Type[] { typeof(u32) })!, null));
        Memory.RAM.ReadS32 = DelegateHelper.Cast<ReadS32_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("ReadS32", new Type[] { typeof(u32) })!, null));
        Memory.RAM.ReadS64 = DelegateHelper.Cast<ReadS64_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("ReadS64", new Type[] { typeof(u32) })!, null));
        Memory.RAM.ReadS8 = DelegateHelper.Cast<ReadS8_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("ReadS8", new Type[] { typeof(u32) })!, null));
        Memory.RAM.ReadU16 = DelegateHelper.Cast<ReadU16_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("ReadU16", new Type[] { typeof(u32) })!, null));
        Memory.RAM.ReadU32 = DelegateHelper.Cast<ReadU32_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("ReadU32", new Type[] { typeof(u32) })!, null));
        Memory.RAM.ReadU64 = DelegateHelper.Cast<ReadU64_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("ReadU64", new Type[] { typeof(u32) })!, null));
        Memory.RAM.ReadU8 = DelegateHelper.Cast<ReadU8_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("ReadU8", new Type[] { typeof(u32) })!, null));
        // Writes
        Memory.RAM.WriteF32 = DelegateHelper.Cast<WriteF32_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("WriteF32", new Type[] { typeof(u32), typeof(f32) })!, null));
        Memory.RAM.WriteF64 = DelegateHelper.Cast<WriteF64_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("WriteF64", new Type[] { typeof(u32), typeof(f64) })!, null));
        Memory.RAM.WriteS16 = DelegateHelper.Cast<WriteS16_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("WriteS16", new Type[] { typeof(u32), typeof(s16) })!, null));
        Memory.RAM.WriteS32 = DelegateHelper.Cast<WriteS32_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("WriteS32", new Type[] { typeof(u32), typeof(s32) })!, null));
        Memory.RAM.WriteS64 = DelegateHelper.Cast<WriteS64_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("WriteS64", new Type[] { typeof(u32), typeof(s64) })!, null));
        Memory.RAM.WriteS8 = DelegateHelper.Cast<WriteS8_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("WriteS8", new Type[] { typeof(u32), typeof(s8) })!, null));
        Memory.RAM.WriteU16 = DelegateHelper.Cast<WriteU16_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("WriteU16", new Type[] { typeof(u32), typeof(u16) })!, null));
        Memory.RAM.WriteU32 = DelegateHelper.Cast<WriteU32_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("WriteU32", new Type[] { typeof(u32), typeof(u32) })!, null));
        Memory.RAM.WriteU64 = DelegateHelper.Cast<WriteU64_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("WriteU64", new Type[] { typeof(u32), typeof(u64) })!, null));
        Memory.RAM.WriteU8 = DelegateHelper.Cast<WriteU8_Delegate>(DelegateHelper.CreateDelegate(boundClass.GetRuntimeMethod("WriteU8", new Type[] { typeof(u32), typeof(u8) })!, null));
        var end = DateTimeOffset.Now.ToUnixTimeMilliseconds();
        var diff = end - start;
        Console.WriteLine($"Delegate generation took {diff}ms.");
    }
}

public class MemoryAccess
{

    public delegate f32 ReadF32_Delegate(u32 address);
    public delegate f64 ReadF64_Delegate(u32 address);
    public delegate s16 ReadS16_Delegate(u32 address);
    public delegate s32 ReadS32_Delegate(u32 address);
    public delegate long ReadS64_Delegate(u32 address);
    public delegate s8 ReadS8_Delegate(u32 address);
    public delegate u16 ReadU16_Delegate(u32 address);
    public delegate u32 ReadU32_Delegate(u32 address);
    public delegate ulong ReadU64_Delegate(u32 address);
    public delegate byte ReadU8_Delegate(u32 address);
    //
    public delegate void WriteF32_Delegate(u32 address, f32 value);
    public delegate void WriteF64_Delegate(u32 address, f64 value);
    public delegate void WriteS16_Delegate(u32 address, s16 value);
    public delegate void WriteS32_Delegate(u32 address, s32 value);
    public delegate void WriteS64_Delegate(u32 address, long value);
    public delegate void WriteS8_Delegate(u32 address, s8 value);
    public delegate void WriteU16_Delegate(u32 address, u16 value);
    public delegate void WriteU32_Delegate(u32 address, u32 value);
    public delegate void WriteU64_Delegate(u32 address, ulong value);
    public delegate void WriteU8_Delegate(u32 address, byte value);

    public ReadF32_Delegate ReadF32;
    public ReadF64_Delegate ReadF64;
    public ReadS16_Delegate ReadS16;
    public ReadS32_Delegate ReadS32;
    public ReadS64_Delegate ReadS64;
    public ReadS8_Delegate ReadS8;
    public ReadU16_Delegate ReadU16;
    public ReadU32_Delegate ReadU32;
    public ReadU64_Delegate ReadU64;
    public ReadU8_Delegate ReadU8;
    //
    public WriteF32_Delegate WriteF32;
    public WriteF64_Delegate WriteF64;
    public WriteS16_Delegate WriteS16;
    public WriteS32_Delegate WriteS32;
    public WriteS64_Delegate WriteS64;
    public WriteS8_Delegate WriteS8;
    public WriteU16_Delegate WriteU16;
    public WriteU32_Delegate WriteU32;
    public WriteU64_Delegate WriteU64;
    public WriteU8_Delegate WriteU8;
}

public static class Memory
{
    public static readonly MemoryAccess RAM = new MemoryAccess();
}

[AttributeUsage(AttributeTargets.Class)]
public class BoundMemoryAttribute : Attribute
{
    public BoundMemoryAttribute() { }
}