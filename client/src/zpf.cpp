#include "patch.h"

#include <string.h>
#include <zlib.h>

// Interpreted from OOTR's N64Patch.py

namespace {

constexpr usize gCompressedSize = 0x2000000;
constexpr usize gDecompressedSize = 0x4000000;
constexpr usize gTable = 0x7430; // DMA table, NTSC 1.0
constexpr u8 gCompressedCrc[] = { 0xEC, 0x70, 0x11, 0xB7, 0x76, 0x16, 0xD7, 0x2B }; // 1.0 US
constexpr u8 gDecompressedCrc[] = { 0x93, 0x52, 0x2E, 0x7B, 0xE5, 0x06, 0xD4, 0x27 };
constexpr usize gMaxPatch = 64u * 1024 * 1024;

u32 Read32(const u8* bytes) {
    return static_cast<u32>(bytes[0]) << 24 | bytes[1] << 16 | bytes[2] << 8 | bytes[3];
}

void Write32(u8* bytes, u32 value) {
    bytes[0] = static_cast<u8>(value >> 24);
    bytes[1] = static_cast<u8>(value >> 16);
    bytes[2] = static_cast<u8>(value >> 8);
    bytes[3] = static_cast<u8>(value);
}

void Update_Crc(std::vector<u8>& rom) {
    u32 t1 = 0xDF26F436;
    u32 t2 = t1;
    u32 t3 = t1;
    u32 t4 = t1;
    u32 t5 = t1;
    u32 t6 = t1;

    for (u32 offset = 0; offset < 0x100000; offset += 4) {
        u32 word = Read32(&rom[0x1000 + offset]);
        u32 rotated = word << (word & 31) | word >> ((32 - (word & 31)) & 31);

        t4 += t6 + word < t6;
        t6 += word;
        t3 ^= word;
        t5 += rotated;
        t2 ^= t2 > word ? rotated : t6 ^ word;
        t1 += Read32(&rom[0x750 + (offset & 0xFF)]) ^ word;
    }

    Write32(&rom[0x10], t6 ^ t4 ^ t3);
    Write32(&rom[0x14], t5 ^ t2 ^ t1);
}

bool Yaz0(std::span<const u8> source, std::span<u8> out) {
    usize read = 0x10;
    usize written = 0;
    u32 code = 0;

    for (u32 bits = 0; written < out.size(); bits--, code <<= 1) {
        if (bits == 0) {
            if (read >= source.size()) {
                return false;
            }
            code = source[read++];
            bits = 8;
        }

        if (read + 3 > source.size()) {
            return false;
        }

        if ((code & 0x80) != 0) {
            out[written++] = source[read++];
            continue;
        }

        u32 distance = ((source[read] & 0xF) << 8 | source[read + 1]) + 1;
        u32 count = source[read] >> 4;
        read += 2;
        count = count == 0 ? source[read++] + 0x12 : count + 2;
        if (distance > written || count > out.size() - written) {
            return false;
        }
        
        for (; count != 0; count--, written++) {
            out[written] = out[written - distance];
        }
    }
    return true;
}

bool Decompress(std::vector<u8>& rom) {
    std::vector<u8> out = rom;
    u32 table_end = Read32(&rom[gTable + 0x24]);

    if (table_end <= gTable || table_end > rom.size()) {
        return false;
    }

    out.resize(gDecompressedSize);
    memset(&out[table_end], 0, gDecompressedSize - table_end);
    for (usize entry = gTable + 0x30; entry < table_end; entry += 16) {
        u32 start = Read32(&rom[entry]);
        u32 end = Read32(&rom[entry + 4]);
        u32 from = Read32(&rom[entry + 8]);
        u32 from_end = Read32(&rom[entry + 12]);
        if (from >= gDecompressedSize || from_end == UINT32_MAX) {
            continue;
        }

        if (end < start || end > gDecompressedSize || from > rom.size() || (from_end == 0 && end - start > rom.size() - from)) {
            return false;
        }

        if (from_end == 0) {
            memcpy(&out[start], &rom[from], end - start);
        }
        else if (!Yaz0(std::span(rom).subspan(from), std::span(out).subspan(start, end - start))) {
            return false;
        }

        Write32(&out[entry + 8], start);
        Write32(&out[entry + 12], 0);
    }

    Update_Crc(out);
    rom = std::move(out);
    return true;
}

struct Reader {
    std::span<const u8> data;
    usize position = 0;
    bool failed = false;

