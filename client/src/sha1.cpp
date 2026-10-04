#include "sha1.h"
#include <modloader_bytes.h>

#include <bit>
#include <string.h>

void Sha1::Block(const u8* data) {
    u32 words[80];
    u32 a;
    u32 b;
    u32 c;
    u32 d;
    u32 e;

    for (u32 index = 0; index < 16; index++) {
        words[index] = ModLoader::Bytes::Read_Be32(data + index * 4);
    }

    for (u32 index = 16; index < 80; index++) {
        words[index] = std::rotl(words[index - 3] ^ words[index - 8] ^ words[index - 14] ^ words[index - 16], 1);
    }

    a = hash[0];
    b = hash[1];
    c = hash[2];
    d = hash[3];
    e = hash[4];
    for (u32 index = 0; index < 80; index++) {
        u32 mix;
        u32 constant;
        u32 next;

        if (index < 20) {
            mix = (b & c) | (~b & d);
            constant = 0x5A827999;
        }
        else if (index < 40) {
            mix = b ^ c ^ d;
            constant = 0x6ED9EBA1;
        }
        else if (index < 60) {
            mix = (b & c) | (b & d) | (c & d);
            constant = 0x8F1BBCDC;
        }
        else {
            mix = b ^ c ^ d;
            constant = 0xCA62C1D6;
        }

        next = std::rotl(a, 5) + mix + e + constant + words[index];
        e = d;
        d = c;
        c = std::rotl(b, 30);
        b = a;
        a = next;
    }

    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
}

void Sha1::Update(const void* data, u64 size) {
    const u8* bytes = static_cast<const u8*>(data);
    u32 take;

    length += size;
    while (size > 0) {
        take = size < 64 - blockUsed ? static_cast<u32>(size) : 64 - blockUsed;
        memcpy(block + blockUsed, bytes, take);
        blockUsed += take;
        bytes += take;
        size -= take;
        if (blockUsed == 64) {
            Block(block);
            blockUsed = 0;
        }
    }
}

void Sha1::Update_Field(std::string_view text) {
    Update(Text_Format("%llu:", static_cast<unsigned long long>(text.size())));
    Update(text);
}

std::string Sha1::Digest(std::span<const u8> bytes) {
    Sha1 hash;
    hash.Update(bytes.data(), bytes.size());
    return hash.Hex_Digest();
}

std::string Sha1::Hex_Digest() {
    u64 bit_length = length * 8;
    u8 padding = 0x80;
    u8 zero = 0;
    u8 length_bytes[8];
    u8 digest[sizeof(hash)];
    std::string text(sizeof(hash) * 2, '\0');

    Update(&padding, 1);
    while (blockUsed != 56) {
        Update(&zero, 1);
    }
    
    ModLoader::Bytes::Write_Be64(length_bytes, bit_length);
    Update(length_bytes, 8);
    ModLoader::Bytes::Write_Be32(digest, hash, 5);
    ModLoader::Bytes::Hex_Encode(digest, sizeof(digest), text.data());
    return text;
}
