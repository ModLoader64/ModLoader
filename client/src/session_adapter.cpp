#include "game_session.h"

#include <algorithm>
#include <ctype.h>
#include <string.h>
#include <unordered_set>

namespace {

bool Parse_Server(std::string_view server, std::string& out_address, u16& out_port) {
    std::string_view address = server;
    std::string_view port;
    if (server.starts_with('[')) {
        usize end = server.find(']');
        if (end == std::string_view::npos) {
            return false;
        }

        address = server.substr(1, end - 1);
        server.remove_prefix(end + 1);
        if (!server.empty()) {
            if (!server.starts_with(':') || server.size() == 1) {
                return false;
            }
            port = server.substr(1);
        }
    }
    else if (usize colon = server.find(':'); colon != std::string_view::npos && colon == server.rfind(':')) {
        address = server.substr(0, colon);
        port = server.substr(colon + 1);
        if (port.empty()) {
            return false;
        }
    }

    if (!port.empty()) {
        auto number = Text_Parse_U64(port);
        if (!number || *number == 0 || *number > UINT16_MAX) {
            return false;
        }
        out_port = static_cast<u16>(*number);
    }

    out_address = address;
    return !address.empty();
}

Game_Session& Session_Of(void* host) {
    return *static_cast<Game_Session*>(host);
}

std::string Lobby_Save_Directory(const std::string& data_directory, std::string lobby) {
    for (char& character : lobby) {
        character = isalnum(static_cast<u8>(character)) || character == '-' || character == '_' || character == ' ' ? character : '_';
    }

    return Path_Join(Path_Join(data_directory, "saves"), lobby);
}

std::string Find_Adapter(const Run_Options& options) {
    if (!options.adapterPath.empty()) {
        return options.adapterPath;
    }

    std::string executable_directory = Path_Executable_Directory();
    std::string name = Text_Format("%sgopher64%s", Library_Prefix(), Library_Extension());
    for (const char* directory : { ".", "adapters" }) {
        std::string path = Path_Join(Path_Join(executable_directory, directory), name);
        if (File_Exists(path)) {
            return path;
        }
    }

    return {};
}


} // namespace

void Game_Session::Invalidate_Code(u32 space, u64 offset, u64 size) {
    if (adapter->invalidateCode != nullptr) {
        adapter->invalidateCode(handle, space, offset, size);
    }
}

bool Game_Session::Translate_Address(u32 processor, u64 address, u32& out_space, u64& out_offset) {
    uint32_t space = 0;
    uint64_t offset = 0;

    if (adapter->translateAddress == nullptr || adapter->translateAddress(handle, processor, address, &space, &offset) != 0) {
        return false;
    }

    out_space = space;
    out_offset = offset;
    return true;
}

void Game_Session::Set_Breakpoints(std::span<const ModLoader_Breakpoint> breakpoints, u32 step_processor) {
    if (adapter->setBreakpoints != nullptr) {
        adapter->setBreakpoints(handle, breakpoints.data(), static_cast<u32>(breakpoints.size()), step_processor);
    }
}

s32 Game_Session::Set_Texture_Sources(std::span<const char* const> paths, u32 flags) {
    return adapter->setTextureSources != nullptr && paths.size() <= UINT32_MAX ? adapter->setTextureSources(handle, paths.data(), static_cast<u32>(paths.size()), flags) : MODLOADER_TEXTURE_UNSUPPORTED;
}

bool Game_Session::Resize_Image(u64 size) {
    if (imageBase == nullptr || size > imageCapacity || adapter->resizeImage == nullptr || adapter->resizeImage(handle, size) != 0) {
        return false;
    }
    return true;
}

u64 Game_Session::Peek_Memory(u32 processor, u64 address, std::span<u8> buffer) {
    return adapter->peekMemory != nullptr ? adapter->peekMemory(handle, processor, address, buffer.data(), buffer.size()) : 0;
}

u64 Game_Session::Poke_Memory(u32 processor, u64 address, std::span<const u8> data) {
    return adapter->pokeMemory != nullptr ? adapter->pokeMemory(handle, processor, address, data.data(), data.size()) : 0;
}

s64 Game_Session::Platform_Query(u32 query, void* output, u64 capacity) {
    return adapter->platformCall != nullptr ? adapter->platformCall(handle, query, nullptr, 0, output, capacity) : -1;
}

