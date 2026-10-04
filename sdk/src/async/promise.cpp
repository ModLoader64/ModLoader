#include "../internal.h"

#include <modloader/async/promise.h>
#include <modloader/async/timer.h>
#include <modloader/async/thread.h>

#include <memory>
#include <variant>
#include <vector>

namespace {

using ModLoader::Error;
using ModLoader::Function;
using ModLoader::Promise_Core::State;

constexpr u32 gIndexBits = 20;
constexpr u32 gIndexMask = (1u << gIndexBits) - 1;
constexpr u32 gGenerationMask = (1u << (32 - gIndexBits)) - 1;
constexpr u32 gDispatchLimit = 100000;

struct Continuation {
    using Value_Callback = Function<void(const void*)>;
    using Error_Callback = Function<void(Error)>;
    using Settled_Callback = Function<void()>;

    std::unique_ptr<Continuation> next;
    u64 id = 0;
    std::variant<Value_Callback, Error_Callback, Settled_Callback> callback;

    void Call(State state, const void* value, Error error) {
        if (auto* on_value = std::get_if<Value_Callback>(&callback)) {
            if (state == State::Fulfilled) {
                (*on_value)(value);
            }
        }
        else if (auto* on_error = std::get_if<Error_Callback>(&callback)) {
            if (state == State::Rejected) {
                (*on_error)(error);
            }
        }
        else {
            std::get<Settled_Callback>(callback)();
        }
    }
};

struct Record {
    u32 generation = 0;
    u32 references = 0;
    State state = State::Pending;
    Error error = Error::None;
    std::shared_ptr<const void> value;
    std::unique_ptr<Continuation> first;
    Continuation* last = nullptr;
    bool ready = false;
    bool callbacksRegistered = true;
    u32 nextReady = 0;
    u32 nextFree = 0;
};

ModLoader::Sync::Mutex sLock;
[[clang::no_destroy]] std::vector<Record> sRecords;
u32 sReadyFirst = 0;
u32 sReadyLast = 0;
u32 sFreeFirst = 0;
u64 sNextContinuation = 0;
bool sStopping = false;

u32 Handle_Of(u32 index) {
    return (index + 1) | ((sRecords[index].generation & gGenerationMask) << gIndexBits);
}

Record* Find(u32 handle) {
    u32 index = (handle & gIndexMask) - 1;
    if (handle == 0 || index >= sRecords.size()) {
        return nullptr;
    }

    Record& record = sRecords[index];
    if (record.references == 0 || (record.generation & gGenerationMask) != handle >> gIndexBits) {
        return nullptr;
    }
    return &record;
}

void Make_Ready(Record& record) {
    if (record.ready || record.state == State::Pending || !record.first) {
        return;
    }

    u32 index = static_cast<u32>(&record - sRecords.data());
    record.ready = true;
    record.references++;
    record.nextReady = 0;
    if (sReadyLast != 0) {
        sRecords[sReadyLast - 1].nextReady = index + 1;
    }
    else {
        sReadyFirst = index + 1;
        ModLoader_Host_Dispatch_Request();
    }
    sReadyLast = index + 1;
}

void Free_Continuations(std::unique_ptr<Continuation> first) {
    while (first) {
        auto next = std::move(first->next);
        first = std::move(next);
    }
}

template<typename Callback>
u64 Add(u32 handle, Callback callback) {
    if (!callback) {
        return 0;
    }

    std::unique_ptr<Continuation> continuation(new (std::nothrow) Continuation{.callback = std::move(callback)});
    if (!continuation) {
        return 0;
    }

    ModLoader::Sync::Scoped_Lock lock(sLock);
    Record* record = Find(handle);
    if (record == nullptr || sStopping) {
        return 0;
    }

    if (++sNextContinuation == 0) {
        ++sNextContinuation;
    }
    u64 id = continuation->id = sNextContinuation;
    Continuation* last = continuation.get();
    if (record->last != nullptr) {
        record->last->next = std::move(continuation);
    }
    else {
        record->first = std::move(continuation);
    }
    record->last = last;
    Make_Ready(*record);
    return id;
}

bool Settle(u32 handle, State state, std::shared_ptr<const void> value, Error error) {
    ModLoader::Sync::Scoped_Lock lock(sLock);
    Record* record = Find(handle);
    if (record == nullptr || record->state != State::Pending) {
        return false;
    }

    record->state = state;
    record->error = error;
    record->value = std::move(value);
    Make_Ready(*record);
    return true;
}

} // namespace

