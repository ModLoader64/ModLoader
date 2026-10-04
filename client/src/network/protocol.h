#pragma once

#include "base.h"
#include "modloader_net.h"
#include <array>

constexpr u8 gProtocolMagic[] = { 'M', 'L', 'L', 'O', 'B', 'B', 'Y', '1' };
constexpr u16 gDefaultPort = 8082;

constexpr u32 gKeySize = 32;
constexpr u32 gNonceSize = 24;
constexpr u32 gTagSize = 16;
constexpr u32 gSignatureSize = 64;

using Member_Id = std::array<u8, 16>;

enum class Network_Transport : u32 {
    Tcp = MODLOADER_NET_TCP,
    Udp = MODLOADER_NET_UDP,
};

struct Datagram_Header {
    Member_Id client;
    u8 sequence[sizeof(u64)];
};

constexpr usize gDatagramOverhead = sizeof(Datagram_Header) + gTagSize;
constexpr usize gMaxDatagram = MODLOADER_NET_MAX_DATAGRAM_SIZE;
constexpr u32 gDatagramsPerPoll = 64;

struct Client_Hello {
    u8 magic[sizeof(gProtocolMagic)];
    u8 ephemeralKey[gKeySize];
};

struct Server_Hello {
    u8 magic[sizeof(gProtocolMagic)];
    u8 ephemeralKey[gKeySize];
    u8 staticKey[gKeySize];
};

struct Sealed_Secret {
    u8 nonce[gNonceSize];
    u8 tag[gTagSize];
    u8 secret[gKeySize];
};

struct Sealed_Message_Header {
    u8 ephemeralKey[gKeySize];
    u8 nonce[gNonceSize];
    u8 tag[gTagSize];
};

struct Signed_Message_Header {
    Member_Id sender;
    u8 signingKey[gKeySize];
    u8 signature[gSignatureSize];
};

constexpr u32 gClientHelloSize = sizeof(Client_Hello);
constexpr u32 gServerHelloSize = sizeof(Server_Hello);
constexpr u32 gFrameHeaderSize = sizeof(u32);
constexpr u32 gFrameOverhead = gFrameHeaderSize + gTagSize;
constexpr u32 gMaxFrame = 16u * 1024 * 1024; // plaintext
constexpr u32 gMaxMessage = 8u * 1024 * 1024; // payload
constexpr u32 gMaxNickname = 32;
constexpr u32 gMaxLobbyName = 64;
constexpr u32 gMaxTopic = 64;
constexpr u32 gSaltSize = 16;
constexpr u32 gSealedSecretSize = sizeof(Sealed_Secret);
constexpr u32 gSealedMessageOverhead = sizeof(Sealed_Message_Header) + sizeof(Signed_Message_Header);

enum Protocol_Message : u8 {
    // client -> server
    Protocol_Hello = 0x01, // identity key[32], connection signing key[32], transcript/key signature[64], nickname
    Protocol_Lobby_Query = 0x02, // lobby name
    Protocol_Lobby_Create = 0x03, // name, salt[16], auth key[32], public key[32], sealed secret key[72]
    Protocol_Lobby_Join = 0x04, // name, auth key[32]
    Protocol_Lobby_Send = 0x05, // flags (0), topic, sealed payload
    Protocol_Server_Send = 0x06, // flags (gProtocolSealed), topic, payload
    Protocol_Lobby_Send_To = 0x07, // recipient ID[16], flags (0), topic, sealed payload
    Protocol_Datagram_Ready = 0x08, // authenticated UDP address announcement
    // server -> client
    Protocol_Welcome = 0x81, // client ID[16]
    Protocol_Lobby_Info = 0x82, // exists (u8), salt[16]
    Protocol_Lobby_Joined = 0x83, // public key[32], sealed secret[72], count (u16), {ID[16], nickname, signing key[32]}[]
    Protocol_Lobby_Refused = 0x84, // Protocol_Refusal (u8); wrong password closes connection
    Protocol_Member_Joined = 0x85, // ID[16], nickname, connection signing key[32]
    Protocol_Member_Left = 0x86, // ID[16]
    Protocol_Lobby_Message = 0x87, // sender ID[16], flags, topic, sealed payload
    Protocol_Goodbye = 0x88, // reason (text), then connection closes
    Protocol_Server_Message = 0x89, // flags (gProtocolSealed), topic, payload
};

enum Protocol_Refusal : u8 {
    Protocol_Refusal_Password = 1,
    Protocol_Refusal_Exists = 2,
    Protocol_Refusal_Missing = 3,
};

constexpr u8 gProtocolSealed = 0x01; // sealed to the lobby key

struct Lobby_Member {
    Member_Id id = {};
    std::string nickname;
};

class Message_Reader {
public:
    Message_Reader(u8* data, u64 size)
        : remaining(data, size) {
    }

    u8 U8();
    u16 U16();
    u8* Bytes(u64 count);
    bool Copy(void* out, u64 count);