    u32 Number(u32 size) {
        u32 number = 0;

        if (size > data.size() - position) {
            failed = true;
            return 0;
        }

        for (; size != 0; size--) {
            number = number << 8 | data[position++];
        }
        return number;
    }
};

struct Key {
    const std::vector<u8>& original;
    u32 first;
    u32 last;
    u32 address;

    // get the next XOR key, skipping 0s
    u8 Next() {
        for (u32 tries = 0; tries <= last - first; tries++) {
            address = address >= last ? first : address + 1;
            if (original[address] != 0) {
                return original[address];
            }
        }
        return 0;
    }
};

} // namespace

bool Zpf_Apply(std::span<const u8> patch, std::vector<u8>& image, u64 limit) {
    bool compressed = image.size() == gCompressedSize && memcmp(&image[0x10], gCompressedCrc, 8) == 0;

    if (limit < gDecompressedSize || (!compressed && (image.size() != gDecompressedSize || memcmp(&image[0x10], gDecompressedCrc, 8) != 0))) {
        return false;
    }

    // load the patch file and decompress; make sure the header is correct
    std::vector<u8> inflated(gMaxPatch);
    uLongf inflated_size = gMaxPatch;
    if (uncompress(inflated.data(), &inflated_size, patch.data(), static_cast<uLong>(patch.size())) != Z_OK ||
        inflated_size < 21 || memcmp(inflated.data(), "ZPFv1", 5) != 0) {
        return false;
    }

    std::vector<u8> original = image;
    if (compressed && (!Decompress(original) || memcmp(&original[0x10], gDecompressedCrc, 8) != 0)) {
        return false;
    }

    // load the patch configuration info
    Reader reader = { std::span(inflated).first(inflated_size), 5 };
    u32 table = reader.Number(4);
    Key key = { original, reader.Number(4), reader.Number(4), reader.Number(4) };
    if (key.first > key.last || key.last >= gDecompressedSize || key.address > key.last) {
        return false;
    }

    // load all the DMA table updates; terminated with 0xFFFF
    std::vector<u8> rom = original;
    u32 table_end = Read32(&original[gTable + 0x24]);
    for (u32 index; (index = reader.Number(2)) != 0xFFFF;) {
        u32 from = reader.Number(4);
        u32 start = reader.Number(4);
        u32 size = reader.Number(3);
        u64 entry = table + static_cast<u64>(index) * 16;
        u32 copied = 0;
        if (reader.failed || entry + 16 > gDecompressedSize || start > gDecompressedSize || size > gDecompressedSize - start) {
            return false;
        }

        // save new DMA table entry
        Write32(&rom[entry], start);
        Write32(&rom[entry + 4], start + size);
        Write32(&rom[entry + 8], start);
        Write32(&rom[entry + 12], 0);

        // if a source file is listed, copy from there; a new file is filled with 0s
        if (from != UINT32_MAX) {
            u32 found = 0;
            for (u32 other = gTable; found == 0 && other + 16 <= table_end && other + 16 <= gDecompressedSize; other += 16) {
                found = Read32(&original[other]) == from ? other : 0;
            }

            u32 end = found != 0 ? Read32(&original[found + 4]) : 0;
            if (end < from || end > gDecompressedSize) {
                return false;
            }

            copied = std::min(size, end - from);
            memcpy(&rom[start], &original[from], copied);
        }
        memset(&rom[start + copied], 0, size - copied);
    }

    // read in the XOR data blocks, to the end of the file
    for (u32 address = 0; reader.position < reader.data.size();) {
        // Continue writing from previous block, skipping XOR keys, or start a new block
        if (reader.data[reader.position] == 0xFF) {
            reader.position++;
            for (u32 skip = reader.Number(1); skip != 0; skip--) {
                key.Next();
            }
        }
        else {
            address = reader.Number(4);
        }

        u32 size = reader.Number(2);
        if (reader.failed || size > reader.data.size() - reader.position || address > gDecompressedSize || size > gDecompressedSize - address) {
            return false;
        }

        // keep 0s as 0s
        for (; size != 0; size--, address++) {
            u8 byte = reader.data[reader.position++];
            rom[address] = byte != 0 ? byte ^ key.Next() : 0;
        }
    }

    // nop the branches of the anti-piracy check of PI DMA timings
    for (u32 address : { 0x6CFC, 0x6D0C, 0x6D18, 0x6D24, 0x6D2C }) {
        Write32(&rom[address], 0);
    }

    Update_Crc(rom);
    image = std::move(rom);
    return true;
}
