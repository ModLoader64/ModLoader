#include "n64.h"
#include "modloader_adapter_api.h"

#include <string.h>

#include "zlib.h"

namespace {

constexpr char gStateMagic[8] = { 'M', 'L', 'S', 'T', 'A', 'T', 'E', '\0' };
constexpr u32 gStateVersion = 1;
constexpr u32 gStateRetryRefreshes = 120;
constexpr s32 gAdapterStateBusy = 2; // saveState: not at this refresh

struct State_Header {
    char magic[8];
    u32 version;
    u32 reserved;
    char imageSha1[48]; // the image the session started from
    u64 adapterSize;
    u64 imageSize; // uncompressed image delta
    u64 imageStoredSize; // zero means no image delta
};

}

bool N64_Platform::Savestates_Allowed() const {
    const Module* blocker = runtime->Savestates_Blocker();

    if (blocker != nullptr) {
        const auto& block = blocker->savestateBlocks.begin()->second;
        Log_Warning("session", "savestates blocked by %s: %s", block.owner.c_str(), block.reason.c_str());
    }
    return blocker == nullptr;
}

std::optional<std::vector<u8>> N64_Platform::Pack_Image_Changes() const {
    const ModLoader_Space_Descriptor& rom = *Rom();
    const std::vector<u8>& original = runtime->config.image->data;

    if (rom.usableSize > rom.size || rom.usableSize > SIZE_MAX) {
        return std::nullopt;
    }
    z_size_t packed_size = compressBound_z(static_cast<z_size_t>(rom.usableSize));
    if (packed_size < rom.usableSize) {
        return std::nullopt;
    }
    std::vector<u8> changes(rom.usableSize);
    std::vector<u8> packed(packed_size);

    for (u64 index = 0; index < rom.usableSize; index++) {
        changes[index] = rom.hostBase[index] ^ (index < original.size() ? original[index] : 0);
    }
    if (compress2_z(packed.data(), &packed_size, changes.data(), changes.size(), Z_BEST_SPEED) != Z_OK) {
        return std::nullopt;
    }
    packed.resize(packed_size);
    return packed;
}

void N64_Platform::Save_State(const ModLoader_Platform_Adapter& adapter, void* handle) {
    saveRetries = gStateRetryRefreshes;
    Retry_Save_State(adapter, handle);
}

void N64_Platform::Retry_Save_State(const ModLoader_Platform_Adapter& adapter, void* handle) {
    if (saveRetries == 0) {
        return;
    }
    const ModLoader_Space_Descriptor* rom = Rom();
    u64 adapter_size = 0;
    std::optional<std::vector<u8>> image_changes;
    State_Header header = {};
    std::vector<u8> bytes;
    u64 capacity;
    s32 result;

    if (!Savestates_Allowed()) {
        saveRetries = 0;
        return;
    }
    result = adapter.saveState(handle, nullptr, &adapter_size);
    if (result == gAdapterStateBusy && saveRetries > 1) {
        saveRetries--;
        return;
    }
    saveRetries = 0;
    if (result < 0 || result == gAdapterStateBusy || adapter_size == 0 || adapter_size > SIZE_MAX - sizeof(header)) {
        Log_Error("session", "adapter cannot save state");
        return;
    }
    image_changes = rom != nullptr && rom->hostBase != nullptr ? Pack_Image_Changes() : std::vector<u8>();
    if (!image_changes || image_changes->size() > SIZE_MAX - sizeof(header) - adapter_size) {
        Log_Error("session", "cannot compress state image changes");
        return;
    }
    memcpy(header.magic, gStateMagic, sizeof(gStateMagic));
    header.version = gStateVersion;
    Text_Copy(header.imageSha1, runtime->config.image->sha1);
    header.adapterSize = adapter_size;
    header.imageSize = rom != nullptr && rom->hostBase != nullptr ? rom->usableSize : 0;
    header.imageStoredSize = image_changes->size();
    bytes.resize(sizeof(header) + adapter_size);
    memcpy(bytes.data(), &header, sizeof(header));
    capacity = adapter_size;
    if (adapter.saveState(handle, bytes.data() + sizeof(header), &capacity) != 0 || capacity != adapter_size) {
        Log_Error("session", "adapter cannot save state");
        return;
    }
    bytes.insert(bytes.end(), image_changes->begin(), image_changes->end());
    Directory_Create_All(Path_Join(runtime->config.dataDirectory, "states"));
    if (!File_Write(statePath, bytes)) {
        Log_Error("session", "cannot write state %s", statePath.c_str());
        return;
    }
}

