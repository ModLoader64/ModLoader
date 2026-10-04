#include "module.h"
#include "base/file.h"
#include "sha1.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <unordered_set>

namespace {

constexpr char gCacheSectionName[] = "modloader.cache.1"; // IMPORTANT: BUMP THIS
constexpr u32 gFingerprintSize = 40;
// WAMR RAW custom section, with a NUL-terminated name and build fingerprint
constexpr u8 gCacheSectionHeader[] = {
    100, 0, 0, 0,
    6 + sizeof(gCacheSectionName) + gFingerprintSize, 0, 0, 0,
    0, 0, 0, 0,
    sizeof(gCacheSectionName), 0,
};
constexpr usize gCacheTagSize = sizeof(gCacheSectionHeader) + sizeof(gCacheSectionName) + gFingerprintSize;

struct Aot_Cache {
    std::string file;
    std::string tag;
    std::vector<std::string> flags;
};

struct Aot_Request {
    usize module;
    Aot_Cache cache;
};

struct Aot_Job {
    Process process;
    usize request = 0;
    std::string temporary;
};

std::optional<Aot_Cache> Cache_Of(Runtime& runtime, std::span<const u8> bytes) {
#if defined(_WIN32)
    const char* size_level = "0";
#else
    const char* size_level = "3";
#endif
    Aot_Cache cache;
    std::string directory = Path_Join(runtime.config.dataDirectory, "cache/aot");

    if (runtime.config.moduleExecution == Module_Execution::Interpreter || runtime.config.aotCompiler.empty()) {
        return std::nullopt;
    }

    if (runtime.aotCompilerFingerprint.empty()) {
        auto compiler = File_Read(runtime.config.aotCompiler);
        if (!compiler) {
            return std::nullopt;
        }
        runtime.aotCompilerFingerprint = Sha1::Digest(*compiler);
    }

    cache.flags = {
        "--target=x86_64", Text_Format("--size-level=%s", size_level),
        Text_Format("--opt-level=%d", static_cast<s32>(runtime.config.moduleExecution)),
        "--enable-multi-thread", "--enable-shared-chain"
    };

    std::string module_hash = Sha1::Digest(bytes);
    Sha1 build_hash;

    build_hash.Update(module_hash);
    for (const std::string& flag : cache.flags) {
        build_hash.Update(flag);
    }

    build_hash.Update(runtime.aotCompilerFingerprint);
    Directory_Create_All(directory);
    cache.file = Path_Join(directory, module_hash + ".aot");
    cache.tag.assign(reinterpret_cast<const char*>(gCacheSectionHeader), sizeof(gCacheSectionHeader));
    cache.tag.append(gCacheSectionName, sizeof(gCacheSectionName));
    cache.tag += build_hash.Hex_Digest();
    return cache;
}

bool Cache_Is_Current(const Aot_Cache& cache) {
    File_Handle file(fopen(cache.file.c_str(), "rb"));
    char tag[gCacheTagSize];

    if (file == nullptr) {
        return false;
    }

    s64 offset = File_Seek(file.get(), -static_cast<s64>(sizeof(tag)), SEEK_END);
    return offset >= 8 && offset % 4 == 0 && fread(tag, 1, sizeof(tag), file.get()) == sizeof(tag) && memcmp(tag, cache.tag.data(), sizeof(tag)) == 0;
}

std::optional<std::vector<u8>> Read_Cached(const Aot_Cache& cache) {
    auto native = File_Read(cache.file);

    if (!native || native->size() < gCacheTagSize + 8 || (native->size() - gCacheTagSize) % 4 != 0 ||
        memcmp(native->data() + native->size() - gCacheTagSize, cache.tag.data(), gCacheTagSize) != 0) {
        return std::nullopt;
    }

    return native;
}

std::string Temporary_Path(const Aot_Cache& cache, u32 serial) {
    return Text_Format("%s.%u.%llu.%u.tmp", cache.file.c_str(), Process_Current_Id(), static_cast<unsigned long long>(Time_Wall_Microseconds()), serial);
}

std::vector<std::string> Compile_Arguments(const Aot_Cache& cache, const std::string& source, const std::string& output) {
    std::vector<std::string> arguments = cache.flags;

    arguments.insert(arguments.end(), { "-o", output, source });
    return arguments;
}

bool Publish_Compiled(const std::string& temporary, const Aot_Cache& cache, s32 result) {
    File_Handle file(result == 0 ? fopen(temporary.c_str(), "ab") : nullptr);
    bool success = false;

    if (file != nullptr) {
        s64 size = File_Seek(file.get(), 0, SEEK_END);
        u8 padding[3] = {};
        usize padding_size = static_cast<usize>((4 - size % 4) % 4);

        success = size >= 8 && fwrite(padding, 1, padding_size, file.get()) == padding_size && fwrite(cache.tag.data(), 1, cache.tag.size(), file.get()) == cache.tag.size();
        success = fclose(file.release()) == 0 && success;
        success = success && File_Replace(temporary, cache.file);
    }

    remove(temporary.c_str());
    return success;
}

}

