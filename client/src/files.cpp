#include "module.h"
#include "base/file.h"

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <filesystem>

struct Module_File {
    std::mutex lock;
    File_Handle file;
    bool writable = false;
};

namespace {

enum class File_Mode : u32 {
    Read = 0,
    Write = 1, // create or truncate
    Append = 2,
    Read_Write = 3, // create when missing, keep the contents
};

std::optional<std::string> Normalize_Path(std::string_view path) {
    if (path.find_first_of(std::string_view(":\\\0", 3)) != std::string_view::npos) {
        return std::nullopt;
    }

    std::string normalized;
    while (!path.empty()) {
        usize separator = path.find('/');
        std::string_view part = path.substr(0, separator);
        path.remove_prefix(separator == std::string_view::npos ? path.size() : separator + 1);
        if (part == "..") {
            if (normalized.empty()) {
                return std::nullopt;
            }
            usize parent = normalized.rfind('/');
            normalized.resize(parent == std::string::npos ? 0 : parent);
        }
        else if (!part.empty() && part != ".") {
#if defined(_WIN32)
            if (part.ends_with('.') || part.ends_with(' ')) {
                return std::nullopt;
            }
#endif
            if (!normalized.empty()) {
                normalized += '/';
            }
            normalized += part;
        }
    }
    return normalized;
}

struct Mounted_Path {
    std::string full;
    bool readOnly;
    bool root;
};

std::optional<Mounted_Path> Mount_Path(const Module& module, std::string_view folder, std::string_view path, bool assets) {
    const Module_Manifest* manifest = module.Manifest_For_Folder(folder);
    auto normalized = Normalize_Path(path);
    if (manifest == nullptr || !normalized || (assets && manifest->assetRoot.empty())) {
        return std::nullopt;
    }

    std::error_code error;
    auto root = std::filesystem::weakly_canonical(assets ? manifest->assetRoot : module.Folder_Path(folder), error);
    if (error) {
        return std::nullopt;
    }
    auto full = std::filesystem::weakly_canonical(root / *normalized, error);
    if (error || std::mismatch(root.begin(), root.end(), full.begin(), full.end()).first != root.end()) {
        return std::nullopt;
    }
    return Mounted_Path { full.generic_string(), assets, normalized->empty() };
}

std::optional<Mounted_Path> Full_Path(const Module& module, u64 folder_address, u64 folder_length, u64 path_address, u64 path_length) {
    std::optional<std::string_view> path = module.Text(path_address, path_length);
    auto folder = module.Owner_Folder(folder_address, folder_length);
    if (!path || !folder) {
        return std::nullopt;
    }
    bool assets = path->starts_with("assets:/");
    if (assets) {
        path->remove_prefix(8);
    }

    return Mount_Path(module, *folder, *path, assets);
}

FILE* Open_Read_Write(const char* path) {
#if defined(_WIN32)
    s32 descriptor = _open(path, _O_CREAT | _O_RDWR | _O_BINARY, _S_IREAD | _S_IWRITE);
    FILE* file = descriptor >= 0 ? _fdopen(descriptor, "r+b") : nullptr;

    if (descriptor >= 0 && file == nullptr) {
        _close(descriptor);
    }
#else
    s32 descriptor = open(path, O_CREAT | O_RDWR, 0666);
    FILE* file = descriptor >= 0 ? fdopen(descriptor, "r+b") : nullptr;

    if (descriptor >= 0 && file == nullptr) {
        close(descriptor);
    }
#endif
    return file;
}

u32 Open(Module& module, u64 folder, u64 folder_length, u64 path, u64 length, File_Mode mode) {
    if (mode > File_Mode::Read_Write) {
        return 0;
    }

    auto mounted = Full_Path(module, folder, folder_length, path, length);
    const char* flags = "rb";
    FILE* file;

    if (!mounted || (mounted->readOnly && mode != File_Mode::Read)) {
        return 0;
    }

    const auto& full = mounted->full;
    if (mode != File_Mode::Read) {
        Directory_Create_All(Path_Directory(full));
    }

    if (mode == File_Mode::Write) {
        flags = "wb";
    }
    else if (mode == File_Mode::Append) {
        flags = "ab";
    }

    if (mode == File_Mode::Read_Write) {
        file = Open_Read_Write(full.c_str());
    }
    else {
        file = mode == File_Mode::Read && !File_Exists(full) ? nullptr : fopen(full.c_str(), flags);
    }

    if (file == nullptr) {
        return 0;
    }

    auto entry = std::make_shared<Module_File>();
    entry->file.reset(file);
    entry->writable = mode != File_Mode::Read;
    std::lock_guard lock(module.resourceLock);
    if (module.nextFile == UINT32_MAX) {
        return 0;
    }

    u32 handle = ++module.nextFile;
    module.files.emplace(handle, std::move(entry));
    return handle;
}

std::shared_ptr<Module_File> File_Of(Module& module, u32 handle) {
    std::lock_guard lock(module.resourceLock);
    auto found = module.files.find(handle);

    return found != module.files.end() ? found->second : nullptr;
}

s64 Transfer(Module& module, u32 handle, u64 address, u64 size, u64 offset, bool write) {
    auto entry = File_Of(module, handle);
    void* data = module.Memory(address, size);
    if (!entry || (write && !entry->writable) || (data == nullptr && size != 0) || offset > INT64_MAX || size > INT64_MAX) {
        return -1;
    }

    std::lock_guard lock(entry->lock);
    FILE* file = entry->file.get();
    if (File_Seek(file, static_cast<s64>(offset), SEEK_SET) < 0) {
        return -1;
    }

    if (size == 0) {
        return 0;
    }

    clearerr(file);
    if (write) {
        usize written = fwrite(data, 1, size, file);
        return fflush(file) == 0 && written == size ? static_cast<s64>(written) : -1;
    }

    usize read = fread(data, 1, size, file);
    return ferror(file) == 0 ? static_cast<s64>(read) : -1;
}

s64 Size(Module& module, u32 handle) {
    auto entry = File_Of(module, handle);
    if (!entry) {
        return -1;
    }

    std::lock_guard lock(entry->lock);
    return File_Seek(entry->file.get(), 0, SEEK_END);
}

void Close(Module& module, u32 handle) {
    std::shared_ptr<Module_File> closing;
    {
        std::lock_guard lock(module.resourceLock);
        auto found = module.files.find(handle);
        if (found != module.files.end()) {
            closing = std::move(found->second);
            module.files.erase(found);
        }
    }
}

// Newline-separated entries; directories end in /
// returns required size or -1
s64 List(Module& module, u64 folder, u64 folder_length, u64 path, u64 length, u64 buffer, u64 capacity) {
    auto mounted = Full_Path(module, folder, folder_length, path, length);
    bool is_folder;
    std::vector<std::string> names;
    std::string listing;
    void* destination;

    if (!mounted) {
        return -1;
    }

    const auto& full = mounted->full;
    is_folder = Directory_Exists(full);
    if (is_folder) {
        names = Directory_List(full);
        std::sort(names.begin(), names.end());
        for (const std::string& name : names) {
            listing += name + (Directory_Exists(Path_Join(full, name)) ? "/\n" : "\n");
        }
    }

    if (!is_folder && !mounted->root) {
        return -1;
    }

    destination = !listing.empty() && listing.size() <= capacity ? module.Memory(buffer, listing.size()) : nullptr;
    if (destination != nullptr) {
        memcpy(destination, listing.data(), listing.size());
    }

    return static_cast<s64>(listing.size());
}

template <typename Action>
u64 With_Path(wasm_exec_env_t exec_env, u64* slots, bool write, Action action) {
    auto path = Full_Path(Module_Of(exec_env), slots[0], slots[1], slots[2], slots[3]);

    return path && !(write && path->readOnly) && action(path->full) ? 1 : 0;
}

NativeSymbol sNatives[] = {
    Native("file_open", "(IIIIi)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Open(Module_Of(exec_env), slots[0], slots[1], slots[2], slots[3], static_cast<File_Mode>(slots[4]));
    }),
    Native("file_read", "(iIII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = static_cast<u64>(Transfer(Module_Of(exec_env), static_cast<u32>(slots[0]), slots[1], slots[2], slots[3], false));
    }),
    Native("file_write", "(iIII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = static_cast<u64>(Transfer(Module_Of(exec_env), static_cast<u32>(slots[0]), slots[1], slots[2], slots[3], true));
    }),
    Native("file_size", "(i)I", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = static_cast<u64>(Size(Module_Of(exec_env), static_cast<u32>(slots[0])));
    }),
    Native("file_close", "(i)", [](wasm_exec_env_t exec_env, u64* slots) {
        Close(Module_Of(exec_env), static_cast<u32>(slots[0]));
    }),
    Native("file_delete", "(IIII)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = With_Path(exec_env, slots, true, [](const std::string& path) {
            return remove(path.c_str()) == 0;
        });
    }),
    Native("file_exists", "(IIII)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = With_Path(exec_env, slots, false, [](const std::string& path) {
            return File_Exists(path) || Directory_Exists(path);
        });
    }),
    Native("file_create_directory", "(IIII)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = With_Path(exec_env, slots, true, [](const std::string& path) {
            return Directory_Create_All(path);
        });
    }),
    Native("file_list", "(IIIIII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = static_cast<u64>(List(Module_Of(exec_env), slots[0], slots[1], slots[2], slots[3], slots[4], slots[5]));
    }),
};

} // namespace

