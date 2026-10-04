#pragma once

#include "base.h"

class Sha1 {
public:
    void Update(const void* data, u64 size);
    
    void Update(std::string_view text) {
        Update(text.data(), text.size());
    }
    
    void Update_Field(std::string_view text);
    static std::string Digest(std::span<const u8> bytes);
    std::string Hex_Digest();

private:
    void Block(const u8* data);

    u32 hash[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    u64 length = 0;
    u8 block[64] = {};
    u32 blockUsed = 0;
};
