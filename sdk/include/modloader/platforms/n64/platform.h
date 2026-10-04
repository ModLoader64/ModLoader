#pragma once

#include <modloader/rom.h>
#include <modloader/async/clock.h>
#include <modloader/guest/types.h>
#include <modloader_n64_module.h>

namespace ModLoader {

namespace N64 {

struct Task {
    u64 taskCount;
    f32 taskTime; // emulated seconds since the previous one
};

struct Vertical_Interrupt {
    u64 viCount;
    f32 viTime; // emulated seconds since the previous VI
    u32 status; // VI_STATUS register
    Guest::uptr origin; // KSEG0 framebuffer address
    u32 width; // pixels per line
};

} // namespace N64

struct Frame {
    u64 frameCount;
    f32 frameTime; // emulated seconds since the previous one
    Guest::uptr origin; // KSEG0 framebuffer address
    u32 width; // pixels per line
};

namespace N64::Clocks {
inline constexpr Clock VI = static_cast<Clock>(MODLOADER_N64_CLOCK_VI);
inline constexpr Clock Task = static_cast<Clock>(MODLOADER_N64_CLOCK_TASK);
inline constexpr Clock Frame = static_cast<Clock>(MODLOADER_N64_CLOCK_FRAME);
}

struct Platform {
    Guest::uptr ramStart;
    Guest::uptr ramEnd; // end of the reported RDRAM
    Guest::uptr extendedRamEnd; // end of extended RDRAM
    Guest::uptr romStart;
    Guest::uptr romEnd;
    Rom_Info rom;

    // Convert a physical/KSEG0/KSEG1 RDRAM address to KSEG0
    Guest::uptr Ram_Address(this const Platform& self, Guest::uptr address);
    bool Is_Ram(this const Platform& self, Guest::uptr address);
    bool Is_Extended_Ram(this const Platform& self, Guest::uptr address);
    bool Is_Rom(this const Platform& self, Guest::uptr address);
    bool Resize_Rom(this const Platform& self, Guest::usize bytes);
    // Resolves the live TLB; 0 when unmapped
    Guest::uptr Translate(this const Platform& self, Guest::uptr address);

    template <typename T>
    T Read(this const Platform&, Guest::uptr address) {
        return *reinterpret_cast<space<cpu> T*>(address);
    }

    template <typename T>
    void Write(this const Platform&, Guest::uptr address, T value) {
        *reinterpret_cast<space<cpu> T*>(address) = value;
    }

};

// Filled before On_Prepare
extern const Platform& gPlatform;

} // namespace ModLoader

extern "C" {

#define MODLOADER_LIFECYCLE(field, arguments, pre, normal, post, event) \
    void pre arguments; \
    void normal arguments; \
    void post arguments;
#include <modloader/platforms/n64/lifecycle.def>
#undef MODLOADER_LIFECYCLE

}
