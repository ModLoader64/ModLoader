#pragma once

#include "base.h"

constexpr u64 gPacketHeaderSize = 16;
constexpr u64 gPacketMaxPayload = 8ull * 1024 * 1024;
constexpr u64 gPacketMaxQueued = 16ull * 1024 * 1024;
constexpr u64 gPacketMaxDatagram = 65507;

struct Packet_View {
    std::string_view type;
    u32 version;
    std::span<const u8> payload;
};

enum class Packet_Result {
    Incomplete,
    Complete,
    Invalid,
};

bool Packet_Encode(std::string_view type, u32 version, std::span<const u8> payload, std::vector<u8>& out);
Packet_Result Packet_Decode(std::span<const u8> bytes, Packet_View& out, u64& out_size);
