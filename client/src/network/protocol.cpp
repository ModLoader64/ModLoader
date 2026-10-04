#include "network/protocol.h"

#include "monocypher.h"
#include <modloader_bytes.h>

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#else
#include <sys/random.h>
#endif

namespace {

constexpr char gClientProofLabel[] = "ModLoader client";
constexpr char gAuthLabel[] = "ModLoader lobby auth";
constexpr char gWrapLabel[] = "ModLoader lobby wrap";
constexpr char gVerifierLabel[] = "ModLoader lobby verifier";
constexpr char gMessageKeyLabel[] = "ModLoader lobby message";
constexpr char gMessageSignatureLabel[] = "ModLoader message";

constexpr u32 gArgonBlocks = 64 * 1024;
constexpr u32 gArgonPasses = 3;

void Nonce_Of(u64 count, u8 out_nonce[gNonceSize]) {
    memset(out_nonce, 0, gNonceSize);
    ModLoader::Bytes::Write_Le64(out_nonce, count);
}

bool Is_Zero(const u8 value[gKeySize]) {
    static const u8 zero[gKeySize] = {};

    return crypto_verify32(value, zero) == 0;
}

void Transcript(const u8 client_public[gKeySize], const u8 server_ephemeral[gKeySize], const u8 server_static[gKeySize], u8 out[64]) {
    crypto_blake2b_ctx context;

    crypto_blake2b_init(&context, 64);
    crypto_blake2b_update(&context, gProtocolMagic, sizeof(gProtocolMagic));
    crypto_blake2b_update(&context, client_public, gKeySize);
    crypto_blake2b_update(&context, server_ephemeral, gKeySize);
    crypto_blake2b_update(&context, server_static, gKeySize);
    crypto_blake2b_final(&context, out);
}

bool Channel_Keys(u8 shared[2 * gKeySize], const u8 transcript[64], bool client, Secure_Channel& out_channel) {
    u8 keys[2 * gKeySize];
    bool good = !Is_Zero(shared) && !Is_Zero(shared + gKeySize);

    out_channel = {};
    if (good) {
        crypto_blake2b_keyed(keys, sizeof(keys), shared, 2 * gKeySize, transcript, 64);
        memcpy(out_channel.sendKey, keys + (client ? 0 : gKeySize), gKeySize);
        memcpy(out_channel.receiveKey, keys + (client ? gKeySize : 0), gKeySize);
        crypto_wipe(keys, sizeof(keys));
    }

    crypto_wipe(shared, 2 * gKeySize);
    return good;
}

void Labelled_Key(const u8 master[gKeySize], const char* label, u8 out_key[gKeySize]) {
    crypto_blake2b_keyed(out_key, gKeySize, master, gKeySize, reinterpret_cast<const u8*>(label), strlen(label));
}

void Message_Digest(const Member_Id& sender, const u8 signing_public[gKeySize], const u8 lobby_public[gKeySize], std::string_view topic, std::span<const u8> payload, u8 out_digest[64]) {
    u8 topic_length = static_cast<u8>(topic.size());
    crypto_blake2b_ctx context;

    crypto_blake2b_init(&context, 64);
    crypto_blake2b_update(&context, reinterpret_cast<const u8*>(gMessageSignatureLabel), sizeof(gMessageSignatureLabel));
    crypto_blake2b_update(&context, sender.data(), sender.size());
    crypto_blake2b_update(&context, signing_public, gKeySize);
    crypto_blake2b_update(&context, lobby_public, gKeySize);
    crypto_blake2b_update(&context, &topic_length, 1);
    crypto_blake2b_update(&context, reinterpret_cast<const u8*>(topic.data()), topic_length);
    crypto_blake2b_update(&context, payload.data(), payload.size());
    crypto_blake2b_final(&context, out_digest);
}

void Message_Key(const u8 shared[gKeySize], const u8 ephemeral_public[gKeySize], const u8 lobby_public[gKeySize], u8 out_key[gKeySize]) {
    u8 context[sizeof(gMessageKeyLabel) + 2 * gKeySize];

    memcpy(context, gMessageKeyLabel, sizeof(gMessageKeyLabel));
    memcpy(context + sizeof(gMessageKeyLabel), ephemeral_public, gKeySize);
    memcpy(context + sizeof(gMessageKeyLabel) + gKeySize, lobby_public, gKeySize);
    crypto_blake2b_keyed(out_key, gKeySize, shared, gKeySize, context, sizeof(context));
}

std::vector<u8> Secret_Context(std::string_view lobby, const u8 public_key[gKeySize]) {
    std::vector<u8> context(lobby.begin(), lobby.end());

    context.insert(context.end(), public_key, public_key + gKeySize);
    return context;
}

} // namespace

