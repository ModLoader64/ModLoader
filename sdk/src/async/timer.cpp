#include "../internal.h"

#include <modloader/async/timer.h>

#include <algorithm>
#include <vector>

namespace {

constexpr u64 gFirstCustomClock = u64{1} << 32;

struct Timer_Record {
    ModLoader::Timer::Handle id;
    u64 clock;
    u64 deadline;
    u64 period;
    ModLoader::Function<void()> callback;
    std::weak_ptr<ModLoader::Detail::Subscription_State> subscription;
};

[[clang::no_destroy]] std::vector<std::shared_ptr<Timer_Record>> sTimers;
[[clang::no_destroy]] std::vector<u64> sCustomNow;
ModLoader::Timer::Handle sNextId;
u32 sDispatchDepth;
bool sStoppingTimers;

u64 Add_Ticks(u64 now, u64 count) {
    return now + std::min(count, static_cast<u64>(-1) - now);
}

bool Follow_Clock(u64 clock, bool add) {
    u32 event;

    if (clock >= gFirstCustomClock) {
        return clock - gFirstCustomClock < sCustomNow.size();
    }

    if (clock == MODLOADER_CLOCK_MILLISECONDS) {
        if (!ModLoader::Runtime::Follow_Event(MODLOADER_EVENT_REFRESH, add)) {
            return false;
        }

        if (!ModLoader::Runtime::Follow_Event(MODLOADER_EVENT_PAUSED, add)) {
            ModLoader::Runtime::Follow_Event(MODLOADER_EVENT_REFRESH, false);
            return false;
        }
        return true;
    }

    if (ModLoader::Runtime::Platform_Clock_Event(static_cast<u32>(clock), &event)) {
        return ModLoader::Runtime::Follow_Event(event, add);
    }
    return false;
}

void Retire_Timer(const Timer_Record& timer) {
    Follow_Clock(timer.clock, false);
    if (auto state = timer.subscription.lock()) {
        state->Dismiss();
    }
}

std::shared_ptr<Timer_Record> Add_Timer(ModLoader::Clock clock, u64 count, u64 period, ModLoader::Function<void()> callback) {
    if (sStoppingTimers || sNextId == static_cast<ModLoader::Timer::Handle>(-1) || !callback || !Follow_Clock(static_cast<u64>(clock), true)) {
        return {};
    }

    if (sDispatchDepth == 0) {
        std::erase(sTimers, nullptr);
    }
    auto timer = std::make_shared<Timer_Record>();
    timer->id = ++sNextId;
    timer->clock = static_cast<u64>(clock);
    timer->deadline = Add_Ticks(ModLoader::Timer::Now(clock), count);
    timer->period = period;
    timer->callback = std::move(callback);
    sTimers.push_back(timer);
    return timer;
}

ModLoader::Subscription Subscribe_Timer(ModLoader::Clock clock, u64 count, u64 period, ModLoader::Function<void()> callback) {
    auto timer = Add_Timer(clock, count, period, std::move(callback));
    if (timer == nullptr) {
        return {};
    }

    auto state = std::make_shared<ModLoader::Detail::Subscription_State>();
    state->cleanup = [id = timer->id] { ModLoader::Timer::Cancel(id); };
    timer->subscription = state;
    return ModLoader::Subscription(std::move(state));
}

} // namespace

ModLoader::Timer::Handle ModLoader::Timer::Timeout(Clock clock, u64 count, Function<void()> callback) {
    auto timer = Add_Timer(clock, count, 0, std::move(callback));
    return timer != nullptr ? timer->id : 0;
}

ModLoader::Timer::Handle ModLoader::Timer::Interval(Clock clock, u64 count, Function<void()> callback) {
    auto timer = Add_Timer(clock, count, count != 0 ? count : 1, std::move(callback));
    return timer != nullptr ? timer->id : 0;
}

ModLoader::Subscription ModLoader::Timer::Schedule(Clock clock, u64 count, Function<void()> callback) {
    return Subscribe_Timer(clock, count, 0, std::move(callback));
}

ModLoader::Subscription ModLoader::Timer::Repeat(Clock clock, u64 count, Function<void()> callback) {
    return Subscribe_Timer(clock, count, count != 0 ? count : 1, std::move(callback));
}

void ModLoader::Timer::Cancel(Handle timer) {
    if (timer == 0) {
        return;
    }

    for (auto& slot : sTimers) {
        if (slot != nullptr && slot->id == timer) {
            auto retired = std::exchange(slot, nullptr);
            Retire_Timer(*retired);
            return;
        }
    }
}

u64 ModLoader::Timer::Now(Clock clock) {
    const u64 number = static_cast<u64>(clock);
    if (number < gFirstCustomClock) {
        return ModLoader_Host_Clock_Now(static_cast<u32>(number));
    }
    const usize index = number - gFirstCustomClock;
    return index < sCustomNow.size() ? sCustomNow[index] : 0;
}

ModLoader::Clock ModLoader::Timer::Create_Clock() {
    if (sStoppingTimers) {
        return Invalid_Clock;
    }
    const u64 number = gFirstCustomClock + sCustomNow.size();
    sCustomNow.push_back(0);
    return static_cast<Clock>(number);
}

void ModLoader::Timer::Tick(Clock clock, u64 count) {
    const u64 number = static_cast<u64>(clock);
    if (number < gFirstCustomClock || number - gFirstCustomClock >= sCustomNow.size()) {
        return;
    }
    const usize index = number - gFirstCustomClock;
    sCustomNow[index] = Add_Ticks(sCustomNow[index], count);
    Runtime::Advance_Clock(number, sCustomNow[index]);
}

void ModLoader::Runtime::Advance_Clock(u64 clock, u64 now) {
    const usize count = sTimers.size();
    sDispatchDepth++;
    for (usize index = 0; index < count && !sStoppingTimers; index++) {
        auto timer = sTimers[index];
        if (timer == nullptr || timer->clock != clock || timer->deadline > now) {
            continue;
        }

        if (timer->period != 0 && now != static_cast<u64>(-1)) {
            const u64 next = Add_Ticks(timer->deadline, timer->period);
            timer->deadline = next > now ? next : Add_Ticks(now, timer->period);
        }
        else {
            Timer::Cancel(timer->id);
        }
        timer->callback();
    }
    
    if (--sDispatchDepth == 0) {
        std::erase(sTimers, nullptr);
    }
}

void ModLoader::Runtime::Timers_Shutdown() {
    sStoppingTimers = true;
    auto retired = std::exchange(sTimers, {});
    for (const auto& timer : retired) {
        if (timer != nullptr) {
            Retire_Timer(*timer);
        }
    }
    sCustomNow.clear();
}

ModLoader::Promise<void> ModLoader::Timer::Delay(Clock clock, u64 count) {
    auto promise = Promise<void>::Create();
    const Handle timer = Timeout(clock, count, [promise] { promise.Resolve(); });
    if (timer == 0) {
        promise.Reject(Error::Failed);
    }
    else {
        promise.Finally([timer] { Cancel(timer); });
    }
    return promise;
}

