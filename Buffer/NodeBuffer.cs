using System.Collections;

namespace NodeBuffer;

public class Buffer
{

    public byte[] _buffer;
    private Memory<byte> _memory;
    public readonly int Size;

    public Buffer(int size) { 
        _buffer = new byte[size];
        _memory = new Memory<byte>(_buffer);
        Size = size;
    }

    public Buffer(byte[] bytes)
    {
        _buffer = new byte[bytes.Length];
        _memory = new Memory<byte>(_buffer);
        Size = bytes.Length;

        bytes.CopyTo(_buffer, 0);
    }

    public Buffer(uint[] words)
    {
        _buffer = new byte[words.Length * 4];
        _memory = new Memory<byte>(_buffer);
        Size = _buffer.Length;


        for (int i = 0; i < words.Length; i++)
        {
            WriteBytes(i * 4, BitConverter.GetBytes(words[i]));
        }
    }

    public void Clear()
    {
        for (int i = 0; i < _buffer.Length; i++)
        {
            _buffer[i] = 0;
        }
    }

    public void Resize(int newSize) {
        if (newSize <= _buffer.Length)
        {
            Console.WriteLine($"Failed to resize buffer! Invalid Length.");
            return;
        }
        byte[] nBuffer = new byte[newSize];
        _buffer.CopyTo(nBuffer, 0);
        _buffer = nBuffer;
        _memory = new Memory<byte>(_buffer);
    }

    public void WriteBuffer(int offset, Buffer buf) {
        WriteBytes(offset, buf.CopyBytes(0, buf.Size));
    }

    public void WriteBytes(int offset, byte[] bytes) {
        bytes.CopyTo(_buffer, offset);
    }

    public byte[] CopyBytes(int offset, int size) {
        return _memory.Slice(offset, size).ToArray();
    }

    public Span<byte> Slice(int offset, int size) {
        return _memory.Slice(offset, size).Span;
    }

    public void WriteNibbleHi(int offset, byte data) {

        byte source = ReadU8(offset);
        byte loNybble = ((byte)(source & 0x0F));      //Right hand nybble = D

        byte writeHi = (byte)((data & 0xF0) >> 4);

        source = (byte)(source & 0xF0 + loNybble);        //Update only low four bits
        source = (byte)(source & 0x0F + (writeHi << 4)); //Update only high bits

        WriteU8(offset, source);
    }

    public void WriteNibbleLo(int offset, byte data)
    {

        byte source = ReadU8(offset);
        byte hiNybble = (byte)((source & 0xF0) >> 4); //Left hand nybble = A

        byte writeLo = ((byte)(data & 0x0F));

        source = (byte)(source & 0xF0 + writeLo);        //Update only low four bits
        source = (byte)(source & 0x0F + (hiNybble << 4)); //Update only high bits

        WriteU8(offset, source);
    }

    public byte ReadNibbleHi(int offset)
    {
        byte source = ReadU8(offset);
        byte hiNybble = (byte)((source & 0xF0) >> 4); //Left hand nybble = A

        return hiNybble;
    }

    public byte ReadNibbleLo(int offset)
    {
        byte source = ReadU8(offset);
        byte loNybble = ((byte)(source & 0x0F));      //Right hand nybble = D

        return loNybble;
    }

    public void WriteU8(int offset, byte data)
    {
        _buffer[offset] = data;
    }

    public void WriteU16(int offset, ushort data)
    {
        WriteBytes(offset, BitConverter.GetBytes(data));
    }

    public void WriteU32(int offset, uint data) {
        WriteBytes(offset, BitConverter.GetBytes(data));
    }

    public void WriteU64(int offset, ulong data) {
        WriteBytes(offset, BitConverter.GetBytes(data));
    }

    public void WriteU8BE(int offset, byte data)
    {
        _buffer[offset] = data;
    }

    public void WriteU16BE(int offset, ushort data)
    {
        WriteBytes(offset, BitConverter.GetBytes(data).Reverse().ToArray());
    }

    public void WriteU32BE(int offset, uint data)
    {
        WriteBytes(offset, BitConverter.GetBytes(data).Reverse().ToArray());
    }

    public void WriteU64BE(int offset, ulong data)
    {
        WriteBytes(offset, BitConverter.GetBytes(data).Reverse().ToArray());
    }

    public void Write8(int offset, sbyte data) {
        _buffer[offset] = (byte)data;
    }

    public void Write16(int offset, short data) {
        WriteBytes(offset, BitConverter.GetBytes(data));
    }

    public void Write32(int offset, int data) {
        WriteBytes(offset, BitConverter.GetBytes(data));
    }

    public void Write64(int offset, long data) {
        WriteBytes(offset, BitConverter.GetBytes(data));
    }

    public void Write8BE(int offset, sbyte data)
    {
        _buffer[offset] = (byte)data;
    }

    public void Write16BE(int offset, short data)
    {
        WriteBytes(offset, BitConverter.GetBytes(data).Reverse().ToArray());
    }

    public void Write32BE(int offset, int data)
    {
        WriteBytes(offset, BitConverter.GetBytes(data).Reverse().ToArray());
    }

    public void Write64BE(int offset, long data)
    {
        WriteBytes(offset, BitConverter.GetBytes(data).Reverse().ToArray());
    }

    public byte ReadU8(int offset) {
        return _buffer[offset];
    }

    public ushort ReadU16(int offset)
    {
        return BitConverter.ToUInt16(_buffer, offset);
    }

    public uint ReadU32(int offset) {
        return BitConverter.ToUInt32(_buffer, offset);
    }

    public ulong ReadU64(int offset)
    {
        return BitConverter.ToUInt64(_buffer, offset);
    }

    public byte ReadU8BE(int offset)
    {
        return _buffer[offset];
    }

    public ushort ReadU16BE(int offset)
    {
        return BitConverter.ToUInt16(CopyBytes(offset, 2).Reverse().ToArray(), 0);
    }

    public uint ReadU32BE(int offset)
    {
        return BitConverter.ToUInt32(CopyBytes(offset, 4).Reverse().ToArray(), 0);
    }

    public ulong ReadU64BE(int offset)
    {
        return BitConverter.ToUInt64(CopyBytes(offset, 8).Reverse().ToArray(), 0);
    }

    public sbyte Read8(int offset) {
        return (sbyte)_buffer[offset];
    }

    public short Read16(int offset)
    {
        return BitConverter.ToInt16(_buffer, offset);
    }

    public int Read32(int offset) {
        return BitConverter.ToInt32(_buffer, offset);
    }

    public long Read64(int offset) {
        return BitConverter.ToInt64(_buffer, offset);
    }

    // Bit level functions.

    public BitArray ReadBits(int offset, int numofbytes)
    {
        return new BitArray(CopyBytes(offset, numofbytes));
    }

    public BitArray ReadBits8(int offset) {
        return ReadBits(offset, 1);
    }

    public BitArray ReadBits16(int offset) {
        return ReadBits(offset, 2);
    }

    public BitArray ReadBits32(int offset)
    {
        return ReadBits(offset, 4);
    }

    public BitArray ReadBits64(int offset)
    {
        return ReadBits(offset, 8);
    }

    public void WriteBits(int offset, BitArray bits) {
        byte[] ret = new byte[(bits.Length - 1) / 8 + 1];
        bits.CopyTo(ret, 0);
        WriteBytes(offset, ret);
    }

}
