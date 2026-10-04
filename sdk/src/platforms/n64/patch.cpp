#include "../../internal.h"
#include "callbacks.h"
#include "patch_code.h"
#include "patch_chain.h"

#include <modloader/platforms/n64/code.h>
#include <modloader/guest/memory.h>
#include <modloader/logger.h>
#include <modloader_n64_adapter.h>

#include <algorithm>
#include <utility>

using namespace ModLoader;

namespace ModLoader::N64::Detail {

struct Patch_State {
    Patch_Code code;
    Patch_Chain chain;
    bool chained = false;
    Guest::Heap* heap = nullptr;
    Guest::Heap::Handle allocation = 0;
    Hypercall::Handle hypercall = 0;
    u64 callbackToken = 0;
    bool requested = false;
    bool pinned = false;
    Patch_State* next = nullptr;
};

} // namespace ModLoader::N64::Detail

namespace {

using N64::Detail::Patch_State;
using N64::Detail::Patch_Mode;
using N64::Detail::Patch_Chain;

Patch_State* sPatches = nullptr;
constexpr u32 gPatchEvents[] = {
    MODLOADER_EVENT_RESET, MODLOADER_EVENT_STATE_LOADED, MODLOADER_EVENT_REFRESH,
    MODLOADER_EVENT_FRAME, MODLOADER_EVENT_N64_RSP_TASK,
};

using N64::Detail::Add_Callback;
using N64::Detail::Remove_Callback;
using N64::Detail::Callback_Active;
using N64::Detail::Callbacks_Stopping;

using N64::Detail::Ram_Range;
using N64::Detail::Physical_Address;
using N64::Detail::Overlaps;

void Follow_Patch_Events(bool follow) {
    for (u32 event : gPatchEvents) {
        Runtime::Follow_Event(event, follow);
    }
}

bool Destroy_State(Patch_State* state) {
    if (Callback_Active(state->callbackToken) ||
        (state->chained && !state->chain.Destroy()) || !state->code.Destroy()) {
        return false;
    }

    if (state->hypercall != 0 && ModLoader_Host_Hypercall_Register(state->hypercall, 0) == 0) {
        return false;
    }

    if (state->heap != nullptr && state->allocation != 0 &&
        !state->heap->Set_Move_Callback(state->allocation, nullptr)) {
        return false;
    }

    Patch_State** link = &sPatches;
    while (*link != nullptr && *link != state) {
        link = &(*link)->next;
    }

    if (*link == state) {
        *link = state->next;
        if (sPatches == nullptr) {
            Follow_Patch_Events(false);
        }
    }

    if (state->heap != nullptr && state->allocation != 0) {
        state->heap->Free(state->allocation);
    }

    u64 token = state->callbackToken;
    delete state;
    Remove_Callback(token);
    return true;
}

bool Set_Enabled(Patch_State& state, bool enabled) {
    if (state.chained) {
        return enabled ? state.chain.Enable() : state.chain.Disable();
    }
    return enabled ? state.code.Enable() : state.code.Disable();
}

bool Is_Enabled(const Patch_State& state) {
    return state.chained ? state.chain.Is_Enabled() : state.code.Is_Enabled();
}

Guest::usize Required_Storage(const Patch_State& state) {
    u64 bytes = state.code.Capacity();
    bytes += state.chained ? Patch_Chain::Header_Size : 0;
    return bytes <= UINT32_MAX ? static_cast<Guest::usize>(bytes) : 0;
}

bool Valid_Storage(const Patch_State& state, const N64::Patch::Storage& storage) {
    Guest::usize prefix = state.chained ? Patch_Chain::Header_Size : 0;
    Guest::usize required = Required_Storage(state);
    return required != 0 && storage.capacity >= required && storage.capacity > prefix &&
        Ram_Range(storage.address, storage.capacity) &&
        !Overlaps(state.code.Address(), state.code.Affected_Size(), storage.address, storage.capacity);
}

bool Bind_Storage(Patch_State& state, const N64::Patch::Storage& storage) {
    Guest::usize prefix = state.chained ? Patch_Chain::Header_Size : 0;
    if (!Valid_Storage(state, storage)) {
        return false;
    }
    return state.code.Relocate(storage.address + prefix, storage.capacity - prefix) &&
        (!state.chained || state.chain.Bind(state.code, storage.address, storage.capacity));
}

} // namespace

void ModLoader::Runtime::Patches_Shutdown() {
    while (sPatches != nullptr) {
        if (!Destroy_State(sPatches)) {
            Logger::Error("N64 patch restore failed during shutdown");
            return;
        }
    }
}

void ModLoader::Runtime::Platform_Maintain(u32 event) {
    if (std::find(std::begin(gPatchEvents), std::end(gPatchEvents), event) == std::end(gPatchEvents)) {
        return;
    }

    for (Patch_State* state = sPatches; state != nullptr; state = state->next) {
        if (!Callback_Active(state->callbackToken)) {
            Set_Enabled(*state, state->requested);
        }
    }
}

