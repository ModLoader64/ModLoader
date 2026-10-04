#include "module_package.h"
#include "base/file.h"

#include <modloader_bytes.h>
#include <monocypher.h>
#include <unzip.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <unordered_map>

namespace {

constexpr u64 gManifestLimit = 1024 * 1024;

struct Entry {
    std::string name;
    unz64_file_pos position {};
    unz_file_info64 information {};
    bool directory = false;
};

struct Path_Entry {
    std::string name;
    bool directory;
    bool explicitEntry;
};

bool Valid_Path(std::string_view name) {
    if (name.empty() || name.front() == '/') {
        return false;
    }

    for (char character : name) {
        if (static_cast<u8>(character) < 32 || character == 127 || std::string_view("\\:*?\"<>|").find(character) != std::string_view::npos) {
            return false;
        }
    }

    if (name.back() == '/') {
        name.remove_suffix(1);
    }

    for (usize start = 0; start <= name.size();) {
        usize end = name.find('/', start);
        auto part = name.substr(start, end == std::string_view::npos ? end : end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') {
            return false;
        }

        std::string stem(part.substr(0, part.find('.')));
        for (char& character : stem) {
            if (character >= 'a' && character <= 'z') {
                character -= 'a' - 'A';
            }
        }

        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
            (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '1' && stem[3] <= '9')) {
            return false;
        }

        if (end == std::string_view::npos) {
            break;
        }

        start = end + 1;
    }

    return name == "manifest.json" || name == "module.wasm" || name == "assets" || name.starts_with("assets/");
}

struct Archive {
    File_Handle input;
    unzFile zip = nullptr;
    std::vector<Entry> entries;
    std::string fingerprint;

    ~Archive() {
        if (zip != nullptr) {
            unzClose(zip);
        }
    }

    bool Open(const std::string& path) {
        input.reset(fopen(path.c_str(), "rb"));
        if (!input) {
            return false;
        }

        crypto_blake2b_ctx context;
        crypto_blake2b_init(&context, 32);
        std::array<u8, 65536> buffer;
        usize count;
        while ((count = fread(buffer.data(), 1, buffer.size(), input.get())) != 0) {
            crypto_blake2b_update(&context, buffer.data(), count);
        }

        if (ferror(input.get()) || File_Seek(input.get(), 0, SEEK_SET) < 0) {
            return false;
        }

        u8 digest[32];
        char text[65] {};
        crypto_blake2b_final(&context, digest);
        ModLoader::Bytes::Hex_Encode(digest, sizeof(digest), text);
        fingerprint = text;

        zlib_filefunc64_def functions;
        fill_fopen64_filefunc(&functions);
        functions.opaque = input.get();
        functions.zopen64_file = [](void* opaque, const void*, int) -> void* {
            return opaque;
        };

        functions.zclose_file = [](void*, void*) -> int {
            return 0;
        };

        zip = unzOpen2_64(path.c_str(), &functions);
        if (zip == nullptr) {
            return false;
        }

        std::unordered_map<std::string, Path_Entry> paths;
        s32 status = unzGoToFirstFile(zip);
        while (status == UNZ_OK) {
            Entry entry;
            auto& information = entry.information;
            if (unzGetCurrentFileInfo64(zip, &information, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK) {
                return false;
            }

            std::vector<char> name(information.size_filename + 1);
            if (unzGetCurrentFileInfo64(zip, &information, name.data(), static_cast<uLong>(name.size()), nullptr, 0, nullptr, 0) != UNZ_OK ||
                unzGetFilePos64(zip, &entry.position) != UNZ_OK) {
                return false;
            }
            
            entry.name.assign(name.data(), information.size_filename);
            entry.directory = entry.name.ends_with('/');
            u32 type = (information.external_fa >> 16) & 0170000;
            if (!Valid_Path(entry.name) || (information.flag & 1) != 0 || information.disk_num_start != 0 ||
                (information.compression_method != 0 && information.compression_method != Z_DEFLATED) ||
                (type != 0 && type != (entry.directory ? 0040000u : 0100000u)) ||
                (entry.directory && information.uncompressed_size != 0)) {
                return false;
            }

            std::string canonical = entry.name;
            if (entry.directory) {
                canonical.pop_back();
            }

            std::transform(canonical.begin(), canonical.end(), canonical.begin(), [](char character) {
                return character >= 'A' && character <= 'Z' ? static_cast<char>(character + 'a' - 'A') : character;
            });

            for (usize end = canonical.find('/');;) {
                bool leaf = end == std::string::npos;
                std::string prefix = entry.name.substr(0, leaf ? canonical.size() : end);
                bool directory = !leaf || entry.directory;
                auto [found, inserted] = paths.try_emplace(canonical.substr(0, end), Path_Entry {prefix, directory, leaf});
                if (!inserted) {
                    auto& previous = found->second;
                    if (previous.name != prefix || previous.directory != directory || (leaf && previous.explicitEntry)) {
                        return false;
                    }
                    previous.explicitEntry |= leaf;
                }

                if (leaf) {
                    break;
                }
                end = canonical.find('/', end + 1);
            }

            entries.push_back(std::move(entry));
            status = unzGoToNextFile(zip);
        }
        return status == UNZ_END_OF_LIST_OF_FILE && Find("manifest.json") != nullptr && Find("module.wasm") != nullptr;
    }

    const Entry* Find(std::string_view name) const {
        auto found = std::find_if(entries.begin(), entries.end(), [&](const Entry& entry) {
            return !entry.directory && entry.name == name;
        });

        return found != entries.end() ? &*found : nullptr;
    }

    bool Read(const Entry& entry, const std::function<bool(std::span<const u8>)>& consume) {
        if (unzGoToFilePos64(zip, &entry.position) != UNZ_OK || unzOpenCurrentFile(zip) != UNZ_OK) {
            return false;
        }

        std::array<u8, 65536> buffer;
        u64 total = 0;
        bool valid = true;
        while (true) {
            s32 count = unzReadCurrentFile(zip, buffer.data(), static_cast<u32>(buffer.size()));
            if (count == 0) {
                break;
            }

            if (count < 0 || static_cast<u64>(count) > entry.information.uncompressed_size - total || !consume({buffer.data(), static_cast<usize>(count)})) {
                valid = false;
                break;
            }
            total += static_cast<u32>(count);
        }
        return unzCloseCurrentFile(zip) == UNZ_OK && valid && total == entry.information.uncompressed_size;
    }

    std::optional<std::vector<u8>> Bytes(std::string_view name, u64 limit) {
        const Entry* entry = Find(name);
        if (entry == nullptr || entry->information.uncompressed_size > limit) {
            return std::nullopt;
        }

        std::vector<u8> bytes;
        if (!Read(*entry, [&](std::span<const u8> part) {
                bytes.insert(bytes.end(), part.begin(), part.end());
                return true;
            })) {
            return std::nullopt;
        }

        return bytes;
    }
};

} // namespace

