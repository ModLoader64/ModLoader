#include "network/packets.h"

#include "modloader_bytes.h"

#include <string.h>

namespace {
constexpr u32 gPacketMagic = 0x31504c4d;
} // namespace

bool Packet_Encode(std::string_view type, u32 version, std::span<const u8> payload, std::vector<u8>& out) {
    if (!Text_Is_Valid(type, 64) || payload.size() > gPacketMaxPayload) {
        return false;
    }

    out.resize(gPacketHeaderSize + type.size() + payload.size());
    ModLoader::Bytes::Write_Le32(out.data(), gPacketMagic);
    ModLoader::Bytes::Write_Le32(out.data() + 4, version);
    ModLoader::Bytes::Write_Le32(out.data() + 8, static_cast<u32>(type.size()));
    ModLoader::Bytes::Write_Le32(out.data() + 12, static_cast<u32>(payload.size()));
    memcpy(out.data() + gPacketHeaderSize, type.data(), type.size());
    if (!payload.empty()) {
        memcpy(out.data() + gPacketHeaderSize + type.size(), payload.data(), payload.size());
    }

    return true;
}

Packet_Result Packet_Decode(std::span<const u8> bytes, Packet_View& out, u64& out_size) {
    if (bytes.size() < gPacketHeaderSize) {
        return Packet_Result::Incomplete;
    }

    u32 type_size = ModLoader::Bytes::Read_Le32(bytes.data() + 8);
    u32 payload_size = ModLoader::Bytes::Read_Le32(bytes.data() + 12);
    if (ModLoader::Bytes::Read_Le32(bytes.data()) != gPacketMagic || type_size == 0 || type_size > 64 || payload_size > gPacketMaxPayload) {
        return Packet_Result::Invalid;
    }

    out_size = gPacketHeaderSize + type_size + payload_size;
    if (bytes.size() < out_size) {
        return Packet_Result::Incomplete;
    }
    
    out.type = { reinterpret_cast<const char*>(bytes.data() + gPacketHeaderSize), type_size };
    out.version = ModLoader::Bytes::Read_Le32(bytes.data() + 4);
    out.payload = bytes.subspan(gPacketHeaderSize + type_size, payload_size);
    return Text_Is_Valid(out.type, 64) ? Packet_Result::Complete : Packet_Result::Invalid;
}
