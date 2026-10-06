#include "module_graph.h"
#include "sha1.h"
#include "wasm_object.h"
#include "base/file.h"

#include <string.h>

namespace {

constexpr u8 gWasmHeader[] = { 0, 'a', 's', 'm', 1, 0, 0, 0 };
constexpr char gCacheName[] = "modloader.link.cache.1"; // IMPORTANT: BUMP THIS
constexpr usize gDigestSize = 40;
constexpr u8 gCacheHeader[] = { 0, sizeof(gCacheName) + gDigestSize, sizeof(gCacheName) - 1 };
constexpr usize gCacheFooterSize = sizeof(gCacheHeader) + sizeof(gCacheName) - 1 + gDigestSize;
static_assert(sizeof(gCacheName) + gDigestSize < 128);

std::string Cache_Digest(std::span<const u8> bytes, std::string_view key) {
    Sha1 hash;
    hash.Update_Field(key);
    hash.Update(bytes.data(), bytes.size());
    return hash.Hex_Digest();
}

bool Cache_Is_Current(const std::string& path, std::string_view key) {
    auto bytes = File_Read(path);
    if (!bytes || bytes->size() < sizeof(gWasmHeader) + gCacheFooterSize ||
        memcmp(bytes->data(), gWasmHeader, sizeof(gWasmHeader)) != 0) {
        return false;
    }

    usize payload_size = bytes->size() - gCacheFooterSize;
    const u8* footer = bytes->data() + payload_size;
    if (memcmp(footer, gCacheHeader, sizeof(gCacheHeader)) != 0 ||
        memcmp(footer + sizeof(gCacheHeader), gCacheName, sizeof(gCacheName) - 1) != 0) {
        return false;
    }

    std::string digest = Cache_Digest(std::span(*bytes).first(payload_size), key);
    return memcmp(bytes->data() + bytes->size() - gDigestSize, digest.data(), gDigestSize) == 0;
}

bool Publish_Cache(const std::string& temporary, const std::string& output, std::string_view key) {
    auto bytes = File_Read(temporary);
    if (!bytes || bytes->size() < sizeof(gWasmHeader) ||
        memcmp(bytes->data(), gWasmHeader, sizeof(gWasmHeader)) != 0) {
        return false;
    }

    std::string digest = Cache_Digest(*bytes, key);
    File_Handle file(fopen(temporary.c_str(), "ab"));
    if (file == nullptr) {
        return false;
    }

    bool success =
        fwrite(gCacheHeader, 1, sizeof(gCacheHeader), file.get()) == sizeof(gCacheHeader) &&
        fwrite(gCacheName, 1, sizeof(gCacheName) - 1, file.get()) == sizeof(gCacheName) - 1 &&
        fwrite(digest.data(), 1, digest.size(), file.get()) == digest.size();

    success = fclose(file.release()) == 0 && success;
    return success && (File_Replace(temporary, output) || Cache_Is_Current(output, key));
}

std::string Response_File(std::span<const std::string> arguments) {
    std::string text;
    for (const std::string& argument : arguments) {
        text += '"';
        for (char character : argument) {
            if (character == '\\' || character == '"') {
                text += '\\';
            }
            text += character;
        }
        text += "\"\n";
    }

    return text;
}

struct Link_Tool {
    std::string path;
    std::string fingerprint;
};

std::optional<Link_Tool> Find_Linker() {
#if defined(_WIN32)
    std::string linker = Path_Join(Path_Executable_Directory(), "wasm-ld.exe");
#else
    std::string linker = Path_Join(Path_Executable_Directory(), "wasm-ld");
#endif
    auto executable = File_Read(linker);
    if (!executable) {
        Log_Error("linker", "cannot read %s", linker.c_str());
        return std::nullopt;
    }
    return Link_Tool{std::move(linker), Sha1::Digest(*executable)};
}

std::optional<Linked_Module> Link(Runtime& runtime, const Module_Link_Runtime& files,
    std::span<Module_Link_Input> modules, const Link_Tool& linker) {
    Sha1 hash;
    hash.Update_Field("modloader.link.wasm.1");
    hash.Update_Field("--fatal-warnings");
    hash.Update_Field(linker.fingerprint);
    hash.Update_Field(files.fingerprint);
    for (const auto& module : modules) {
        hash.Update_Field(module.fingerprint);
    }

    std::string name;
    if (modules.size() > 3) {
        name = Text_Format("%llu modules", static_cast<unsigned long long>(modules.size()));
    }
    else {
        for (const auto& module : modules) {
            if (!name.empty()) {
                name += ", ";
            }
            name += module.manifest.name;
        }
    }

    std::string key = hash.Hex_Digest();
    std::string directory = Path_Join(runtime.config.dataDirectory, "cache/link/" + key);
    std::string output = Path_Join(directory, "group.wasm");
    Linked_Module linked{output, name, {}};
    for (const auto& module : modules) {
        linked.manifests.push_back(module.manifest);
    }
    
    if (Cache_Is_Current(output, key)) {
        return linked;
    }

    if (!Directory_Create_All(directory)) {
        Log_Error("linker", "cannot create %s", directory.c_str());
        return std::nullopt;
    }

    auto absolute_directory = Path_Canonical(directory);
    if (!absolute_directory) {
        return std::nullopt;
    }

    directory = *absolute_directory;
    output = Path_Join(directory, "group.wasm");
    std::string work = Path_Join(directory, Text_Format(".link-%u-%llu-%llu", Process_Current_Id(), static_cast<unsigned long long>(Thread_Current_Id()), static_cast<unsigned long long>(Time_Monotonic_Microseconds())));
    Temporary_Directory cleanup;
    if (!cleanup.Create(work)) {
        Log_Error("linker", "cannot create %s", work.c_str());
        return std::nullopt;
    }

    std::vector<std::string> arguments = {
        "-m", "wasm64", "--no-entry", "--shared-memory", "--gc-sections", "--fatal-warnings",
        "--export=__heap_base", "--export=__data_end", "--undefined=modloader_event_init",
        "-z", Text_Format("stack-size=%llu", static_cast<unsigned long long>(files.stackSize)),
        Text_Format("--max-memory=%llu", static_cast<unsigned long long>(files.maxMemory))
    };

    for (usize index = 0; index < modules.size(); ++index) {
        auto& module = modules[index];
        std::string file = Path_Join(work, Text_Format("%llu.o", static_cast<unsigned long long>(index)));
        if (!Wasm_Localize(module.object, module.manifest.name) || !File_Write(file, module.object)) {
            Log_Error("linker", "cannot prepare module %s", module.manifest.name.c_str());
            return std::nullopt;
        }
        arguments.push_back(std::move(file));
    }

    std::string runtime_file = Path_Join(work, "runtime.o");
    if (!File_Write(runtime_file, files.object)) {
        Log_Error("linker", "cannot prepare the SDK runtime");
        return std::nullopt;
    }

    arguments.push_back(std::move(runtime_file));
    std::string temporary = Path_Join(work, "group.wasm");
    arguments.insert(arguments.end(), { "-o", temporary });
    std::string response = Path_Join(work, "link.rsp");
    if (!File_Write_Text(response, Response_File(arguments))) {
        Log_Error("linker", "cannot write linker arguments");
        return std::nullopt;
    }

    Log_Info("linker", "linking %s", name.c_str());
    if (runtime.progress != nullptr) {
        (*runtime.progress)(Runtime_Stage::Linking, name.c_str(), 0, 0);
    }

    std::string response_argument = "@" + response;
    std::string diagnostics = Path_Join(work, "link.log");
    s32 result = Process_Run(linker.path, std::span(&response_argument, 1), diagnostics);
    if (result != 0) {
        Log_Error("linker", "%s: linking failed (%d)", name.c_str(), result);
        for (usize index = 0; index < modules.size(); ++index) {
            Log_Error("linker", "%llu.o: %s", static_cast<unsigned long long>(index), modules[index].manifest.name.c_str());
        }
        Log_Error("linker", "runtime.o: SDK runtime");
        if (auto bytes = File_Read(diagnostics); bytes && !bytes->empty()) {
            std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            while (!text.empty()) {
                usize end = text.find_first_of("\r\n");
                std::string_view line = text.substr(0, end);
                if (!line.empty()) {
                    Log_Error("linker", "%.*s", static_cast<int>(line.size()), line.data());
                }
                text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
            }
        }
        return std::nullopt;
    }
    if (!Publish_Cache(temporary, output, key)) {
        Log_Error("linker", "%s: cannot cache linked module to %s", name.c_str(), output.c_str());
        return std::nullopt;
    }

    linked.path = output;
    return linked;
}

} // namespace

std::optional<std::vector<Linked_Module>> Link_Modules(Runtime& runtime, std::span<const std::string> paths) {
    auto plan = Prepare_Link(runtime, paths);
    if (!plan) {
        return std::nullopt;
    }

    if (plan->groups.empty()) {
        return std::vector<Linked_Module>{};
    }

    auto linker = Find_Linker();
    if (!linker) {
        return std::nullopt;
    }

    std::vector<Linked_Module> output;
    output.reserve(plan->groups.size());
    for (auto& group : plan->groups) {
        auto linked = Link(runtime, plan->runtimes[group.front().runtimeIndex], group, *linker);
        if (!linked) {
            return std::nullopt;
        }
        output.push_back(std::move(*linked));
        group.clear();
    }

    return output;
}