u8 Message_Reader::U8() {
    const u8* bytes = Bytes(sizeof(u8));
    return bytes != nullptr ? bytes[0] : 0;
}

u16 Message_Reader::U16() {
    const u8* bytes = Bytes(sizeof(u16));
    return bytes != nullptr ? ModLoader::Bytes::Read_Le16(bytes) : 0;
}

u8* Message_Reader::Bytes(u64 count) {
    if (failed || count > remaining.size()) {
        failed = true;
        return nullptr;
    }

    u8* bytes = remaining.data();
    remaining = remaining.subspan(count);
    return bytes;
}

bool Message_Reader::Copy(void* out, u64 count) {
    const u8* bytes = Bytes(count);
    if (bytes != nullptr) {
        memcpy(out, bytes, static_cast<usize>(count));
    }

    return bytes != nullptr;
}

std::optional<std::string> Message_Reader::Text(u32 max) {
    u8 length = U8();
    const char* bytes = reinterpret_cast<const char*>(Bytes(length));

    if (bytes == nullptr || !Text_Is_Valid(std::string_view(bytes, length), max)) {
        failed = true;
        return std::nullopt;
    }

    return std::string(bytes, length);
}

std::span<u8> Message_Reader::Rest() {
    u64 count = remaining.size();
    u8* bytes = Bytes(count);
    return bytes != nullptr ? std::span<u8>(bytes, count) : std::span<u8>();
}

Message_Writer& Message_Writer::U8(u8 value) {
    bytes.push_back(value);
    return *this;
}

Message_Writer& Message_Writer::U16(u16 value) {
    u8 encoded[sizeof(value)];
    ModLoader::Bytes::Write_Le16(encoded, value);
    return Bytes(encoded, sizeof(encoded));
}

Message_Writer& Message_Writer::Bytes(const void* data, u64 size) {
    bytes.insert(bytes.end(), static_cast<const u8*>(data), static_cast<const u8*>(data) + size);
    return *this;
}

Message_Writer& Message_Writer::Text(std::string_view text) {
    U8(static_cast<u8>(text.size()));
    return Bytes(text.data(), text.size());
}

std::string Hex_Text(const u8* bytes, u64 size) {
    std::string text;

    if (size > text.max_size() / 2) {
        return text;
    }

    text.resize(static_cast<usize>(size) * 2);
    ModLoader::Bytes::Hex_Encode(bytes, static_cast<usize>(size), text.data());
    return text;
}

bool Random_Bytes(void* out, u64 size) {
#if defined(_WIN32)
    return BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    u8* bytes = static_cast<u8*>(out);
    u64 done = 0;

    while (done < size) {
        ssize_t result = getrandom(bytes + done, static_cast<size_t>(size - done), 0);

        if (result <= 0) {
            return false;
        }
        done += static_cast<u64>(result);
    }
    return true;
#endif
}

void Wipe(void* secret, u64 size) {
    crypto_wipe(secret, static_cast<size_t>(size));
}

bool Keys_Equal(const u8 left[gKeySize], const u8 right[gKeySize]) {
    return crypto_verify32(left, right) == 0;
}

void Public_Key_Of(const u8 secret[gKeySize], u8 out_public[gKeySize]) {
    crypto_x25519_public_key(out_public, secret);
}

bool Key_Pair_New(u8 out_secret[gKeySize], u8 out_public[gKeySize]) {
    if (!Random_Bytes(out_secret, gKeySize)) {
        return false;
    }

    crypto_x25519_public_key(out_public, out_secret);
    return true;
}

bool Handshake_Client(
    const u8 client_secret[gKeySize],
    const u8 client_public[gKeySize],
    const u8 server_ephemeral[gKeySize],
    const u8 server_static[gKeySize],
    Secure_Channel& out_channel,
    u8 out_transcript[64]
) {
    u8 shared[2 * gKeySize];

    crypto_x25519(shared, client_secret, server_ephemeral);
    crypto_x25519(shared + gKeySize, client_secret, server_static);
    Transcript(client_public, server_ephemeral, server_static, out_transcript);
    return Channel_Keys(shared, out_transcript, true, out_channel);
}

