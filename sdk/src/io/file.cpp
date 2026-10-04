#include "../internal.h"
#include "../read_text.h"

#include <modloader/detail/mounts.h>
#include <modloader/async/task.h>

#include <string>
#include <atomic>

using namespace ModLoader;

struct File::Handle::State {
    explicit State(u32 id)
        : id(id) {
    }

    ~State() {
        Close();
    }

    void Close() {
        if (u32 closing = id.exchange(0)) {
            ModLoader_Host_File_Close(closing);
        }
    }

    std::atomic<u32> id;
};

File::Handle::Handle(u32 id)
    : state(id != 0 ? std::make_shared<State>(id) : nullptr) {
}

u32 File::Handle::Id() const {
    return state ? state->id.load() : 0;
}

bool File::Handle::Is_Open() const {
    return Id() != 0;
}

s64 File::Handle::Read(void* buffer, u64 size, u64 offset) const {
    return ModLoader_Host_File_Read(Id(), buffer, size, offset);
}

s64 File::Handle::Write(const void* data, u64 size, u64 offset) const {
    return ModLoader_Host_File_Write(Id(), data, size, offset);
}

s64 File::Handle::Size() const {
    return ModLoader_Host_File_Size(Id());
}

void File::Handle::Close() {
    if (state) {
        state->Close();
    }
}

File::Handle Detail::File_Open(std::string_view folder, std::string_view path, File::Mode mode) {
    return File::Handle(ModLoader_Host_File_Open(folder.data(), folder.size(), path.data(), path.size(), static_cast<u32>(mode)));
}

bool Detail::File_Delete(std::string_view folder, std::string_view path) {
    return ModLoader_Host_File_Delete(folder.data(), folder.size(), path.data(), path.size()) != 0;
}

bool Detail::File_Exists(std::string_view folder, std::string_view path) {
    return ModLoader_Host_File_Exists(folder.data(), folder.size(), path.data(), path.size()) != 0;
}

bool Detail::File_Create_Directory(std::string_view folder, std::string_view path) {
    return ModLoader_Host_File_Create_Directory(folder.data(), folder.size(), path.data(), path.size()) != 0;
}

bool Detail::File_List(std::string_view folder, std::string_view path, Function<void(std::string_view name, bool is_folder)> each) {
    auto listing = Runtime::Read_Text([&](char* buffer, u64 capacity) {
        return ModLoader_Host_File_List(folder.data(), folder.size(), path.data(), path.size(), buffer, capacity);
    });
    
    if (!listing) {
        return false;
    }

    std::string_view rest = *listing;
    while (!rest.empty()) {
        usize end = rest.find('\n');
        std::string_view line = rest.substr(0, end);
        bool is_folder = !line.empty() && line.back() == '/';

        rest = end != std::string_view::npos ? rest.substr(end + 1) : std::string_view();
        if (is_folder) {
            line.remove_suffix(1);
        }
        if (!line.empty() && each) {
            each(line, is_folder);
        }
    }
    return true;
}

std::optional<std::vector<u8>> Detail::File_Read_All(std::string_view folder, std::string_view path) {
    File::Handle file = File_Open(folder, path, File::Mode::Read);
    s64 size = file.Is_Open() ? file.Size() : -1;
    std::vector<u8> contents;

    if (size < 0) {
        return std::nullopt;
    }

    contents.resize(static_cast<usize>(size));
    if (file.Read(contents.data(), contents.size(), 0) != size) {
        return std::nullopt;
    }

    return contents;
}

bool Detail::File_Write_All(std::string_view folder, std::string_view path, const void* data, u64 size) {
    if (size > INT64_MAX || (size != 0 && data == nullptr)) {
        return false;
    }
    File::Handle file = File_Open(folder, path, File::Mode::Write);
    return file.Is_Open() && file.Write(data, size, 0) == static_cast<s64>(size);
}

Promise<std::vector<u8>> Detail::File_Read_All_Async(std::string_view folder, std::string_view path) {
    Promise<std::vector<u8>> promise = Promise<std::vector<u8>>::Create();
    Task::Submit(promise.Handle(), [promise, folder = std::string(folder), path = std::string(path)] {
        auto contents = File_Read_All(folder, path);
        if (contents) {
            promise.Resolve(std::move(*contents));
        }
        else {
            promise.Reject(Error::Failed);
        }
    });
    return promise;
}

Promise<bool> Detail::File_Write_All_Async(std::string_view folder, std::string_view path, const void* data, u64 size) {
    if (size > INT64_MAX || (size != 0 && data == nullptr)) {
        return Promise<bool>::Rejected(Error::Failed);
    }

    std::vector<u8> contents(static_cast<usize>(size));
    if (size != 0) {
        __builtin_memcpy(contents.data(), data, contents.size());
    }

    return Task::Run([folder = std::string(folder), path = std::string(path), contents = std::move(contents)] {
        return File_Write_All(folder, path, contents.data(), contents.size());
    });
}

