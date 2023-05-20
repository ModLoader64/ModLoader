namespace ModLoader.API;

public static class MemoryAccess
{
    public static Memory ram { get; set; }
}
public interface Memory
{
    /// <summary>
    /// Read a UInt8.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public u8 ReadU8(Ptr address);

    /// <summary>
    /// Read a UInt16.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public u16 ReadU16(Ptr address);

    /// <summary>
    /// Read a UInt32.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public u32 ReadU32(Ptr address);

    /// <summary>
    /// Read a UInt64.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public u64 ReadU64(Ptr address);

    /// <summary>
    /// Read a SInt8
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public s8 ReadS8(Ptr address);

    /// <summary>
    /// Read a SInt16
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public s16 ReadS16(Ptr address);

    /// <summary>
    /// Read a SInt32
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public s32 ReadS32(Ptr address);

    /// <summary>
    /// Read a SInt64.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public s64 ReadS64(Ptr address);

    /// <summary>
    /// Read a float 32.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public f32 ReadF32(Ptr address);

    /// <summary>
    /// Read a float 64.
    /// </summary>
    /// <param name="address"></param>
    /// <returns></returns>
    public f64 ReadF64(Ptr address);

    /// <summary>
    /// Write a UInt8.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public void WriteU8(Ptr address, u8 value);

    /// <summary>
    /// Write a UInt16.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public void WriteU16(Ptr address, u16 value);

    /// <summary>
    /// Write a UInt32.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public void WriteU32(Ptr address, u32 value);

    /// <summary>
    /// Write a UInt64.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public void WriteU64(Ptr address, u64 value);

    /// <summary>
    /// Write a SInt8.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public void WriteS8(Ptr address, s8 value);

    /// <summary>
    /// Write a SInt16.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public void WriteS16(Ptr address, s16 value);

    /// <summary>
    /// Write a SInt32.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public void WriteS32(Ptr address, s32 value);

    /// <summary>
    /// Write a SInt64.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public void WriteS64(Ptr address, s64 value);

    /// <summary>
    /// Write a float 32.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public void WriteF32(Ptr address, f32 value);

    /// <summary>
    /// Write a float 64.
    /// </summary>
    /// <param name="address"></param>
    /// <param name="value"></param>
    public void WriteF64(Ptr address, f64 value);

}