void Aot_Compile_All(Runtime& runtime, std::span<const Linked_Module> modules) {
    if (runtime.config.moduleExecution == Module_Execution::Interpreter) {
        return;
    }

    std::vector<Aot_Request> requests;
    std::unordered_set<std::string> scheduled;

    for (usize index = 0; index < modules.size(); index++) {
        auto bytes = File_Read(modules[index].path);
        auto cache = bytes ? Cache_Of(runtime, *bytes) : std::nullopt;
        if (cache && !Cache_Is_Current(*cache) && scheduled.insert(cache->file).second) {
            requests.push_back({ index, std::move(*cache) });
        }
    }

    usize worker_count = std::min(requests.size(), static_cast<usize>(std::max(1u, Thread_Cpu_Count())));
    std::vector<Aot_Job> jobs(worker_count);
    usize next = 0;
    usize completed = 0;

    while (completed < requests.size()) {
        for (Aot_Job& job : jobs) {
            auto result = job.process.Poll();
            if (result) {
                const Aot_Request& request = requests[job.request];
                const char* name = modules[request.module].name.c_str();
                if (!Publish_Compiled(job.temporary, request.cache, *result)) {
                    Log_Error("wamrc", "%s: compilation failed (%d)", name, *result);
                }
                completed++;
            }

            if (!job.process.Is_Watched() && next < requests.size()) {
                job.request = next++;
                const Aot_Request& request = requests[job.request];
                const Linked_Module& module = modules[request.module];

                job.temporary = Temporary_Path(request.cache, static_cast<u32>(job.request));
                Log_Info("wamrc", "compiling %s", module.name.c_str());
                if (!job.process.Spawn(runtime.config.aotCompiler, Compile_Arguments(request.cache, module.path, job.temporary))) {
                    Log_Error("wamrc", "%s: cannot start compiler", module.name.c_str());
                    completed++;
                }
            }
        }

        if (runtime.progress != nullptr) {
            for (const Aot_Job& job : jobs) {
                if (job.process.Is_Watched()) {
                    (*runtime.progress)(Runtime_Stage::Compiling, modules[requests[job.request].module].name.c_str(),
                        static_cast<u32>(completed + 1), static_cast<u32>(requests.size()));
                    break;
                }
            }
        }

        if (completed < requests.size()) {
            Time_Sleep_Milliseconds(10);
        }
    }
}

bool Aot_Take_Native(Runtime& runtime, Module& module) {
    std::optional<Aot_Cache> cache = Cache_Of(runtime, module.bytes);

    if (!cache) {
        Log_Error("wamrc", "%s: cannot compile", module.name.c_str());
        return false;
    }

    auto native = Read_Cached(*cache);
    if (!native) {
        return false;
    }
    
    module.bytes = std::move(*native);
    return true;
}
