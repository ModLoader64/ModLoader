#include <modloader/async/coroutine.h>

#include <memory>
#include <utility>

namespace {

struct Wait {
    u32 awaited;
    u32 owner;
    void* frame;
    bool propagate;
    u64 awaitedContinuation = 0;
    u64 ownerContinuation = 0;
};

void Finish(const std::shared_ptr<Wait>& wait) {
    using namespace ModLoader;
    void* frame = std::exchange(wait->frame, nullptr);
    if (frame == nullptr) {
        return;
    }
    
    Promise_Core::Remove_Continuation(wait->awaited, wait->awaitedContinuation);
    Promise_Core::Remove_Continuation(wait->owner, wait->ownerContinuation);
    auto coroutine = std::coroutine_handle<>::from_address(frame);
    if (Promise_Core::Get_State(wait->owner) != Promise_Core::State::Pending) {
        coroutine.destroy();
    }
    else if (wait->propagate && Promise_Core::Get_State(wait->awaited) == Promise_Core::State::Rejected) {
        Promise_Core::Reject(wait->owner, Promise_Core::Get_Error(wait->awaited));
        coroutine.destroy();
    }
    else {
        coroutine.resume();
    }
}

} // namespace

void ModLoader::Coroutine_Core::Suspend_On(u32 awaited, void* frame, u32 owner, bool propagate) {
    auto wait = std::make_shared<Wait>(Wait{awaited, owner, frame, propagate});
    wait->awaitedContinuation = Promise_Core::Finally(awaited, [wait] {
        Finish(wait);
    });
    wait->ownerContinuation = Promise_Core::Finally(owner, [wait] {
        Finish(wait);
    });
    if (wait->awaitedContinuation == 0 || wait->ownerContinuation == 0) {
        Promise_Core::Reject(owner, Error::Failed);
        Finish(wait);
    }
}