bool Handshake_Server(
    const u8 server_secret[gKeySize],
    const u8 server_public[gKeySize],
    const u8 static_secret[gKeySize],
    const u8 static_public[gKeySize],
    const u8 client_public[gKeySize],
    Secure_Channel& out_channel,
    u8 out_transcript[64]
) {
    u8 shared[2 * gKeySize];

    crypto_x25519(shared, server_secret, client_public);
    crypto_x25519(shared + gKeySize, static_secret, client_public);
    Transcript(client_public, server_public, static_public, out_transcript);
    return Channel_Keys(shared, out_transcript, false, out_channel);
}

u32 Frame_Length(const u8* frame) {
    return ModLoader::Bytes::Read_Le32(frame);
}

bool Secure_Channel::Seal(std::span<const u8> plaintext, std::vector<u8>& out) {
    u64 start = out.size();
    u32 length = static_cast<u32>(plaintext.size() + gTagSize);
    u8* frame;
    u8 nonce[gNonceSize];

    if (plaintext.size() > gMaxFrame) {
        return false;
    }

    out.resize(start + plaintext.size() + gFrameOverhead);
    frame = out.data() + start;
    ModLoader::Bytes::Write_Le32(frame, length);
    Nonce_Of(sendCount++, nonce);
    crypto_aead_lock(frame + gFrameHeaderSize, frame + gFrameHeaderSize + plaintext.size(), sendKey, nonce, frame, gFrameHeaderSize, plaintext.data(), plaintext.size());
    return true;
}

bool Secure_Channel::Open(u8* frame, u64 size) {
    u64 text_size;
    u8 nonce[gNonceSize];

    if (size < gFrameOverhead || Frame_Length(frame) != size - gFrameHeaderSize) {
        return false;
    }

    text_size = size - gFrameOverhead;
    Nonce_Of(receiveCount, nonce);
    if (crypto_aead_unlock(frame + gFrameHeaderSize, frame + gFrameHeaderSize + text_size, receiveKey, nonce, frame, gFrameHeaderSize, frame + gFrameHeaderSize, static_cast<size_t>(text_size)) != 0) {
        return false;
    }

    receiveCount++;
    return true;
}

bool Signing_Key_New(Signing_Key& out_key) {
    u8 seed[gKeySize];

    if (!Random_Bytes(seed, sizeof(seed))) {
        crypto_wipe(seed, sizeof(seed));
        return false;
    }

    crypto_eddsa_key_pair(out_key.secret, out_key.publicKey, seed);
    return true;
}

bool Identity_Load(const std::string& path, Identity& out_identity) {
    std::optional<std::vector<u8>> bytes = File_Read(path);
    u8 seed[gKeySize];

    if (bytes && bytes->size() == sizeof(seed)) {
        memcpy(seed, bytes->data(), sizeof(seed));
        crypto_wipe(bytes->data(), bytes->size());
    }
    else if (bytes) {
        crypto_wipe(bytes->data(), bytes->size());
        Log_Error("network", "invalid identity key: %s (expected 32 bytes)", path.c_str());
        return false;
    }
    else if (!Random_Bytes(seed, sizeof(seed)) || !File_Write(path, seed)) {
        Log_Error("network", "cannot create identity key: %s", path.c_str());
        return false;
    }

    crypto_eddsa_key_pair(out_identity.secret, out_identity.publicKey, seed); // wipes the seed
    out_identity.id = Client_Id(out_identity.publicKey);
    return true;
}

Member_Id Client_Id(const u8 public_key[gKeySize]) {
    Member_Id id;

    crypto_blake2b(id.data(), id.size(), public_key, gKeySize);
    return id;
}

std::string Client_Id_Text(const Member_Id& id) {
    return Hex_Text(id.data(), id.size());
}

void Identity_Prove(const Identity& identity, const u8 transcript[64], const u8 signing_public[gKeySize], u8 out_signature[gSignatureSize]) {
    u8 message[sizeof(gClientProofLabel) + 64 + gKeySize];

    memcpy(message, gClientProofLabel, sizeof(gClientProofLabel));
    memcpy(message + sizeof(gClientProofLabel), transcript, 64);
    memcpy(message + sizeof(gClientProofLabel) + 64, signing_public, gKeySize);
    crypto_eddsa_sign(out_signature, identity.secret, message, sizeof(message));
}

