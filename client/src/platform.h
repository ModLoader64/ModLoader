#pragma once

#include "base.h"
#include <memory>

class Runtime;
struct ModLoader_Platform_Adapter;

struct Game_Image {
    std::vector<u8> data;
    std::string title;
    std::string sha1;
};

class Platform {
public:
    std::string identifier;
    const char* ramSpace = nullptr;
    const char* imageSpace = nullptr;
    bool bigEndian = false;
    bool savestates = false;
    u32 lastHypercall = UINT32_MAX;
    u32 clockCount = 0;
    std::span<const char* const> controls;

    virtual ~Platform() = default;

    virtual bool Load_Image(const std::string& path, Game_Image& image);

    virtual void Pack_Controls(std::span<const f32>, std::vector<u8>& out_state) const {
        out_state.clear();
    }

    virtual void Register_Natives() {}

    virtual bool Start(Runtime&) {
        return true;
    }

    virtual bool Map_Address(u32, u64, u32&, u64&) const {
        return false;
    }

    virtual u64 Fixed_Address(u32, u32, u64) const {
        return 0;
    }

    virtual void Event_Record(u32 event, u64 time, const void* data, u64 size, std::vector<u8>& out_record);
    virtual s64 Query(Runtime& runtime, u32 query, void* output, u64 capacity);
    virtual u64 Clock_Now(u32) const {
        return 0;
    }

    virtual void Reset() {}
    virtual void State_Loaded() {}
    virtual void Save_State(const ModLoader_Platform_Adapter&, void*) {}
    virtual void Retry_Save_State(const ModLoader_Platform_Adapter&, void*) {}

    virtual bool Load_State(const ModLoader_Platform_Adapter&, void*) {
        return false;
    }
};

using Platform_Factory = std::unique_ptr<Platform> (*)();
void Platform_Register(std::string_view identifier, Platform_Factory factory);

std::unique_ptr<Platform> Platform_Create(std::string_view identifier);
