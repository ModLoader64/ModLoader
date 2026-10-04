#include "base.h"
#include "base/file.h"

#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

s64 File_Seek(FILE* file, s64 offset, s32 origin) {
#if defined(_WIN32)
    return _fseeki64(file, offset, origin) == 0 ? _ftelli64(file) : -1;
#else
    return fseeko(file, static_cast<off_t>(offset), origin) == 0 ? static_cast<s64>(ftello(file)) : -1;
#endif
}

std::optional<std::vector<u8>> File_Read(const std::string& path) {
    File_Handle file(fopen(path.c_str(), "rb"));
    std::vector<u8> data;
    s64 length;

    if (file == nullptr) {
        return std::nullopt;
    }

    length = File_Seek(file.get(), 0, SEEK_END);
    if (length < 0 || static_cast<u64>(length) > SIZE_MAX || File_Seek(file.get(), 0, SEEK_SET) < 0) {
        return std::nullopt;
    }

    data.resize(static_cast<usize>(length));
    bool read = data.empty() || fread(data.data(), 1, data.size(), file.get()) == data.size();
    return read ? std::optional(std::move(data)) : std::nullopt;
}

bool File_Write(const std::string& path, std::span<const u8> data) {
    File_Handle file(fopen(path.c_str(), "wb"));
    bool ok;

    if (file == nullptr) {
        return false;
    }

    ok = data.empty() || fwrite(data.data(), 1, data.size(), file.get()) == data.size();
    return fclose(file.release()) == 0 && ok;
}

bool File_Write_Text(const std::string& path, std::string_view text) {
    return File_Write(path, { reinterpret_cast<const u8*>(text.data()), text.size() });
}

bool File_Replace(const std::string& source, const std::string& destination) {
#if defined(_WIN32)
    return MoveFileExA(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(source.c_str(), destination.c_str()) == 0;
#endif
}

bool File_Exists(const std::string& path) {
#if defined(_WIN32)
    DWORD attributes = GetFileAttributesA(path.c_str());

    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat information;

    return stat(path.c_str(), &information) == 0 && S_ISREG(information.st_mode);
#endif
}

bool Directory_Exists(const std::string& path) {
#if defined(_WIN32)
    DWORD attributes = GetFileAttributesA(path.c_str());

    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat information;

    return stat(path.c_str(), &information) == 0 && S_ISDIR(information.st_mode);
#endif
}

bool Directory_Create_All(const std::string& path) {
    std::error_code error;
    return std::filesystem::create_directories(path, error) || Directory_Exists(path);
}

std::vector<std::string> Directory_List(const std::string& path) {
    std::vector<std::string> names;
    std::error_code error;
    std::filesystem::directory_iterator entries(path, error);
    while (!error && entries != std::filesystem::directory_iterator()) {
        names.push_back(entries->path().filename().string());
        entries.increment(error);
    }

    return names;
}

std::string Path_Join(std::string_view left, std::string_view right) {
    std::string path(left);

    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path += '/';
    }

    path += right;
    return path;
}

bool Temporary_Directory::Create(const std::string& directory) {
    std::error_code error;
    if (!path.empty() || !std::filesystem::create_directory(directory, error)) {
        return false;
    }

    path = directory;
    return true;
}

Temporary_Directory::~Temporary_Directory() {
    if (!path.empty()) {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
}

const char* Path_File_Name(const char* path) {
    const char* name = path;

    for (const char* cursor = path; *cursor != '\0'; cursor++) {
        if (*cursor == '/' || *cursor == '\\') {
            name = cursor + 1;
        }
    }

    return name;
}

std::string Path_Directory(std::string_view path) {
    usize separator = path.find_last_of("/\\");
    return separator == std::string_view::npos ? std::string(".") : std::string(path.substr(0, separator));
}

std::optional<std::string> Path_Canonical(const std::string& path) {
    std::error_code error;
    auto canonical = std::filesystem::canonical(path, error);
    if (error) {
        Log_Error("files", "cannot open %s: %s", path.c_str(), error.message().c_str());
        return std::nullopt;
    }
    return canonical.generic_string();
}

std::string Path_Executable_Directory() {
#if defined(_WIN32)
    std::vector<char> path(256);
    for (;;) {
        DWORD length = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return ".";
        }

        if (length < path.size()) {
            return Path_Directory(std::string_view(path.data(), length));
        }
        if (path.size() > UINT32_MAX / 2) {
            return ".";
        }

        path.resize(path.size() * 2);
    }
#else
    std::vector<char> path(256);
    for (;;) {
        ssize_t length = readlink("/proc/self/exe", path.data(), path.size());
        if (length <= 0) {
            return ".";
        }
        
        if (static_cast<usize>(length) < path.size()) {
            return Path_Directory(std::string_view(path.data(), static_cast<usize>(length)));
        }
        path.resize(path.size() * 2);
    }
#endif
}

