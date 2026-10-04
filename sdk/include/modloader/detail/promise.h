#pragma once

#include <modloader/function.h>
#include <modloader/async/clock.h>
#include <modloader/types.h>

#include <memory>

namespace ModLoader {

enum class Error : s32;

namespace Promise_Core {

enum class State : u32 {
    Pending,
    Fulfilled,
    Rejected,
};

u32 Create();
void Retain(u32 handle);
void Release(u32 handle);
bool Resolve(u32 handle, std::shared_ptr<const void> value = {});
bool Reject(u32 handle, Error error);
u64 Then(u32 handle, Function<void(const void*)> callback);
u64 Catch(u32 handle, Function<void(Error)> callback);
u64 Finally(u32 handle, Function<void()> callback);
bool Remove_Continuation(u32 handle, u64 continuation);
bool Timeout(u32 handle, Clock clock, u64 count);
void Registration_Failed(u32 handle);
bool Callbacks_Registered(u32 handle);
State Get_State(u32 handle);
Error Get_Error(u32 handle);
std::shared_ptr<const void> Get_Value(u32 handle);

class Reference {
public:
    Reference() = default;
    explicit Reference(u32 handle);
    Reference(const Reference& other);
    Reference(Reference&& other) noexcept;
    Reference& operator=(Reference other) noexcept;
    ~Reference();

    u32 Handle() const {
        return handle;
    }

private:
    u32 handle = 0;
};

template<typename T>
struct Value_Callback {
    using Type = Function<void(const T&)>;
};

template<>
struct Value_Callback<void> {
    using Type = Function<void()>;
};

} // namespace Promise_Core

} // namespace ModLoader

