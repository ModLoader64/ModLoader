namespace ModLoader.API;

public interface IMemory
{
    /// <summary>
    /// Read a u328.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract u8 ReadU8(u64 address);

    /// <summary>
    /// Read a u3216.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract u16 ReadU16(u64 address);

    /// <summary>
    /// Read a u3232.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract u32 ReadU32(u64 address);

    /// <summary>
    /// Read a u3264.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract u64 ReadU64(u64 address);

    /// <summary>
    /// Read a SInt8
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract s8 ReadS8(u64 address);

    /// <summary>
    /// Read a SInt16
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract s16 ReadS16(u64 address);

    /// <summary>
    /// Read a SInt32
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract s32 ReadS32(u64 address);

    /// <summary>
    /// Read a SInt64.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract s64 ReadS64(u64 address);

    /// <summary>
    /// Read a f32 32.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract f32 ReadF32(u64 address);

    /// <summary>
    /// Read a f32 64.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public static abstract f64 ReadF64(u64 address);

    /// <summary>
    /// Write a u328.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteU8(u64 address, u8 value);

    /// <summary>
    /// Write a u3216.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteU16(u64 address, u16 value);

    /// <summary>
    /// Write a u3232.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteU32(u64 address, u32 value);

    /// <summary>
    /// Write a u3264.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteU64(u64 address, u64 value);

    /// <summary>
    /// Write a SInt8.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteS8(u64 address, s8 value);

    /// <summary>
    /// Write a SInt16.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteS16(u64 address, s16 value);

    /// <summary>
    /// Write a SInt32.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteS32(u64 address, s32 value);

    /// <summary>
    /// Write a SInt64.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteS64(u64 address, s64 value);

    /// <summary>
    /// Write a f32 32.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteF32(u64 address, f32 value);

    /// <summary>
    /// Write a f32 64.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public static abstract void WriteF64(u64 address, f64 value);

    public static abstract void InvalidateCachedCode();

}

public static class Memory
{
    // Is there another way to avoid this rule warning without having to pass instances of this shit around?
    public static IMemoryDelegates RAM;
    public static IMemoryDelegates ROM;
}

[AttributeUsage(AttributeTargets.Class)]
public class BoundMemoryAttribute : Attribute
{
}

[AttributeUsage(AttributeTargets.Class)]
public class BoundROMAttribute : Attribute
{
}