bool Game_Session::Open_Adapter() {
    std::string path = Find_Adapter(options);
    ModLoader_Get_Platform_Adapter_Function get_adapter;

    if (path.empty()) {
        Log_Error("adapter", "no adapter found");
        return false;
    }

    if (!library.Open(path)) {
        Log_Error("adapter", "cannot load %s", path.c_str());
        return false;
    }

    get_adapter = reinterpret_cast<ModLoader_Get_Platform_Adapter_Function>(library.Symbol("ModLoader_Get_Platform_Adapter"));
    adapter = get_adapter != nullptr ? get_adapter() : nullptr;
    if (adapter == nullptr || adapter->abiVersion != MODLOADER_ADAPTER_ABI_VERSION) {
        Log_Error("adapter", "incompatible adapter %s (expected ABI %u)", path.c_str(), MODLOADER_ADAPTER_ABI_VERSION);
        return false;
    }

    constexpr usize required_size = offsetof(ModLoader_Platform_Adapter, processorDescribe) + sizeof(adapter->processorDescribe);
    if (adapter->structSize < required_size) {
        Log_Error("adapter", "incomplete adapter interface in %s", path.c_str());
        return false;
    }

    memcpy(&adapterStorage, adapter, std::min<usize>(adapter->structSize, sizeof(adapterStorage)));
    adapter = &adapterStorage;
    if (adapter->platformIdentifier == nullptr || adapter->platformIdentifier[0] == 0 || strlen(adapter->platformIdentifier) >= 64 ||
        adapter->adapterIdentifier == nullptr || adapter->adapterIdentifier[0] == 0 || adapter->create == nullptr ||
        adapter->destroy == nullptr || adapter->run == nullptr || adapter->requestQuit == nullptr || adapter->spaceCount == nullptr ||
        adapter->spaceDescribe == nullptr || adapter->processorCount == nullptr || adapter->processorDescribe == nullptr) {
        Log_Error("adapter", "missing identity or callbacks in %s", path.c_str());
        return false;
    }

    platform = Platform_Create(adapter->platformIdentifier);
    return true;
}

bool Game_Session::Start_Adapter(const std::string& lobby_name) {
    ModLoader_Host_Callbacks callbacks = {};
    ModLoader_Adapter_Config config = {};

    callbacks.host = this;
    callbacks.event = [](void* host, uint32_t event, uint64_t time, const void* data, uint64_t size) {
        Session_Of(host).On_Event(event, time, data, size);
    };

    callbacks.hypercall = [](void* host, uint32_t processor, uint32_t id, void* state, uint64_t size) {
        return Session_Of(host).runtime->Hypercall(processor, id, state, size);
    };

    callbacks.breakpoint = [](void* host, const ModLoader_Break* hit, void* state, uint64_t size) {
        Session_Of(host).runtime->Breakpoint(*hit, state, size);
    };

    callbacks.log = [](void*, uint32_t level, const char* message) {
        Log_Write(level <= static_cast<u32>(Log_Level::Debug) ? static_cast<Log_Level>(level) : Log_Level::Info, "adapter", "%s", message);
    };

    callbacks.presentFrame = [](void* host, const ModLoader_Frame* frame) {
        Session_Of(host).presenter->Submit_Frame(*frame);
    };

    callbacks.pushAudio = [](void* host, const int16_t* samples, uint32_t frame_count, uint32_t sample_rate) {
        Session_Of(host).presenter->Push_Audio(samples, frame_count, sample_rate);
    };

    callbacks.pollInput = [](void* host, uint32_t port, void* out_state, uint64_t size) {
        return Session_Of(host).presenter->Poll_Input(port, out_state, size) ? 1 : 0;
    };

    callbacks.setRumble = [](void* host, uint32_t port, uint32_t strength) {
        Session_Of(host).presenter->Set_Rumble(port, strength);
    };

    callbacks.outputSize = [](void* host, uint32_t* out_width, uint32_t* out_height) {
        Session_Of(host).presenter->Output_Size(*out_width, *out_height);
    };

    config.imageData = image.data.data();
    config.imageSize = image.data.size();
    config.contentPath = options.romPath.c_str();
    config.dataDirectory = options.dataDirectory.c_str();
    config.windowMode = MODLOADER_WINDOW_HOST;
    presenter->Gpu_Uuid(config.gpuUuid);
    saveDirectory = options.Online() || !clientSettings.Get("network.server_address").empty() ? Lobby_Save_Directory(options.dataDirectory, lobby_name) : "";
    config.saveDirectory = !saveDirectory.empty() ? saveDirectory.c_str() : nullptr;

    if (adapter->create(&config, &callbacks, &handle) != 0) {
        Log_Error("adapter", "cannot create %s", adapter->adapterIdentifier);
        handle = nullptr;
        return false;
    }

    Load_Adapter_Settings();
    return true;
}

