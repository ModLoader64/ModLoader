#include "module.h"
#include "network/lobby_client.h"
#include "network/lobby_server.h"

#include "modloader_lobby.h"

#include <string.h>
#include <utility>

namespace {

ModLoader_Lobby_Member Member_Record(const Lobby_Member& member) {
    ModLoader_Lobby_Member record = {};

    memcpy(record.id, member.id.data(), sizeof(record.id));
    Text_Copy(record.nickname, member.nickname);
    return record;
}

std::optional<std::string_view> Topic(const Module& module, u64 address, u64 length) {
    return length != 0 && length <= gMaxTopic ? module.Text(address, length) : std::nullopt;
}

template <typename T>
void Copy_Out(const Module& module, u64 address, u64 size, const T& record) {
    u64 count = size < sizeof(record) ? size : sizeof(record);
    void* destination = module.Memory(address, count);

    if (destination != nullptr) {
        memcpy(destination, &record, count);
    }
}

bool Send(Module& module, u64* slots, bool to_server) {
    Lobby_Client* client = module.runtime.Lobby();
    std::optional<std::string_view> topic = Topic(module, slots[0], slots[1]);
    std::optional<std::string_view> data = module.Text(slots[2], slots[3]);

    if (client == nullptr || !topic || !data) {
        return false;
    }

    std::span<const u8> payload(reinterpret_cast<const u8*>(data->data()), data->size());
    return to_server
        ? client->Send_Server(*topic, payload, slots[4] != 0, static_cast<Network_Transport>(slots[5]))
        : client->Send(*topic, payload, static_cast<Network_Transport>(slots[4]));
}

NativeSymbol sNatives[] = {
    Native("network_configure", "(i)i", [](wasm_exec_env_t exec_env, u64* slots) {
        u32 port = static_cast<u32>(slots[0]);
        slots[0] = Require_Emulation_Thread(exec_env, "network configuration") && port != 0 && port <= UINT16_MAX && Module_Of(exec_env).runtime.Configure_Network(static_cast<u16>(port));
    }),
    Native("lobby_packet_follow", "(iIIi)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        u32 event = static_cast<u32>(slots[0]);
        auto topic = slots[2] <= gMaxTopic ? module.Text(slots[1], slots[2]) : std::nullopt;

        if (!Require_Emulation_Thread(exec_env, "packet registration") || !topic ||
            (event != MODLOADER_EVENT_LOBBY && event != MODLOADER_EVENT_SERVER) ||
            (!topic->empty() && !Text_Is_Valid(*topic, gMaxTopic))) {
            slots[0] = 0;
            return;
        }

        auto& packets = event == MODLOADER_EVENT_LOBBY ? module.lobbyPackets : module.serverPackets;
        if (slots[3] != 0) {
            packets.emplace(*topic);
        }
        else {
            packets.erase(std::string(*topic));
        }

        slots[0] = 1;
    }),
    Native("lobby_status", "(II)", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        const Lobby_Client* client = module.runtime.Lobby();
        ModLoader_Lobby_Status status = {};
        status.state = MODLOADER_LOBBY_STATUS_DISCONNECTED;

        if (client != nullptr) {
            status.state = static_cast<u32>(client->State());
            status.memberCount = client->Member_Count();
            status.self = Member_Record(client->Self());
            Text_Copy(status.lobby, client->Lobby_Name());
            client->Lobby_Key(status.lobbyKey);
        }

        Copy_Out(module, slots[0], slots[1], status);
    }),
    Native("lobby_member", "(iII)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        const Lobby_Client* client = module.runtime.Lobby();
        auto member = client != nullptr ? client->Member_At(static_cast<u32>(slots[0])) : std::nullopt;
        if (member) {
            Copy_Out(module, slots[1], slots[2], Member_Record(*member));
        }
        slots[0] = member ? 1 : 0;
    }),
    Native("lobby_send", "(IIIIi)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Send(Module_Of(exec_env), slots, false) ? 1 : 0;
    }),
    Native("lobby_send_to", "(IIIIIi)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        Lobby_Client* client = module.runtime.Lobby();
        const Member_Id* member = module.Memory_As<Member_Id>(slots[0]);
        auto topic = Topic(module, slots[1], slots[2]);
        const void* data = module.Memory(slots[3], slots[4]);

        slots[0] = client != nullptr && member != nullptr && topic && data != nullptr &&
            client->Send_To(*member, *topic, { static_cast<const u8*>(data), static_cast<usize>(slots[4]) },
                static_cast<Network_Transport>(slots[5])) ? 1 : 0;
    }),
    Native("lobby_send_server", "(IIIIii)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Send(Module_Of(exec_env), slots, true) ? 1 : 0;
    }),
    Native("lobby_read", "(III)I", [](wasm_exec_env_t exec_env, u64* slots) {
        if (!Require_Emulation_Thread(exec_env, "packet payload")) {
            slots[0] = 0;
            return;
        }

        Module& module = Module_Of(exec_env);
        std::span<const u8> payload = module.runtime.lobbyPayload;
        u64 offset = slots[0] < payload.size() ? slots[0] : payload.size();
        u64 count = payload.size() - offset < slots[2] ? payload.size() - offset : slots[2];
        void* destination = count != 0 ? module.Memory(slots[1], count) : nullptr;

        if (destination != nullptr) {
            memcpy(destination, payload.data() + offset, count);
        }
        slots[0] = destination != nullptr ? count : 0;
    }),
    Native("server_send", "(iIIIIIIii)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        Lobby_Server* server = module.runtime.Server();
        const Member_Id* member = slots[1] != 0 ? module.Memory_As<Member_Id>(slots[1]) : nullptr;
        const Member_Id* except = slots[2] != 0 ? module.Memory_As<Member_Id>(slots[2]) : nullptr;
        bool addressed = (slots[1] == 0 || member != nullptr) && (slots[2] == 0 || except != nullptr);
        std::optional<std::string_view> topic = Topic(module, slots[3], slots[4]);
        std::optional<std::string_view> data = module.Text(slots[5], slots[6]);
        slots[0] = server != nullptr && addressed && topic && data &&
            server->Send(static_cast<u32>(slots[0]), member, except, *topic,
                { reinterpret_cast<const u8*>(data->data()), data->size() }, slots[7] != 0,
                static_cast<Network_Transport>(slots[8])) ? 1 : 0;
    }),
};

