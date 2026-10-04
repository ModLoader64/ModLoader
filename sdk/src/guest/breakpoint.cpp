#include "../internal.h"

#include <modloader/guest/breakpoint.h>

#include <memory>
#include <unordered_map>
#include <utility>

using namespace ModLoader;
using Guest::Breakpoint;

namespace {

struct Record {
    u32 processor;
    u64 address;
    u64 size;
    u32 access;
    Function<bool(const Breakpoint::Hit&, std::span<u8>)> handler;
    Breakpoint::Handle id = 0;
};

[[clang::no_destroy]] std::unordered_map<u64, std::shared_ptr<Record>> sBreakpoints;
u64 sNextToken = 1;
u64 sStepToken;
bool sStopping;

std::shared_ptr<Record> Find(u64 token) {
    auto found = sBreakpoints.find(token);
    return found != sBreakpoints.end() ? found->second : nullptr;
}

u64 Insert(Record record) {
    if (sStopping || sNextToken == 0 || !record.handler) {
        return 0;
    }
    u64 token = sNextToken++;
    sBreakpoints.emplace(token, std::make_shared<Record>(std::move(record)));
    return token;
}

void Disable(Record& record) {
    if (Breakpoint::Handle id = std::exchange(record.id, 0)) {
        ModLoader_Host_Breakpoint_Remove(id);
    }
}

void Erase(u64 token) {
    auto retired = sBreakpoints.extract(token);
    if (!retired.empty()) {
        Disable(*retired.mapped());
    }
}

bool Stop_Next(u32 processor, Function<bool(const Breakpoint::Hit&, std::span<u8>)> handler) {
    u64 token = Insert({processor, 0, 0, 0, std::move(handler)});
    if (token == 0) {
        return false;
    }
    Erase(std::exchange(sStepToken, token));
    ModLoader_Host_Breakpoint_Resume(processor, 1, token);
    return true;
}

} // namespace

Breakpoint::~Breakpoint() {
    Destroy();
}

Breakpoint::Breakpoint(Breakpoint&& other) noexcept : token(std::exchange(other.token, 0)) {
}

Breakpoint& Breakpoint::operator=(Breakpoint&& other) noexcept {
    if (this != &other) {
        Destroy();
        token = std::exchange(other.token, 0);
    }
    return *this;
}

bool Breakpoint::Create(u32 processor, u64 address, u64 size, u32 access, Handler handler) {
    access &= Execute | Read | Write;
    if (Is_Created() || size == 0 || size > UINT64_MAX - address || access == 0) {
        return false;
    }
    token = Insert({processor, address, size, access, std::move(handler)});
    return token != 0;
}

bool Breakpoint::Enable() {
    auto record = Find(token);
    if (record == nullptr) {
        return false;
    }

    if (record->id == 0) {
        record->id = ModLoader_Host_Breakpoint_Add(record->processor, record->address,
            record->size, record->access, token);
    }
    return record->id != 0;
}

bool Breakpoint::Disable() {
    if (auto record = Find(token)) {
        ::Disable(*record);
    }
    return true;
}

bool Breakpoint::Destroy() {
    Erase(std::exchange(token, 0));
    return true;
}

bool Breakpoint::Is_Created() const {
    return sBreakpoints.contains(token);
}

bool Breakpoint::Is_Enabled() const {
    return Id() != 0;
}

Breakpoint::Handle Breakpoint::Id() const {
    auto record = Find(token);
    return record != nullptr ? record->id : 0;
}

Breakpoint::Handle Breakpoint::Add(u32 processor, u64 address, u64 size, u32 access, Handler handler) {
    Breakpoint breakpoint;
    if (!breakpoint.Create(processor, address, size, access, std::move(handler)) || !breakpoint.Enable()) {
        return 0;
    }
    Handle id = breakpoint.Id();
    breakpoint.token = 0;
    return id;
}

void Breakpoint::Remove(Handle id) {
    if (id == 0) {
        return;
    }
    for (const auto& [token, record] : sBreakpoints) {
        if (record->id == id) {
            Erase(token);
            return;
        }
    }
}

void Breakpoint::Resume() {
    Erase(std::exchange(sStepToken, 0));
    ModLoader_Host_Breakpoint_Resume(0, 0, 0);
}

bool Breakpoint::Pause(u32 processor, Handler handler) {
    return !Paused() && Stop_Next(processor, std::move(handler));
}

bool Breakpoint::Step(u32 processor, Handler handler) {
    return Paused() && Stop_Next(processor, std::move(handler));
}

bool Breakpoint::Paused() {
    return ModLoader_Host_Breakpoint_Paused() != 0;
}

bool Breakpoint::Get_Cpu(u32 processor, std::span<u8> state) {
    return ModLoader_Host_Breakpoint_Cpu(processor, state.data(), state.size(), 0) != 0;
}

bool Breakpoint::Set_Cpu(u32 processor, std::span<const u8> state) {
    return ModLoader_Host_Breakpoint_Cpu(processor, const_cast<u8*>(state.data()), state.size(), 1) != 0;
}

bool ModLoader::Runtime::Breakpoint_Event(u64 token, u32 processor, void* state, u64 size, const ModLoader_Break& hit) {
    auto record = Find(token);
    if (record == nullptr || record->processor != processor || hit.processor != processor ||
        state == nullptr || (hit.id != 0 ? hit.id != record->id : token != sStepToken)) {
        return false;
    }
    if (hit.id == 0) {
        Erase(std::exchange(sStepToken, 0));
    }

    return record->handler(hit, {static_cast<u8*>(state), size});
}

void ModLoader::Runtime::Breakpoints_Shutdown() {
    sStopping = true;
    sStepToken = 0;
    while (!sBreakpoints.empty()) {
        Erase(sBreakpoints.begin()->first);
    }
}
