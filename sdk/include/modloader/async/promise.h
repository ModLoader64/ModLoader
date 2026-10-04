#pragma once

#include <modloader/detail/promise.h>
#include <modloader/types.h>

#include <memory>
#include <type_traits>
#include <utility>

namespace ModLoader {

enum class Error : s32 {
    None = 0,
    Timeout = 1,
    Cancelled = 2,
    Failed = 3,
};

const char* Error_Name(Error error); // Static lowercase text

// Shared completion state
// Copies refer to the same result; only the first Resolve/Reject/Cancel succeeds
// Callbacks run later on the main thread, including callbacks added to an already settled promise
// Resolving and registering callbacks are thread safe. Timeout() must be called on the main thread
template<typename T = void>
class Promise {
public:
    using Callback = typename Promise_Core::Value_Callback<T>::Type;

    static Promise Create() {
        return Adopt(Promise_Core::Create());
    }

    // Adopts a reference returned by Create or Retain
    static Promise Adopt(u32 handle) {
        return Promise(handle);
    }

    template<typename Value = T>
    static Promise Resolved(std::type_identity_t<Value> value) requires (!std::is_void_v<Value>) {
        Promise promise = Create();
        promise.Resolve(std::move(value));
        return promise;
    }

    static Promise Resolved() requires std::is_void_v<T> {
        Promise promise = Create();
        promise.Resolve();
        return promise;
    }

    static Promise Rejected(Error error) {
        Promise promise = Create();
        promise.Reject(error);
        return promise;
    }

    Promise() = default; // Empty handle: rejected with Error::Failed

    // Stores an owned value, callbacks receive a const reference valid for the duration of their call
    template<typename Value = T>
    bool Resolve(std::type_identity_t<Value> value) const requires (!std::is_void_v<Value> && std::is_same_v<Value, T>) {
        if (!Is_Pending()) {
            return false;
        }
        return Promise_Core::Resolve(Handle(), std::make_shared<const T>(std::move(value)));
    }

    bool Resolve() const requires std::is_void_v<T> {
        return Promise_Core::Resolve(Handle());
    }

    bool Reject(Error error) const {
        return Promise_Core::Reject(Handle(), error);
    }

    bool Cancel() const { // Rejects with Cancelled; running tasks stop only when they check their token
        return Reject(Error::Cancelled);
    }

    // Register success/failure/completion callbacks; returns false if registration failed
    template<typename Callable>
    bool Try_Then(Callable callback) const {
        if constexpr (std::is_pointer_v<Callable> || std::is_same_v<Callable, Callback>) {
            if (!callback) {
                return false;
            }
        }

        return Promise_Core::Then(Handle(), [callback = std::move(callback)](const void* value) mutable {
            if constexpr (std::is_void_v<T>) {
                callback();
            }
            else {
                callback(*static_cast<const T*>(value));
            }
        }) != 0;
    }

    bool Try_Catch(Function<void(Error)> callback) const {
        return Promise_Core::Catch(Handle(), std::move(callback)) != 0;
    }

    bool Try_Finally(Function<void()> callback) const {
        return Promise_Core::Finally(Handle(), std::move(callback)) != 0;
    }

    // Chainable registration; registration failure rejects a pending promise with Failed
    // These return the same promise, not a transformed result
    template<typename Callable>
    const Promise& Then(Callable callback) const {
        Register(Try_Then(std::move(callback)));
        return *this;
    }

    const Promise& Catch(Function<void(Error)> callback) const {
        Register(Try_Catch(std::move(callback)));
        return *this;
    }

    const Promise& Finally(Function<void()> callback) const {
        Register(Try_Finally(std::move(callback)));
        return *this;
    }

    // Reject with Timeout after count clock ticks
    const Promise& Timeout(Clock clock, u64 count) const {
        Register(Promise_Core::Timeout(Handle(), clock, count));
        return *this;
    }

    // False if any chainable registration failed
    bool Callbacks_Registered() const {
        return Promise_Core::Callbacks_Registered(Handle());
    }

    bool Is_Pending() const {
        return Promise_Core::Get_State(Handle()) == Promise_Core::State::Pending;
    }

    bool Is_Fulfilled() const {
        return Promise_Core::Get_State(Handle()) == Promise_Core::State::Fulfilled;
    }

    bool Is_Rejected() const {
        return Promise_Core::Get_State(Handle()) == Promise_Core::State::Rejected;
    }

    // Null until fulfilled; retains the result independently of the promise
    std::shared_ptr<const T> Shared_Value() const requires (!std::is_void_v<T>) {
        return std::static_pointer_cast<const T>(Promise_Core::Get_Value(Handle()));
    }

    // Copies the result, or T{} if not fulfilled
    T Value() const requires (!std::is_void_v<T> && std::is_copy_constructible_v<T> && std::is_default_constructible_v<T>) {
        auto value = Shared_Value();
        return value ? *value : T{};
    }

    Error Get_Error() const {
        return Promise_Core::Get_Error(Handle());
    }

    u32 Handle() const {
        return reference.Handle();
    }

private:
    explicit Promise(u32 handle)
        : reference(handle) {
    }

    void Register(bool registered) const {
        if (!registered) {
            Promise_Core::Registration_Failed(Handle());
            Reject(Error::Failed);
        }
    }

    Promise_Core::Reference reference;
};

} // namespace ModLoader


