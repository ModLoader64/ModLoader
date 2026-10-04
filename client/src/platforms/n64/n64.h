#pragma once

#include "module.h"

#include "modloader_n64_module.h"

constexpr u32 gKseg0 = 0x80000000u;
constexpr u32 gKseg1 = 0xA0000000u;
constexpr u32 gKseg1RamEnd = 0x03F00000u;
constexpr u32 gRomCpu = 0xB0000000u;
constexpr u32 gRomCpuEnd = 0x0FC00000u;
constexpr u32 gRomPi = 0x10000000u;

struct Event_Clock {
    u64 count = 0;
    u64 lastTime = 0; // emulated nanoseconds
    f32 lastSeconds = 0.0f; // reused after loading a state
    bool timeKnown = false;

    f32 Tick(u64 time);
};

class N64_Platform : public Platform {
public:
    N64_Platform();

    bool Load_Image(const std::string& path, Game_Image& out_image) override;
    void Pack_Controls(std::span<const f32> values, std::vector<u8>& out_state) const override;
    void Register_Natives() override;
    bool Start(Runtime& owner) override;
    bool Map_Address(u32 processor, u64 address, u32& out_space, u64& out_offset) const override;
    u64 Fixed_Address(u32 processor, u32 space, u64 offset) const override;
    void Event_Record(u32 event, u64 time, const void* data, u64 size, std::vector<u8>& out_record) override;
    s64 Query(Runtime& owner, u32 query, void* output, u64 capacity) override;
    u64 Clock_Now(u32 clock) const override;
    void Reset() override;
    void State_Loaded() override;
    void Save_State(const ModLoader_Platform_Adapter& adapter, void* handle) override;
    void Retry_Save_State(const ModLoader_Platform_Adapter& adapter, void* handle) override;
    bool Load_State(const ModLoader_Platform_Adapter& adapter, void* handle) override;

    const ModLoader_Space_Descriptor* Rom() const {
        return romSpace < runtime->config.spaces.size() ? &runtime->config.spaces[romSpace] : nullptr;
    }

    ModLoader_N64_Image image = {};
    Runtime* runtime = nullptr;
    u32 romSpace = 0;

private:
    bool Savestates_Allowed() const;
    std::optional<std::vector<u8>> Pack_Image_Changes() const;
    void Restore_Image();
    std::string statePath;
    u32 saveRetries = 0;
    std::optional<std::vector<u8>> pendingImageChanges;
    Event_Clock vi;
    Event_Clock tasks;
    Event_Clock frames;
};