bool Game_Session::Start_Runtime(const std::string& nickname, const std::string& lobby_name) {
    Runtime_Config config;
    ModLoader_Space_Descriptor descriptor;
    u32 space_count = adapter->spaceCount(handle);
    std::unordered_set<std::string> space_names;
    std::unordered_set<std::string> processor_names;

    config.platform = platform.get();
    for (u32 index = 0; index < space_count; index++) {
        descriptor = {};
        if (adapter->spaceDescribe(handle, index, &descriptor) != 0 || descriptor.name == nullptr || descriptor.name[0] == 0 ||
            strlen(descriptor.name) >= sizeof(ModLoader_Module_Space::name) || !space_names.insert(descriptor.name).second ||
            descriptor.usableSize > descriptor.size || descriptor.reportedSize > descriptor.size ||
            descriptor.codec > MODLOADER_CODEC_BIG_ENDIAN_WORD_SWAPPED ||
            ((descriptor.flags & MODLOADER_SPACE_MAPPED) != 0 && (descriptor.hostBase == nullptr || descriptor.size == 0 ||
                descriptor.size % Memory_Page_Size() != 0))) {
            Log_Error("adapter", "invalid space description %u", index);
            return false;
        }

        if (platform->imageSpace != nullptr && strcmp(descriptor.name, platform->imageSpace) == 0 && descriptor.usableSize >= image.data.size()) {
            imageBase = descriptor.hostBase;
            imageCapacity = descriptor.size;
        }
        config.spaces.push_back(descriptor);
    }

    u32 processor_count = adapter->processorCount(handle);
    for (u32 index = 0; index < processor_count; index++) {
        ModLoader_Processor_Descriptor processor = {};
        if (adapter->processorDescribe(handle, index, &processor) != 0 || processor.name == nullptr || processor.name[0] == 0 ||
            processor.stateFormat == nullptr || processor.stateFormat[0] == 0 || strlen(processor.name) >= sizeof(ModLoader_Module_Processor::name) ||
            strlen(processor.stateFormat) >= sizeof(ModLoader_Module_Processor::stateFormat) || !processor_names.insert(processor.name).second) {
            Log_Error("adapter", "invalid processor description %u", index);
            return false;
        }
        config.processors.push_back(processor);
    }

    if (options.host) {
        config.serverConfig = { options.hostPort, options.hostAddress, options.dataDirectory };
    }
    config.lobbyConfig = Lobby_Client::Config {
        .address = options.host ? "" : clientSettings.Get("network.server_address"),
        .port = clientSettings.Server_Port(),
        .nickname = nickname,
        .lobby = lobby_name,
        .password = !options.password.empty() ? options.password : clientSettings.Get("network.password"),
        .dataDirectory = options.dataDirectory,
    };

    if (!options.connect.empty() && !Parse_Server(options.connect, config.lobbyConfig->address, config.lobbyConfig->port)) {
        Log_Error("network", "invalid --connect address %s", options.connect.c_str());
        config.lobbyConfig.reset();
    }

    config.image = &image;
    config.dataDirectory = options.dataDirectory;
    config.moduleExecution = clientSettings.Execution_Mode();
    if (config.moduleExecution != Module_Execution::Interpreter) {
        config.aotCompiler = Aot_Compiler_Path();
    }

    config.session = MODLOADER_SESSION_GAME | (options.host ? MODLOADER_SESSION_HOSTING : 0);
    config.ui = true;
    config.host = this;
    runtime = Runtime::Create(std::move(config));
    return runtime != nullptr;
}