void Files_Register_Natives() {
    Register_Natives(sNatives, "file");
}

void Files_Create_Folders(const Module& module) {
    for (const auto& manifest : module.manifests) {
        Directory_Create_All(module.Folder_Path(manifest.folder));
    }
}

void Files_Release_Owner(Module& owner) {
    owner.files.clear();
}

std::optional<std::string> Module_Mounted_Path(const Module& module, std::string_view folder, std::string_view path) {
    bool assets = path.starts_with("assets:/");
    if (assets) {
        path.remove_prefix(8);
    }

    auto mounted = Mount_Path(module, folder, path, assets);
    return mounted ? std::optional(std::move(mounted->full)) : std::nullopt;
}

std::optional<std::string> Module_File_Path(const Module& module, std::string_view path) {
    bool assets = path.starts_with("@assets/");
    if (assets) {
        path.remove_prefix(8);
    }

    while (path.starts_with('/')) {
        path.remove_prefix(1);
    }

    usize separator = path.find('/');
    std::string_view folder = path.substr(0, separator);
    if (separator == std::string_view::npos) {
        return std::nullopt;
    }

    auto mounted = Mount_Path(module, folder, path.substr(separator + 1), assets);
    return mounted ? std::optional(std::move(mounted->full)) : std::nullopt;
}