void Deliver(Runtime& runtime, u32 event, const std::string& topic, bool message, const void* record, u64 size,
    std::span<const u8> payload) {
    std::vector<Module*> recipients;

    for (const auto& module : runtime.modules) {
        const auto& packets = event == MODLOADER_EVENT_LOBBY ? module->lobbyPackets : module->serverPackets;

        if (!module->stopping && !module->disabled && (module->Follows(event) ||
            (message ? packets.contains(topic) : !packets.empty()))) {
            recipients.push_back(module.get());
        }
    }

    auto previous = std::exchange(runtime.lobbyPayload, payload);
    for (Module* module : recipients) {
        module->Deliver(event, record, size, false);
    }
    runtime.lobbyPayload = previous;
}

} // namespace

void Lobby_Register_Natives() {
    Register_Natives(sNatives, "lobby");
}

bool Runtime::Configure_Network(u16 port) {
    if (networkStarted || port == 0) {
        return false;
    }

    if (defaultNetworkPort != 0 && defaultNetworkPort != port) {
        Log_Error("network", "conflicting plugin ports: %u and %u", defaultNetworkPort, port);
        return false;
    }

    defaultNetworkPort = port;
    return true;
}

void Runtime::Start_Network() {
    if (networkStarted) {
        return;
    }

    networkStarted = true;
    u16 port = defaultNetworkPort != 0 ? defaultNetworkPort : gDefaultPort;
    if (config.serverConfig && Server() == nullptr) {
        auto& server_config = *config.serverConfig;
        if (server_config.port != 0) {
            port = server_config.port;
        }
        server_config.port = port;
        ownedServer = Lobby_Server::Start(server_config);
        activeServer = ownedServer.get();
    }

    if (config.lobbyConfig && Lobby() == nullptr) {
        auto& lobby_config = *config.lobbyConfig;
        if (lobby_config.port == 0) {
            lobby_config.port = defaultNetworkPort != 0 ? defaultNetworkPort : gDefaultPort;
        }

        if (lobby_config.address.empty() && Server() != nullptr) {
            std::string address = config.serverConfig ? config.serverConfig->address : "";
            if (address.empty() || address == "0.0.0.0") {
                address = "127.0.0.1";
            }
            else if (address == "::") {
                address = "::1";
            }

            lobby_config.address = std::move(address);
            lobby_config.port = port;
        }

        ownedLobby = Lobby_Client::Start(lobby_config);
        activeLobby = ownedLobby.get();
    }
}

void Lobby_Dispatch(Runtime& runtime, u64 time) {
    std::optional<Lobby_Event> event;
    std::optional<Server_Event> server_event;
    Lobby_Client* client = runtime.Lobby();
    Lobby_Server* server = runtime.Server();

    while (client != nullptr && (event = client->Next_Event())) {
        ModLoader_Lobby_Record record = {};

        record.header = { time, Time_Monotonic_Milliseconds() };
        record.kind = static_cast<u32>(event->kind);
        record.sealed = event->sealed ? 1 : 0;
        record.transport = static_cast<u32>(event->transport);
        record.size = event->payload.size();
        record.member = Member_Record(event->member);
        Text_Copy(record.topic, event->topic);
        Deliver(runtime, MODLOADER_EVENT_LOBBY, event->topic,
            event->kind == Lobby_Event_Kind::Message || event->kind == Lobby_Event_Kind::Server_Message, &record, sizeof(record), event->payload);
    }

    while (server != nullptr && (server_event = server->Next_Event())) {
        ModLoader_Server_Record record = {};

        record.header = { time, Time_Monotonic_Milliseconds() };
        record.kind = static_cast<u32>(server_event->kind);
        record.lobby = server_event->lobby;
        record.size = server_event->payload.size();
        record.sealed = server_event->sealed ? 1 : 0;
        record.transport = static_cast<u32>(server_event->transport);
        record.member = Member_Record(server_event->member);
        memcpy(record.publicKey, server_event->publicKey.data(), sizeof(record.publicKey));
        Text_Copy(record.lobbyName, server_event->lobbyName);
        Text_Copy(record.topic, server_event->topic);
        Deliver(runtime, MODLOADER_EVENT_SERVER, server_event->topic, server_event->kind == Server_Event_Kind::Message, &record, sizeof(record), server_event->payload);
    }
}