    template <usize N>
    bool Copy(std::array<u8, N>& out) {
        return Copy(out.data(), N);
    }

    std::optional<std::string> Text(u32 max);
    std::span<u8> Rest();

    bool At_End() const {
        return !failed && remaining.empty();
    }

    bool Failed() const {
        return failed;
    }

private:
    std::span<u8> remaining;
    bool failed = false;
};

struct Message_Writer {
    std::vector<u8> bytes;

    Message_Writer& U8(u8 value);
    Message_Writer& U16(u16 value);
    Message_Writer& Bytes(const void* data, u64 size);

    template <usize N>
    Message_Writer& Bytes(const std::array<u8, N>& data) {
        return Bytes(data.data(), N);
    }

    Message_Writer& Bytes(std::span<const u8> data) {
        return Bytes(data.data(), data.size());
    }

    Message_Writer& Text(std::string_view text);
};

std::string Hex_Text(const u8* bytes, u64 size);

bool Random_Bytes(void* out, u64 size);
void Wipe(void* secret, u64 size);
bool Key_Pair_New(u8 out_secret[gKeySize], u8 out_public[gKeySize]); // X25519 keys
void Public_Key_Of(const u8 secret[gKeySize], u8 out_public[gKeySize]);
bool Keys_Equal(const u8 left[gKeySize], const u8 right[gKeySize]);

struct Secure_Channel {
    u8 sendKey[gKeySize];
    u8 receiveKey[gKeySize];
    u64 sendCount;
    u64 receiveCount;

    bool Seal(std::span<const u8> plaintext, std::vector<u8>& out);
    bool Open(u8* frame, u64 size);
};

struct Datagram_Channel {
    u8 sendKey[gKeySize] = {};
    u8 receiveKey[gKeySize] = {};
    u64 sendCount = 0;
    u64 receiveCount = 0;
    u64 received = 0;

    void Initialize(const Secure_Channel& channel);
    bool Seal(const Member_Id& client, std::span<const u8> plaintext, std::vector<u8>& out);
    bool Open(std::span<u8> datagram);
};

bool Handshake_Client(
    const u8 client_secret[gKeySize],
    const u8 client_public[gKeySize],
    const u8 server_ephemeral[gKeySize],
    const u8 server_static[gKeySize],
    Secure_Channel& out_channel,
    u8 out_transcript[64]
);

bool Handshake_Server(
    const u8 server_secret[gKeySize],
    const u8 server_public[gKeySize],
    const u8 static_secret[gKeySize],
    const u8 static_public[gKeySize],
    const u8 client_public[gKeySize],
    Secure_Channel& out_channel,
    u8 out_transcript[64]
);

u32 Frame_Length(const u8* frame);

// Monocypher EdDSA identity (BLAKE2b, not Ed25519)
struct Identity {
    u8 secret[64];
    u8 publicKey[gKeySize];
    Member_Id id;
};

// Fresh for each connection; its public key is authenticated by the identity proof
struct Signing_Key {
    u8 secret[64];
    u8 publicKey[gKeySize];
};

bool Signing_Key_New(Signing_Key& out_key);

bool Identity_Load(const std::string& path, Identity& out_identity);
Member_Id Client_Id(const u8 public_key[gKeySize]);
std::string Client_Id_Text(const Member_Id& id);
void Identity_Prove(const Identity& identity, const u8 transcript[64], const u8 signing_public[gKeySize], u8 out_signature[gSignatureSize]);
bool Identity_Check(const u8 public_key[gKeySize], const u8 transcript[64], const u8 signing_public[gKeySize], const u8 signature[gSignatureSize]);

struct Lobby_Keys {
    u8 authKey[gKeySize];
    u8 wrapKey[gKeySize];
};

bool Lobby_Derive_Keys(std::string_view password, const u8 salt[gSaltSize], Lobby_Keys& out_keys);
void Lobby_Verifier(const u8 auth_key[gKeySize], u8 out_verifier[gKeySize]);
bool Lobby_Seal_Secret(const Lobby_Keys& keys, std::string_view lobby, const u8 public_key[gKeySize], const u8 secret[gKeySize], u8 out_sealed[gSealedSecretSize]);
bool Lobby_Open_Secret(const Lobby_Keys& keys, std::string_view lobby, const u8 public_key[gKeySize], const u8 sealed[gSealedSecretSize], u8 out_secret[gKeySize]);

bool Lobby_Seal_Message(const Signing_Key& key, const Member_Id& sender, const u8 lobby_public[gKeySize], std::string_view topic, std::span<const u8> payload, std::vector<u8>& out);

bool Lobby_Open_Message(
    const u8 lobby_secret[gKeySize],
    const u8 lobby_public[gKeySize],
    std::string_view topic,
    std::span<u8> sealed,
    std::span<const u8>& out_payload,
    Member_Id& out_sender,
    std::array<u8, gKeySize>& out_signing_key
);