const char* ModLoader::Error_Name(Error error) {
    switch (error) {
    case Error::None:
        return "none";
    case Error::Timeout:
        return "timeout";
    case Error::Cancelled:
        return "cancelled";
    case Error::Failed:
        return "failed";
    default:
        return "error";
    }
}

u32 ModLoader::Promise_Core::Create() {
    Sync::Scoped_Lock lock(sLock);
    if (sStopping) {
        return 0;
    }

    u32 slot;
    if (sFreeFirst != 0) {
        slot = sFreeFirst - 1;
        sFreeFirst = sRecords[slot].nextFree;
    }
    else {
        slot = static_cast<u32>(sRecords.size());
        if (slot == gIndexMask) {
            return 0;
        }
        sRecords.emplace_back();
    }

    Record& record = sRecords[slot];
    ++record.generation;
    record.references = 1;
    record.state = State::Pending;
    record.error = Error::None;
    record.callbacksRegistered = true;
    return Handle_Of(slot);
}

void ModLoader::Promise_Core::Retain(u32 handle) {
    Sync::Scoped_Lock lock(sLock);
    if (Record* record = Find(handle)) {
        ++record->references;
    }
}

void ModLoader::Promise_Core::Release(u32 handle) {
    std::unique_ptr<Continuation> orphans;
    std::shared_ptr<const void> value;
    {
        Sync::Scoped_Lock lock(sLock);
        Record* record = Find(handle);
        if (record == nullptr) {
            return;
        }
        if (--record->references == 0) {
            orphans = std::move(record->first);
            value = std::move(record->value);
            record->last = nullptr;
            record->nextFree = sFreeFirst;
            sFreeFirst = static_cast<u32>(record - sRecords.data()) + 1;
        }
    }
    Free_Continuations(std::move(orphans));
}

bool ModLoader::Promise_Core::Resolve(u32 handle, std::shared_ptr<const void> value) {
    return Settle(handle, State::Fulfilled, std::move(value), Error::None);
}

bool ModLoader::Promise_Core::Reject(u32 handle, Error error) {
    return Settle(handle, State::Rejected, {}, error == Error::None ? Error::Failed : error);
}

u64 ModLoader::Promise_Core::Then(u32 handle, Function<void(const void*)> callback) {
    return Add(handle, std::move(callback));
}

u64 ModLoader::Promise_Core::Catch(u32 handle, Function<void(Error)> callback) {
    return Add(handle, std::move(callback));
}

u64 ModLoader::Promise_Core::Finally(u32 handle, Function<void()> callback) {
    return Add(handle, std::move(callback));
}
bool ModLoader::Promise_Core::Remove_Continuation(u32 handle, u64 id) {
    std::unique_ptr<Continuation> removed;
    {
        Sync::Scoped_Lock lock(sLock);
        Record* record = Find(handle);
        if (record == nullptr) {
            return false;
        }

        auto* next = &record->first;
        Continuation* previous = nullptr;
        while (*next && (*next)->id != id) {
            previous = next->get();
            next = &(*next)->next;
        }

        if (!*next) {
            return false;
        }
        removed = std::move(*next);
        *next = std::move(removed->next);
        if (record->last == removed.get()) {
            record->last = previous;
        }
    }
    return true;
}

bool ModLoader::Promise_Core::Timeout(u32 handle, Clock clock, u64 count) {
    if (Get_State(handle) != State::Pending) {
        return true;
    }

    Retain(handle);
    Reference reference(handle);
    Timer::Handle timer = Timer::Timeout(clock, count, [reference] {
        Reject(reference.Handle(), Error::Timeout);
    });
    if (timer == 0) {
        return false;
    }

    if (Finally(handle, [timer] {
            Timer::Cancel(timer);
        }) == 0) {
        Timer::Cancel(timer);
        return false;
    }
    return true;
}

