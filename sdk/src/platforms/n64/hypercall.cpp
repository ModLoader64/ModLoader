#include "../../internal.h"
#include "callbacks.h"

#include <modloader_n64_adapter.h>

#include <unordered_map>
#include <utility>

using namespace ModLoader;

namespace {

struct Callback_Record {
    N64::Hypercall::Callback handler;
    u32 activeCalls = 0;
    bool retired = false;
};

[[clang::no_destroy]] std::unordered_map<u64, Callback_Record> sCallbacks;
[[clang::no_destroy]] std::unordered_map<N64::Hypercall::Handle, u64> sHypercallCallbacks;
u64 sNextCallback = 1;
bool sStoppingCallbacks;

using N64::Detail::Add_Callback;
using N64::Detail::Remove_Callback;

void Remove_Callback(std::unordered_map<N64::Hypercall::Handle, u64>& owners, N64::Hypercall::Handle owner) {
    auto found = owners.find(owner);
    if (found != owners.end()) {
        u64 token = found->second;
        owners.erase(found);
        Remove_Callback(token);
    }
}

} // namespace

namespace ModLoader::N64::Detail {

u64 Add_Callback(N64::Hypercall::Callback handler) {
    if (sStoppingCallbacks || !handler || sNextCallback == 0) {
        return 0;
    }
    u64 token = sNextCallback++;
    sCallbacks.emplace(token, Callback_Record{std::move(handler)});
    return token;
}

void Remove_Callback(u64 token) {
    auto found = sCallbacks.find(token);
    if (found == sCallbacks.end()) {
        return;
    }

    if (found->second.activeCalls != 0) {
        found->second.retired = true;
    }
    else {
        auto retired = sCallbacks.extract(found);
    }
}

bool Callback_Active(u64 token) {
    auto found = sCallbacks.find(token);
    return found != sCallbacks.end() && found->second.activeCalls != 0;
}

bool Callbacks_Stopping() {
    return sStoppingCallbacks;
}

} // namespace ModLoader::N64::Detail

bool ModLoader::Runtime::Platform_Hypercall(u64 handler, u32 processor, void* state, u64 size) {
    if (processor != MODLOADER_N64_PROCESSOR_VR4300 || size != sizeof(N64::Cpu_State) || state == nullptr) {
        return false;
    }

    auto found = sCallbacks.find(handler);
    if (found == sCallbacks.end() || found->second.retired) {
        return false;
    }

    Callback_Record* callback = &found->second;
    callback->activeCalls++;
    bool handled = callback->handler(*static_cast<N64::Cpu_State*>(state));
    callback->activeCalls--;

    // Self-removal waits for the outermost call
    if (callback->activeCalls == 0 && callback->retired) {
        auto retired = sCallbacks.extract(handler);
    }
    return handled;
}

void ModLoader::Runtime::Platform_Shutdown() {
    if (sStoppingCallbacks) {
        return;
    }

    sStoppingCallbacks = true;
    Patches_Shutdown();

    while (!sHypercallCallbacks.empty()) {
        N64::Hypercall::Handle id = sHypercallCallbacks.begin()->first;
        if (ModLoader_Host_Hypercall_Register(id, 0) == 0) {
            return;
        }
        Remove_Callback(sHypercallCallbacks, id);
    }
}

ModLoader::N64::Hypercall::Handle ModLoader::N64::Hypercall::Allocate() {
    return ModLoader_Host_Hypercall_Allocate();
}

bool ModLoader::N64::Hypercall::Register(Handle id, Callback handler) {
    bool has_handler = static_cast<bool>(handler);
    u64 token = has_handler ? Add_Callback(std::move(handler)) : 0;
    if ((has_handler && token == 0) || ModLoader_Host_Hypercall_Register(id, token) == 0) {
        Remove_Callback(token);
        return false;
    }
    
    Remove_Callback(sHypercallCallbacks, id);
    if (token != 0) {
        sHypercallCallbacks.emplace(id, token);
    }
    return true;
}