ModLoader::N64::Patch::~Patch() {
    if (!Destroy()) {
        Logger::Error("N64 patch destruction failed");
    }
}

ModLoader::N64::Patch::Patch(Patch&& other) noexcept : state(std::exchange(other.state, nullptr)) {
}

ModLoader::N64::Patch& ModLoader::N64::Patch::operator=(Patch&& other) noexcept {
    if (this != &other && Destroy()) {
        state = std::exchange(other.state, nullptr);
    }
    return *this;
}

bool ModLoader::N64::Patch::Finish_Create(Patch_State* created, Guest::Heap* heap, Storage storage) {
    bool valid = created->code.Is_Created() && !Callbacks_Stopping();
    Guest::uptr source = created->code.Address();
    Guest::usize size = created->code.Affected_Size();
    for (Patch_State* other = sPatches; valid && other != nullptr; other = other->next) {
        Guest::uptr start = other->code.Address();
        valid = (created->chained && other->chained && Physical_Address(source) == Physical_Address(start)) ||
            !Overlaps(source, size, start, other->code.Affected_Size());
    }

    std::vector<u8> canonical;
    if (valid && !created->chained) {
        valid = Patch_Chain::Capture(source, size, canonical) &&
            canonical.empty();
    }

    if (valid && created->code.Capacity() != 0) {
        Guest::usize required = Required_Storage(*created);
        created->heap = heap;
        if (heap != nullptr && required != 0) {
            created->allocation = heap->Alloc(required);
            storage = {heap->Address(created->allocation), heap->Size(created->allocation)};
        }

        valid = required != 0 && storage.address != 0 && Bind_Storage(*created, storage);
        if (valid && heap != nullptr) {
            valid = heap->Set_Move_Callback(created->allocation,
                [created](Guest::Heap::Move_Phase phase, const Guest::Heap::Move& move) {
                    switch (phase) {
                    case Guest::Heap::Move_Phase::Prepare:
                        return (!created->pinned || move.from == move.to) &&
                            !Callback_Active(created->callbackToken) &&
                            Valid_Storage(*created, {move.to, move.newSize}) &&
                            (!created->chained || created->chain.Prepare_Relocation(move.to, move.newSize));
                    case Guest::Heap::Move_Phase::Commit:
                        return created->chained ? created->chain.Commit_Relocation() :
                            created->code.Relocate(move.to, move.newSize);
                    case Guest::Heap::Move_Phase::Rollback:
                        created->chain.Cancel_Relocation();
                        return true;
                    }
                    return false;
                });
        }
    }

    if (!valid) {
        Destroy_State(created);
        return false;
    }

    if (sPatches == nullptr) {
        Follow_Patch_Events(true);
    }

    created->next = sPatches;
    sPatches = created;
    state = created;
    return true;
}

bool ModLoader::N64::Patch::Create_Inline(Guest::uptr source, std::string_view assembly) {
    return Create_Assembly(source, nullptr, {}, Preserve::None, assembly);
}

bool ModLoader::N64::Patch::Create_Inline(Guest::uptr source, std::span<const u8> bytes) {
    if (state != nullptr) {
        return false;
    }

    auto* created = new (std::nothrow) Patch_State;
    if (created == nullptr) {
        return false;
    }
    created->code.Create_Bytes(source, bytes);
    return Finish_Create(created);
}

bool ModLoader::N64::Patch::Create_Assembly(Guest::uptr source, Guest::Heap* heap, Storage storage, Preserve preserve, std::string_view assembly) {
    if (state != nullptr || Callbacks_Stopping()) {
        return false;
    }

    bool detour = heap != nullptr || storage.address != 0;
    std::vector<u8> canonical;
    if (detour && !Patch_Chain::Capture(source, 8, canonical)) {
        return false;
    }

    auto* created = new (std::nothrow) Patch_State;
    if (created == nullptr) {
        return false;
    }

    created->chained = detour;
    created->code.Create_Assembly(source, assembly, detour ? Patch_Mode::Detour : Patch_Mode::Inline, preserve, canonical);
    return Finish_Create(created, heap, storage);
}