bool Identity_Check(const u8 public_key[gKeySize], const u8 transcript[64], const u8 signing_public[gKeySize], const u8 signature[gSignatureSize]) {
    u8 message[sizeof(gClientProofLabel) + 64 + gKeySize];

    memcpy(message, gClientProofLabel, sizeof(gClientProofLabel));
    memcpy(message + sizeof(gClientProofLabel), transcript, 64);
    memcpy(message + sizeof(gClientProofLabel) + 64, signing_public, gKeySize);
    return crypto_eddsa_check(signature, public_key, message, sizeof(message)) == 0;
}

bool Lobby_Derive_Keys(std::string_view password, const u8 salt[gSaltSize], Lobby_Keys& out_keys) {
    void* work_area = malloc(static_cast<usize>(gArgonBlocks) * 1024);
    crypto_argon2_config config = {};
    crypto_argon2_inputs inputs = {};
    u8 master[gKeySize];

    if (work_area == nullptr) {
        return false;
    }

    config.algorithm = CRYPTO_ARGON2_ID;
    config.nb_blocks = gArgonBlocks;
    config.nb_passes = gArgonPasses;
    config.nb_lanes = 1;
    inputs.pass = reinterpret_cast<const u8*>(password.data());
    inputs.pass_size = static_cast<u32>(password.size());
    inputs.salt = salt;
    inputs.salt_size = gSaltSize;
    crypto_argon2(master, sizeof(master), work_area, config, inputs, crypto_argon2_no_extras);
    free(work_area);
    Labelled_Key(master, gAuthLabel, out_keys.authKey);
    Labelled_Key(master, gWrapLabel, out_keys.wrapKey);
    crypto_wipe(master, sizeof(master));
    return true;
}

void Lobby_Verifier(const u8 auth_key[gKeySize], u8 out_verifier[gKeySize]) {
    crypto_blake2b_ctx context;

    crypto_blake2b_init(&context, gKeySize);
    crypto_blake2b_update(&context, reinterpret_cast<const u8*>(gVerifierLabel), sizeof(gVerifierLabel));
    crypto_blake2b_update(&context, auth_key, gKeySize);
    crypto_blake2b_final(&context, out_verifier);
}

bool Lobby_Seal_Secret(const Lobby_Keys& keys, std::string_view lobby, const u8 public_key[gKeySize], const u8 secret[gKeySize], u8 out_sealed[gSealedSecretSize]) {
    std::vector<u8> context = Secret_Context(lobby, public_key);

    if (!Random_Bytes(out_sealed + offsetof(Sealed_Secret, nonce), sizeof(Sealed_Secret::nonce))) {
        return false;
    }

    crypto_aead_lock(out_sealed + offsetof(Sealed_Secret, secret), out_sealed + offsetof(Sealed_Secret, tag), keys.wrapKey,
        out_sealed + offsetof(Sealed_Secret, nonce), context.data(), context.size(), secret, sizeof(Sealed_Secret::secret));
    return true;
}

bool Lobby_Open_Secret(const Lobby_Keys& keys, std::string_view lobby, const u8 public_key[gKeySize], const u8 sealed[gSealedSecretSize], u8 out_secret[gKeySize]) {
    std::vector<u8> context = Secret_Context(lobby, public_key);
    u8 derived[gKeySize];

    if (crypto_aead_unlock(out_secret, sealed + offsetof(Sealed_Secret, tag), keys.wrapKey,
        sealed + offsetof(Sealed_Secret, nonce), context.data(), context.size(),
        sealed + offsetof(Sealed_Secret, secret), sizeof(Sealed_Secret::secret)) != 0) {
        return false;
    }

    crypto_x25519_public_key(derived, out_secret);
    if (crypto_verify32(derived, public_key) != 0) {
        crypto_wipe(out_secret, gKeySize);
        return false;
    }
    return true;
}

