// hypercall code    000000 code[19:0] 101000    SPECIAL funct 0x28; the ID is the 20-bit code
// hypercallr rs     000000 rs 000000000000000 101001    SPECIAL funct 0x29; the ID is in GPR rs
#pragma once

#include <modloader/types.h>
#include <modloader/function.h>

namespace ModLoader::N64 {

// General-purpose register indices for Cpu_State::gpr and Get_Word/Set_Word
enum Gpr : u32 {
    Zero = 0,
    At = 1,
    V0 = 2,
    V1 = 3,
    A0 = 4,
    A1 = 5,
    A2 = 6,
    A3 = 7,
    T0 = 8,
    T1 = 9,
    T2 = 10,
    T3 = 11,
    T4 = 12,
    T5 = 13,
    T6 = 14,
    T7 = 15,
    S0 = 16,
    S1 = 17,
    S2 = 18,
    S3 = 19,
    S4 = 20,
    S5 = 21,
    S6 = 22,
    S7 = 23,
    T8 = 24,
    T9 = 25,
    K0 = 26,
    K1 = 27,
    Gp = 28,
    Sp = 29,
    Fp = 30,
    Ra = 31,
};

constexpr u32 gStatusFr = 1u << 26;

// Callback CPU state; changes are written back except for status, cause, and count
struct Cpu_State {
    u64 gpr[32]; // writes to gpr[0] are ignored
    u64 hi;
    u64 lo;
    u64 pc; // current instruction; hypercalls resume at pc + 4 unless the handler changes pc
    u64 fpr[32]; // the physical 64-bit registers; with Status.FR clear, $f(2n+1) is the high half of fpr[2n]
    u32 fcr31;
    u32 status; // read-only
    u32 cause; // read-only
    u32 count; // read-only

    // Single-precision register $f<index> as the game sees it in either FR mode
    f32 Get_Single(this const Cpu_State& self, u32 index) {
        u32 bits;

        if ((self.status & gStatusFr) != 0 || (index & 1) == 0) {
            bits = static_cast<u32>(self.fpr[index]);
        }
        else {
            bits = static_cast<u32>(self.fpr[index - 1] >> 32);
        }
        return __builtin_bit_cast(f32, bits);
    }

    void Set_Single(this Cpu_State& self, u32 index, f32 value) {
        u64 bits = __builtin_bit_cast(u32, value);

        if ((self.status & gStatusFr) != 0 || (index & 1) == 0) {
            self.fpr[index] = (self.fpr[index] & 0xFFFFFFFF00000000ull) | bits;
        }
        else {
            self.fpr[index - 1] = (self.fpr[index - 1] & 0xFFFFFFFFull) | (bits << 32);
        }
    }

    // Double-precision register $f<index>; with Status.FR clear the index must be even
    f64 Get_Double(this const Cpu_State& self, u32 index) {
        return __builtin_bit_cast(f64, self.fpr[index]);
    }

    void Set_Double(this Cpu_State& self, u32 index, f64 value) {
        self.fpr[index] = __builtin_bit_cast(u64, value);
    }

    // Low 32 bits of a GPR
    u32 Get_Word(this const Cpu_State& self, u32 index) {
        return static_cast<u32>(self.gpr[index]);
    }

    void Set_Word(this Cpu_State& self, u32 index, u32 value) {
        self.gpr[index] = static_cast<u64>(static_cast<s64>(static_cast<s32>(value)));
    }
};

namespace Hypercall {

using Handle = u32;

// Returns true when it handled the hypercall
using Callback = Function<bool(Cpu_State& state)>;

// IDs start at 1; returns 0 when none are left
Handle Allocate();
// an empty handler unregisters
bool Register(Handle id, Callback handler);

// The instruction words, for code patches
constexpr u32 Encode(Handle id) {
    return ((id & 0xFFFFF) << 6) | 0x28;
}

constexpr u32 Encode_Register_Form(u32 rs) {
    return ((rs & 31) << 21) | 0x29;
}

} // namespace Hypercall

} // namespace ModLoader::N64