bool ModLoader::N64::Patch::Create_Callback(Guest::uptr source, Guest::Heap* heap, Storage storage, Preserve preserve, Callback handler) {
    if (state != nullptr || Callbacks_Stopping() || !handler) {
        return false;
    }

    bool detour = heap != nullptr || storage.address != 0;
    std::vector<u8> canonical;
    if (detour && !Patch_Chain::Capture(source, 8, canonical)) {
        return false;
    }

    auto* created = new (std::nothrow) Patch_State;
    if (created == nullptr) {
        return false;
    }

    created->chained = detour;
    created->callbackToken = Add_Callback(std::move(handler));
    created->hypercall = created->callbackToken != 0 ? Hypercall::Allocate() : 0;
    if (created->hypercall != 0 &&
        ModLoader_Host_Hypercall_Register(created->hypercall, created->callbackToken) != 0) {
        created->code.Create_Hypercall(source, created->hypercall, detour ? Patch_Mode::Detour : Patch_Mode::Inline, preserve, canonical);
    }
    return Finish_Create(created, heap, storage);
}

bool ModLoader::N64::Patch::Create_Inline(Guest::uptr source, Callback handler) {
    return Create_Callback(source, nullptr, {}, Preserve::None, std::move(handler));
}

bool ModLoader::N64::Patch::Create_Redirect(Guest::uptr source, Guest::uptr destination) {
    if (state != nullptr) {
        return false;
    }

    auto* created = new (std::nothrow) Patch_State;
    if (created == nullptr) {
        return false;
    }
    created->code.Create_Destination(source, destination);
    return Finish_Create(created);
}

bool ModLoader::N64::Patch::Create_Detour(Guest::uptr source, Guest::Heap& heap, Preserve preserve, std::string_view assembly) {
    return Create_Assembly(source, &heap, {}, preserve, assembly);
}

bool ModLoader::N64::Patch::Create_Detour(Guest::uptr source, Guest::Heap& heap, Preserve preserve, Callback handler) {
    return Create_Callback(source, &heap, {}, preserve, std::move(handler));
}

bool ModLoader::N64::Patch::Create_Detour(Guest::uptr source, Storage storage, Preserve preserve, std::string_view assembly) {
    return storage.address != 0 && storage.capacity != 0 && Create_Assembly(source, nullptr, storage, preserve, assembly);
}

bool ModLoader::N64::Patch::Create_Detour(Guest::uptr source, Storage storage, Preserve preserve, Callback handler) {
    return storage.address != 0 && storage.capacity != 0 && Create_Callback(source, nullptr, storage, preserve, std::move(handler));
}

bool ModLoader::N64::Patch::Enable() {
    if (state == nullptr || !Set_Enabled(*state, true)) {
        return false;
    }

    state->requested = true;
    if (state->allocation != 0 && !state->pinned) {
        state->heap->Pin(state->allocation);
        state->pinned = true;
    }
    return true;
}

bool ModLoader::N64::Patch::Disable() {
    if (state == nullptr) {
        return true;
    }

    if (!Set_Enabled(*state, false)) {
        return false;
    }

    state->requested = false;
    return true;
}

bool ModLoader::N64::Patch::Allow_Relocation() {
    if (state == nullptr || Callback_Active(state->callbackToken)) {
        return false;
    }

    if (state->pinned) {
        state->heap->Unpin(state->allocation);
        state->pinned = false;
    }
    return true;
}

bool ModLoader::N64::Patch::Destroy() {
    if (state == nullptr) {
        return true;
    }

    Patch_State* retired = state;
    state = nullptr;
    if (!Destroy_State(retired)) {
        state = retired;
        return false;
    }

    return true;
}

bool ModLoader::N64::Patch::Is_Created() const {
    return state != nullptr;
}

bool ModLoader::N64::Patch::Is_Enabled() const {
    return state != nullptr && ::Is_Enabled(*state);
}

Guest::uptr ModLoader::N64::Patch::Address() const {
    return state != nullptr ? state->code.Address() : 0;
}

Guest::uptr ModLoader::N64::Patch::Destination() const {
    return state != nullptr ? state->code.Destination() : 0;
}

Guest::uptr ModLoader::N64::Patch::Trampoline() const {
    return state != nullptr ? state->code.Trampoline() : 0;
}

Guest::Heap::Handle ModLoader::N64::Patch::Allocation() const {
    return state != nullptr ? state->allocation : 0;
}

std::vector<u8> ModLoader::N64::Patch::Original_Bytes() const {
    return state != nullptr ? std::vector<u8>(std::from_range, state->code.Original_Bytes()) : std::vector<u8>{};
}

std::vector<u8> ModLoader::N64::Patch::Patched_Bytes() const {
    if (state == nullptr) {
        return {};
    }

    std::vector<u8> bytes(std::from_range, state->code.Patched_Bytes());
    if (state->chained && state->chain.Is_Enabled() && !N64::Detail::Read_Code(state->code.Address(), bytes)) {
        return {};
    }

    return bytes;
}

std::vector<u8> ModLoader::N64::Patch::Destination_Bytes() const {
    if (state == nullptr) {
        return {};
    }

    if (state->chained && state->chain.Is_Enabled()) {
        return state->chain.Destination_Bytes();
    }
    
    return std::vector<u8>(std::from_range, state->code.Destination_Bytes());
}
