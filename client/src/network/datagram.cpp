#include "network/protocol.h"

#include <modloader_bytes.h>
#include <monocypher.h>
#include <cstring>

using namespace ModLoader::Bytes;

void Datagram_Channel::Initialize(const Secure_Channel& channel) {
    constexpr u8 label[] = "ModLoader UDP";
    *this = {};
    crypto_blake2b_keyed(sendKey, sizeof(sendKey), channel.sendKey, sizeof(channel.sendKey), label, sizeof(label));
    crypto_blake2b_keyed(receiveKey, sizeof(receiveKey), channel.receiveKey, sizeof(channel.receiveKey), label, sizeof(label));
}

bool Datagram_Channel::Seal(const Member_Id& client, std::span<const u8> plaintext, std::vector<u8>& out) {
    if (plaintext.size() > gMaxDatagram - gDatagramOverhead || sendCount == UINT64_MAX) {
        return false;
    }
    Datagram_Header header{client, {}};
    u8 nonce[gNonceSize] = {};
    Write_Le64(header.sequence, sendCount);
    Write_Le64(nonce, sendCount++);
    out.resize(sizeof(header) + plaintext.size() + gTagSize);
    memcpy(out.data(), &header, sizeof(header));
    u8* payload = out.data() + sizeof(header);
    crypto_aead_lock(payload, payload + plaintext.size(), sendKey, nonce,
        out.data(), sizeof(header), plaintext.data(), plaintext.size());
    return true;
}

bool Datagram_Channel::Open(std::span<u8> datagram) {
    if (datagram.size() < gDatagramOverhead || datagram.size() > gMaxDatagram) {
        return false;
    }
    Datagram_Header header;
    memcpy(&header, datagram.data(), sizeof(header));
    u64 sequence = Read_Integer(header.sequence, sizeof(header.sequence), false);
    constexpr u64 window = sizeof(received) * 8;
    if (received != 0 && sequence <= receiveCount &&
        (receiveCount - sequence >= window || (received & (u64{1} << (receiveCount - sequence))) != 0)) {
        return false;
    }
    u8 nonce[gNonceSize] = {};
    Write_Le64(nonce, sequence);
    auto payload = datagram.subspan(sizeof(header), datagram.size() - gDatagramOverhead);
    if (crypto_aead_unlock(payload.data(), payload.data() + payload.size(), receiveKey, nonce,
        datagram.data(), sizeof(header), payload.data(), payload.size()) != 0) {
        return false;
    }
    // Commit the replay window only after authentication succeeds.
    if (received == 0 || sequence > receiveCount) {
        u64 distance = sequence - receiveCount;
        received = distance >= window ? 1 : (received << distance) | 1;
        receiveCount = sequence;
    }
    else {
        received |= u64{1} << (receiveCount - sequence);
    }
    return true;
}