std::optional<Module_Package> Open_Module_Package(const std::string& path, const std::string& data_directory) {
    Archive archive;
    if (!archive.Open(path)) {
        Log_Error("package", "invalid .pak archive: %s", path.c_str());
        return std::nullopt;
    }

    auto manifest = archive.Bytes("manifest.json", gManifestLimit);
    if (!manifest) {
        Log_Error("package", "invalid manifest: %s", path.c_str());
        return std::nullopt;
    }

    std::string parent = Path_Join(data_directory, "cache/packages");
    std::string root = Path_Join(parent, archive.fingerprint);
    Module_Package package {root, Path_Join(root, "module.wasm"), archive.fingerprint, std::move(*manifest)};
    if (File_Exists(Path_Join(root, ".complete")) && File_Exists(package.modulePath)) {
        return package;
    }

    if (!Directory_Create_All(parent)) {
        return std::nullopt;
    }

    Temporary_Directory temporary;
    std::error_code error;
    if (!temporary.Create(root + Text_Format(".tmp-%u-%llu-%llu", Process_Current_Id(), static_cast<unsigned long long>(Thread_Current_Id()), static_cast<unsigned long long>(Time_Monotonic_Microseconds())))) {
        return std::nullopt;
    }

    for (const Entry& entry : archive.entries) {
        std::string destination = Path_Join(temporary.Path(), entry.name);
        if (!Directory_Create_All(entry.directory ? destination : Path_Directory(destination))) {
            return std::nullopt;
        }

        if (entry.directory) {
            continue;
        }

        File_Handle output(fopen(destination.c_str(), "wbx"));
        if (!output || !archive.Read(entry, [&](std::span<const u8> bytes) {
                return fwrite(bytes.data(), 1, bytes.size(), output.get()) == bytes.size();
            }) || fclose(output.release()) != 0) {
            Log_Error("package", "cannot extract %s from %s", entry.name.c_str(), path.c_str());
            return std::nullopt;
        }
    }

    if (!File_Write_Text(Path_Join(temporary.Path(), ".complete"), archive.fingerprint)) {
        return std::nullopt;
    }

    std::filesystem::rename(temporary.Path(), root, error);
    if (error && !(File_Exists(Path_Join(root, ".complete")) && File_Exists(package.modulePath))) {
        Log_Error("package", "cannot publish package cache: %s", error.message().c_str());
        return std::nullopt;
    }
    
    return package;
}