bool Lobby_Seal_Message(const Signing_Key& signer, const Member_Id& sender, const u8 lobby_public[gKeySize], std::string_view topic, std::span<const u8> payload, std::vector<u8>& out) {
    u64 start = out.size();
    u8 ephemeral_secret[gKeySize];
    u8* sealed;
    u8* ephemeral_public;
    u8* nonce;
    u8* tag;
    u8* text;
    u8 shared[gKeySize];
    u8 key[gKeySize];
    u8 digest[64];

    if (payload.size() > gMaxMessage || !Random_Bytes(ephemeral_secret, sizeof(ephemeral_secret))) {
        return false;
    }

    out.resize(start + payload.size() + gSealedMessageOverhead);
    sealed = out.data() + start;
    ephemeral_public = sealed + offsetof(Sealed_Message_Header, ephemeralKey);
    nonce = sealed + offsetof(Sealed_Message_Header, nonce);
    tag = sealed + offsetof(Sealed_Message_Header, tag);
    text = sealed + sizeof(Sealed_Message_Header);
    crypto_x25519_public_key(ephemeral_public, ephemeral_secret);
    crypto_x25519(shared, ephemeral_secret, lobby_public);
    crypto_wipe(ephemeral_secret, sizeof(ephemeral_secret));
    if (Is_Zero(shared) || !Random_Bytes(nonce, sizeof(Sealed_Message_Header::nonce))) {
        out.resize(start);
        return false;
    }

    Message_Key(shared, ephemeral_public, lobby_public, key);
    crypto_wipe(shared, sizeof(shared));
    Message_Digest(sender, signer.publicKey, lobby_public, topic, payload, digest);
    memcpy(text + offsetof(Signed_Message_Header, sender), sender.data(), sender.size());
    memcpy(text + offsetof(Signed_Message_Header, signingKey), signer.publicKey, sizeof(signer.publicKey));
    crypto_eddsa_sign(text + offsetof(Signed_Message_Header, signature), signer.secret, digest, sizeof(digest));
    memcpy(text + sizeof(Signed_Message_Header), payload.data(), payload.size());
    crypto_aead_lock(text, tag, key, nonce, reinterpret_cast<const u8*>(topic.data()), topic.size(), text, payload.size() + sizeof(Signed_Message_Header));
    crypto_wipe(key, sizeof(key));
    return true;
}

bool Lobby_Open_Message(
    const u8 lobby_secret[gKeySize],
    const u8 lobby_public[gKeySize],
    std::string_view topic,
    std::span<u8> sealed,
    std::span<const u8>& out_payload,
    Member_Id& out_sender,
    std::array<u8, gKeySize>& out_signing_key
) {
    u8 shared[gKeySize];
    u8 key[gKeySize];
    bool opened;
    u8* text;
    u64 text_size;
    std::span<const u8> payload;
    u8 digest[64];
    Member_Id sender;

    out_payload = {};
    out_sender = {};
    out_signing_key = {};

    if (sealed.size() < gSealedMessageOverhead) {
        return false;
    }

    u8* ephemeral_public = sealed.data() + offsetof(Sealed_Message_Header, ephemeralKey);
    u8* nonce = sealed.data() + offsetof(Sealed_Message_Header, nonce);
    u8* tag = sealed.data() + offsetof(Sealed_Message_Header, tag);
    crypto_x25519(shared, lobby_secret, ephemeral_public);
    if (Is_Zero(shared)) {
        return false;
    }

    Message_Key(shared, ephemeral_public, lobby_public, key);
    crypto_wipe(shared, sizeof(shared));
    text = sealed.data() + sizeof(Sealed_Message_Header);
    text_size = sealed.size() - sizeof(Sealed_Message_Header);
    opened = crypto_aead_unlock(text, tag, key, nonce, reinterpret_cast<const u8*>(topic.data()), topic.size(), text, text_size) == 0;
    crypto_wipe(key, sizeof(key));
    if (!opened) {
        return false;
    }

    u8* signing_key = text + offsetof(Signed_Message_Header, signingKey);
    u8* signature = text + offsetof(Signed_Message_Header, signature);
    memcpy(sender.data(), text + offsetof(Signed_Message_Header, sender), sender.size());
    payload = std::span<const u8>(text + sizeof(Signed_Message_Header), text_size - sizeof(Signed_Message_Header));
    Message_Digest(sender, signing_key, lobby_public, topic, payload, digest);
    if (crypto_eddsa_check(signature, signing_key, digest, sizeof(digest)) != 0) {
        return false;
    }
    
    out_sender = sender;
    memcpy(out_signing_key.data(), signing_key, out_signing_key.size());
    out_payload = payload;
    return true;
}
