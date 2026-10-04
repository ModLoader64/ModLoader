// / and relative paths use the module's mount
// assets:/ reads read-only bundled module assets (.pak)
#pragma once

#include <modloader/function.h>
#include <modloader/async/promise.h>
#include <modloader/types.h>

#include <optional>
#include <memory>
#include <string_view>
#include <vector>

namespace ModLoader::File {

enum class Mode : u32 {
    Read = 0,
    Write = 1, // creates or truncates
    Append = 2,
    Read_Write = 3, // creates when missing, keeps the contents
};

// Copies share ownership; Close invalidates every copy; the last copy closes automatically
class Handle {
public:
    Handle() = default;
    explicit Handle(u32 id);

    // Absolute byte offsets; returns bytes transferred or -1; reads stop at end of file
    s64 Read(void* buffer, u64 size, u64 offset) const;
    s64 Write(const void* data, u64 size, u64 offset) const;
    s64 Size() const; // Byte length, or -1 on failure.
    void Close();

    bool Is_Open() const;

private:
    struct State;
    u32 Id() const;
    std::shared_ptr<State> state;
};

// Paths use this module's mounts
// Write modes create missing directories
Handle Open(std::string_view path, Mode mode);
bool Delete(std::string_view path);
bool Exists(std::string_view path);
bool Create_Directory(std::string_view path);
// Calls each(name, is_folder) in name order; names last until the callback returns
bool List(std::string_view path, Function<void(std::string_view name, bool is_folder)> each);
// Read failure returns nullopt; writes create or replace the file
std::optional<std::vector<u8>> Read_All(std::string_view path);
bool Write_All(std::string_view path, const void* data, u64 size);
// Inputs are copied; reads reject on failure; writes resolve false on failure
Promise<std::vector<u8>> Read_All_Async(std::string_view path);
Promise<bool> Write_All_Async(std::string_view path, const void* data, u64 size);

} // namespace ModLoader::File