void ModLoader::Promise_Core::Registration_Failed(u32 handle) {
    Sync::Scoped_Lock lock(sLock);
    if (Record* record = Find(handle)) {
        record->callbacksRegistered = false;
    }
}

bool ModLoader::Promise_Core::Callbacks_Registered(u32 handle) {
    Sync::Scoped_Lock lock(sLock);
    Record* record = Find(handle);
    return record != nullptr && record->callbacksRegistered;
}

ModLoader::Promise_Core::State ModLoader::Promise_Core::Get_State(u32 handle) {
    Sync::Scoped_Lock lock(sLock);
    Record* record = Find(handle);
    return record != nullptr ? record->state : State::Rejected;
}

ModLoader::Error ModLoader::Promise_Core::Get_Error(u32 handle) {
    Sync::Scoped_Lock lock(sLock);
    Record* record = Find(handle);
    return record != nullptr ? record->error : Error::Failed;
}

std::shared_ptr<const void> ModLoader::Promise_Core::Get_Value(u32 handle) {
    Sync::Scoped_Lock lock(sLock);
    Record* record = Find(handle);
    return record != nullptr && record->state == State::Fulfilled ? record->value : nullptr;
}

ModLoader::Promise_Core::Reference::Reference(u32 handle) : handle(handle) {
}

ModLoader::Promise_Core::Reference::Reference(const Reference& other) : handle(other.handle) {
    Retain(handle);
}

ModLoader::Promise_Core::Reference::Reference(Reference&& other) noexcept : handle(std::exchange(other.handle, 0)) {
}

ModLoader::Promise_Core::Reference& ModLoader::Promise_Core::Reference::operator=(Reference other) noexcept {
    std::swap(handle, other.handle);
    return *this;
}

ModLoader::Promise_Core::Reference::~Reference() {
    Release(handle);
}

void ModLoader::Runtime::Dispatch() {
    for (u32 round = 0; round < gDispatchLimit; ++round) {
        std::unique_ptr<Continuation> continuation;
        std::shared_ptr<const void> value;
        State state;
        Error error;
        u32 handle;
        {
            Sync::Scoped_Lock lock(sLock);
            if (sReadyFirst == 0) {
                return;
            }

            u32 index = sReadyFirst - 1;
            Record& record = sRecords[index];
            sReadyFirst = record.nextReady;
            if (sReadyFirst == 0) {
                sReadyLast = 0;
            }

            record.ready = false;
            continuation = std::move(record.first);
            if (continuation) {
                record.first = std::move(continuation->next);
            }

            if (!record.first) {
                record.last = nullptr;
            }

            state = record.state;
            error = record.error;
            value = record.value;
            handle = Handle_Of(index);
            Make_Ready(record);
        }

        if (continuation) {
            continuation->Call(state, value.get(), error);
            continuation.reset();
        }
        Promise_Core::Release(handle);
    }
    ModLoader_Host_Dispatch_Request();
}

void ModLoader::Runtime::Promises_Shutdown() {
    {
        Sync::Scoped_Lock lock(sLock);
        sStopping = true;
        for (Record& record : sRecords) {
            if (record.references != 0 && record.state == State::Pending) {
                record.state = State::Rejected;
                record.error = Error::Cancelled;
                Make_Ready(record);
            }
        }
    }

    for (;;) {
        Dispatch();
        Sync::Scoped_Lock lock(sLock);
        if (sReadyFirst == 0) {
            break;
        }
    }

    std::vector<Record> retired;
    {
        Sync::Scoped_Lock lock(sLock);
        retired.swap(sRecords);
        sReadyFirst = 0;
        sReadyLast = 0;
        sFreeFirst = 0;
    }
    
    for (auto& record : retired) {
        Free_Continuations(std::move(record.first));
        record.value.reset();
    }
}


