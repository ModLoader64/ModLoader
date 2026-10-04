#pragma once

#include <modloader/detail/coroutine.h>

namespace ModLoader {

// Settled() returns an error alongside the value; a rejected promise supplies a default value
template <typename T>
struct Result {
    Error error;
    T value;
};

template <>
struct Result<void> {
    Error error;
};

// Start on the main thread; execution begins immediately and resumes there after suspension
// Cancelling the returned promise destroys its suspended frame without cancelling the awaited operation
// Awaited nonvoid values must support copying and default construction
template <typename T>
class Coroutine : public Promise<T> {
public:
    using promise_type = Coroutine_Core::Promise_Type<T>;

    explicit Coroutine(const Promise<T>& promise) : Promise<T>(promise) {}
};

template <typename T>
Coroutine<T> Coroutine_Core::Promise_Type_Base<T>::get_return_object() {
    return Coroutine<T>(this->result);
}

template <typename T>
Coroutine<T> Coroutine_Core::Promise_Type_Base<T>::get_return_object_on_allocation_failure() {
    return Coroutine<T>(Promise<T>::Rejected(Error::Failed));
}

// Yields the value on success; rejection ends this coroutine and rejects its returned promise
template <typename T>
Coroutine_Core::Awaiter<T, true> operator co_await(const Promise<T>& promise) {
    return { promise };
}

// Handles rejection locally: co_await Settled(promise) returns Result<T> instead of ending the coroutine
template <typename T>
Coroutine_Core::Awaiter<T, false> Settled(const Promise<T>& promise) {
    return { promise };
}

} // namespace ModLoader

