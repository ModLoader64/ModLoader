using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace ModLoader.API;

//// <summary>
/// Wrapper class for helping with accessing generic objects in emulated memory
/// </summary>
public class MemoryObject
{
    /// <summary>
    /// Pointer to object
    /// </summary>
    [System.Text.Json.Serialization.JsonIgnoreAttribute()]
    public u32 Pointer;

    /// <summary>
    /// Real size of object
    /// </summary>
    internal u32 _Size = 0;

    /// <summary>
    /// Size of object (if applicable)
    /// </summary>
    public u32 Size {
        get { return _Size; }
        set { _Size = value; }
    }

    /// <summary>
    /// Constructor
    /// </summary>
    /// <param name="pointer">Pointer to object</param>
    public MemoryObject(u32 pointer)
    {
        Pointer = pointer;
    }

    /// <summary>
    /// Constructor
    /// </summary>
    /// <param name="pointer">Pointer to object</param>
    /// <param name="size">Size of object</param>
    public MemoryObject(u32 pointer, u32 size)
    {
        Pointer = pointer;
        Size = size;
    }

    // MEMORY FUNCTIONS (TODO: bounds checking if size != 0)

    /// <summary>
    /// Read an unsigned byte from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to read</param>
    /// <returns>Unsigned byte that was read</returns>
    public u8 ReadU8(u32 offset) => Memory.RAM.ReadU8(Pointer + offset);

    /// <summary>
    /// Read an unsigned short from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to read</param>
    /// <returns>Unsigned short that was read</returns>
    public u16 ReadU16(u32 offset) => Memory.RAM.ReadU16(Pointer + offset);

    /// <summary>
    /// Read an unsigned word from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to read</param>
    /// <returns>Unsigned word that was read</returns>
    public u32 ReadU32(u32 offset) => Memory.RAM.ReadU32(Pointer + offset);

    /// <summary>
    /// Read an unsigned long from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to read</param>
    /// <returns>Unsigned long that was read</returns>
    public u64 ReadU64(u32 offset) => Memory.RAM.ReadU64(Pointer + offset);

    /// <summary>
    /// Read an signed byte from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to read</param>
    /// <returns>Signed byte that was read</returns>
    public s8 ReadS8(u32 offset) => Memory.RAM.ReadS8(Pointer + offset);

    /// <summary>
    /// Read an signed short from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to read</param>
    /// <returns>Signed short that was read</returns>
    public s16 ReadS16(u32 offset) => Memory.RAM.ReadS16(Pointer + offset);

    /// <summary>
    /// Read an signed word from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to read</param>
    /// <returns>Signed word that was read</returns>
    public s32 ReadS32(u32 offset) => Memory.RAM.ReadS32(Pointer + offset);

    /// <summary>
    /// Read an signed long from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to read</param>
    /// <returns>Signed long that was read</returns>
    public s64 ReadS64(u32 offset) => Memory.RAM.ReadS64(Pointer + offset);

    /// <summary>
    /// Read a 32bit float from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to read</param>
    /// <returns> float that was read</returns>
    public f32 ReadF32(u32 offset) => Memory.RAM.ReadF32(Pointer + offset);

    /// <summary>
    /// Read a 64bit float from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to read</param>
    /// <returns> float that was read</returns>
    public f64 ReadF64(u32 offset) => Memory.RAM.ReadF64(Pointer + offset);

    /// <summary>
    /// Write a unsigned byte from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to write</param>
    /// <param name="value">Value to write</param>
    public void WriteU8(u32 offset, u8 value) => Memory.RAM.WriteU8(Pointer + offset, value);

    /// <summary>
    /// Write a unsigned short from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to write</param>
    /// <param name="value">Value to write</param>
    public void WriteU16(u32 offset, u16 value) => Memory.RAM.WriteU16(Pointer + offset, value);

    /// <summary>
    /// Write a unsigned word from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to write</param>
    /// <param name="value">Value to write</param>
    public void WriteU32(u32 offset, u32 value) => Memory.RAM.WriteU32(Pointer + offset, value);

    /// <summary>
    /// Write a unsigned long from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to write</param>
    /// <param name="value">Value to write</param>
    public void WriteU64(u32 offset, u64 value) => Memory.RAM.WriteU64(Pointer + offset, value);

    /// <summary>
    /// Write a signed byte from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to write</param>
    /// <param name="value">Value to write</param>
    public void WriteS8(u32 offset, s8 value) => Memory.RAM.WriteS8(Pointer + offset, value);

    /// <summary>
    /// Write a signed short from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to write</param>
    /// <param name="value">Value to write</param>
    public void WriteS16(u32 offset, s16 value) => Memory.RAM.WriteS16(Pointer + offset, value);

    /// <summary>
    /// Write a signed word from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to write</param>
    /// <param name="value">Value to write</param>
    public void WriteS32(u32 offset, s32 value) => Memory.RAM.WriteS32(Pointer + offset, value);

    /// <summary>
    /// Write a signed long from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to write</param>
    /// <param name="value">Value to write</param>
    public void WriteS64(u32 offset, s64 value) => Memory.RAM.WriteS64(Pointer + offset, value);

    /// <summary>
    /// Write a 32bit float from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to write</param>
    /// <param name="value">Value to write</param>
    public void WriteF32(u32 offset, f32 value) => Memory.RAM.WriteF32(Pointer + offset, value);

    /// <summary>
    /// Write a 64bit float from within the object
    /// </summary>
    /// <param name="offset">Offset within the object to write</param>
    /// <param name="value">Value to write</param>
    public void WriteF64(u32 offset, f64 value) => Memory.RAM.WriteF64(Pointer + offset, value);
}