// Adapter loads after refresh; state-loaded restores the image delta.
bool N64_Platform::Load_State(const ModLoader_Platform_Adapter& adapter, void* handle) {
    const ModLoader_Space_Descriptor* rom = Rom();
    std::optional<std::vector<u8>> bytes;
    State_Header header = {};
    std::vector<u8> changes;
    u64 size;

    saveRetries = 0;
    if (!Savestates_Allowed()) {
        return false;
    }
    bytes = File_Read(statePath);
    if (!bytes) {
        Log_Error("session", "cannot read state %s", statePath.c_str());
        return false;
    }
    size = bytes->size();
    if (size >= sizeof(header)) {
        memcpy(&header, bytes->data(), sizeof(header));
    }
    header.imageSha1[sizeof(header.imageSha1) - 1] = '\0';
    if (size < sizeof(header) || memcmp(header.magic, gStateMagic, sizeof(gStateMagic)) != 0 || header.version != gStateVersion ||
        header.adapterSize > size - sizeof(header) || header.imageStoredSize > size - sizeof(header) - header.adapterSize) {
        Log_Error("session", "invalid state %s", statePath.c_str());
        return false;
    }
    if (runtime->config.image->sha1 != header.imageSha1) {
        Log_Error("session", "%s belongs to another game image", statePath.c_str());
        return false;
    }
    if (header.imageSize > (rom != nullptr ? rom->size : 0) || header.imageSize > SIZE_MAX ||
        (header.imageStoredSize != 0 && (rom == nullptr || rom->hostBase == nullptr)) || (header.imageStoredSize == 0 && header.imageSize != 0)) {
        Log_Error("session", "unsupported image size in %s", statePath.c_str());
        return false;
    }
    if (header.imageStoredSize != 0) {
        z_size_t unpacked_size = static_cast<z_size_t>(header.imageSize);
        z_size_t packed_size = static_cast<z_size_t>(header.imageStoredSize);
        changes.resize(header.imageSize);
        if (uncompress2_z(changes.data(), &unpacked_size, bytes->data() + sizeof(header) + header.adapterSize, &packed_size) != Z_OK ||
            unpacked_size != header.imageSize || packed_size != header.imageStoredSize) {
            Log_Error("session", "corrupt image changes in %s", statePath.c_str());
            return false;
        }
    }
    if (adapter.loadState(handle, bytes->data() + sizeof(header), header.adapterSize) != 0) {
        Log_Error("session", "adapter cannot load state %s", statePath.c_str());
        return false;
    }
    pendingImageChanges = header.imageStoredSize != 0 ? std::optional(std::move(changes)) : std::nullopt;
    return true;
}

void N64_Platform::Restore_Image() {
    if (!pendingImageChanges) {
        return;
    }
    const ModLoader_Space_Descriptor& rom = *Rom();
    const std::vector<u8>& original = runtime->config.image->data;
    u64 size = pendingImageChanges->size();
    if (size != rom.usableSize && !runtime->Resize_Image(size)) {
        Log_Error("session", "cannot restore state image (%llu bytes)", static_cast<unsigned long long>(size));
        pendingImageChanges.reset();
        return;
    }
    for (u64 index = 0; index < size; index++) {
        rom.hostBase[index] = (index < original.size() ? original[index] : 0) ^ (*pendingImageChanges)[index];
    }
    pendingImageChanges.reset();
}
