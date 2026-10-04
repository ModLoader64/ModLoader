#pragma once

#include <modloader/types.h>
#include <modloader/size_literals.h>
#include <modloader_net.h>

#include <span>
#include <stdlib.h>
#include <string_view>
#include <type_traits>

namespace ModLoader::Net {

using namespace size_literals;

constexpr s64 gClosed = 0;
constexpr s64 gError = -1;
constexpr s64 gTimedOut = -2;
constexpr u64 gMaxPacketSize = 8_MiB;
constexpr u64 gMaxLobbyPacketSize = gMaxPacketSize - 16; // managed packet envelope
constexpr u64 gMaxLobbyUdpPacketSize = MODLOADER_LOBBY_MAX_UDP_MESSAGE - 16;

enum class Transport : u32 {
    Tcp = MODLOADER_NET_TCP,
    Udp = MODLOADER_NET_UDP,
};

struct Address {
    u32 ipv4;
    u16 port;
};

// Name and version identify the packet; typed handlers also require exactly sizeof(T) bytes
template<typename T>
struct Packet_Type {
    static_assert(std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T> &&
        std::is_trivially_copy_constructible_v<T>, "packet records must be standard-layout and trivially copyable");
    static_assert(sizeof(T) <= gMaxPacketSize, "packet record exceeds the transport limit");

    std::string_view name;
    u32 version = 1;
};

namespace Detail {

struct Socket_Access;

template<typename T, typename Sender>
bool Send_Value(const T* value, Sender send) {
    if (value == nullptr) {
        return false;
    }

    T* copy = static_cast<T*>(aligned_alloc(alignof(T), sizeof(T)));
    if (copy == nullptr) {
        return false;
    }

    __builtin_memcpy(copy, value, sizeof(T));
    __builtin_clear_padding(copy);
    bool sent = send(std::span<const u8>(reinterpret_cast<const u8*>(copy), sizeof(T)));
    free(copy);
    return sent;
}

} // namespace Detail

} // namespace ModLoader::Net
