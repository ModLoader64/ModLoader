#include "module.h"
#include "network/packets.h"
#include "network/socket.h"

#include "modloader_net.h"

#include <algorithm>
#include <deque>
#include <string.h>

enum class Socket_Kind {
    Stream,
    Listener,
    Datagram,
};

struct Queued_Packet {
    std::vector<u8> bytes;
    u32 address;
    u16 port;
};

struct Packet_State {
    std::vector<u8> input; // emulation thread only
    usize inputOffset = 0;
    std::deque<Queued_Packet> output;
    usize outputOffset = 0;
    u64 queuedBytes = 0;
};

struct Module_Socket {
    Socket socket;
    Socket_Kind kind;
    std::atomic<bool> closed = false;
    std::mutex lock;
    u32 rawOperations = 0;
    std::unique_ptr<Packet_State> packets;

    bool Close() {
        if (closed.exchange(true)) {
            return false;
        }
        socket.Shutdown();
        return true;
    }
};

namespace {

constexpr u32 gPacketMaxPending = 1024;
constexpr u32 gSocketEventBudget = 16;
constexpr u64 gSocketByteBudget = 256 * 1024;
constexpr u64 gPacketReadChunk = 64 * 1024;

u32 Add(Module& module, Socket socket, Socket_Kind kind = Socket_Kind::Stream, bool packets = false) {
    if (!socket.Is_Open() || (packets && !socket.Set_Blocking(false))) {
        return 0;
    }

    auto entry = std::make_shared<Module_Socket>();
    entry->socket = std::move(socket);
    entry->kind = kind;

    if (packets) {
        entry->packets = std::make_unique<Packet_State>();
    }

    std::lock_guard lock(module.resourceLock);
    if (module.stopping || module.nextSocket == UINT32_MAX) {
        return 0;
    }

    u32 handle = ++module.nextSocket;
    module.sockets.emplace(handle, std::move(entry));
    return handle;
}

std::shared_ptr<Module_Socket> Socket_Of(Module& module, u64 handle) {
    std::lock_guard lock(module.resourceLock);
    auto found = handle <= UINT32_MAX ? module.sockets.find(static_cast<u32>(handle)) : module.sockets.end();
    return found != module.sockets.end() ? found->second : nullptr;
}

struct Raw_Access {
    std::shared_ptr<Module_Socket> entry;

    Raw_Access(const Raw_Access&) = delete;
    Raw_Access& operator=(const Raw_Access&) = delete;

    Raw_Access(Module& module, u64 handle) {
        auto found = Socket_Of(module, handle);
        if (found != nullptr) {
            std::lock_guard lock(found->lock);
            if (!found->closed && !found->packets) {
                found->rawOperations++;
                entry = std::move(found);
            }
        }
    }

    ~Raw_Access() {
        if (entry != nullptr) {
            std::lock_guard lock(entry->lock);
            entry->rawOperations--;
        }
    }
};

void Close(Module& module, u64 handle) {
    std::shared_ptr<Module_Socket> entry;
    {
        std::lock_guard lock(module.resourceLock);
        auto found = handle <= UINT32_MAX ? module.sockets.find(static_cast<u32>(handle)) : module.sockets.end();
        if (found == module.sockets.end()) {
            return;
        }

        entry = std::move(found->second);
        module.sockets.erase(found);
    }
    entry->Close();
}

bool Wait(const std::shared_ptr<Module_Socket>& entry, u32 timeout) {
    u64 deadline = Time_Monotonic_Milliseconds() + timeout;
    do {
        if (!entry || entry->closed) {
            return false;
        }

        u64 now = Time_Monotonic_Milliseconds();
        u32 remaining = now < deadline ? static_cast<u32>(deadline - now) : 0;
        if (entry->socket.Wait(false, remaining < 50 ? remaining : 50)) {
            return !entry->closed;
        }

    } while (Time_Monotonic_Milliseconds() < deadline);
    return false;
}

bool Enable_Packets(Module& module, u64 handle) {
    auto entry = Socket_Of(module, handle);
    if (entry == nullptr || module.stopping) {
        return false;
    }

    std::lock_guard lock(entry->lock);
    if (entry->closed || entry->rawOperations != 0) {
        return false;
    }

    if (!entry->packets) {
        if (!entry->socket.Set_Blocking(false)) {
            return false;
        }
        entry->packets = std::make_unique<Packet_State>();
    }

    return true;
}

bool Send_Packet(Module& module, u64* slots, bool datagram) {
    auto entry = Socket_Of(module, slots[0]);
    if (entry == nullptr || module.stopping || slots[2] == 0 || slots[2] > 64 || slots[5] > gPacketMaxPayload ||
        (datagram && (slots[7] == 0 || slots[7] > UINT16_MAX || gPacketHeaderSize + slots[2] + slots[5] > gPacketMaxDatagram))) {
        return false;
    }

    std::optional<std::string_view> type = module.Text(slots[1], slots[2]);
    std::optional<std::string_view> data = module.Text(slots[4], slots[5]);

    if (!type || !data) {
        return false;
    }

    std::lock_guard lock(entry->lock);
    u64 frame_size = gPacketHeaderSize + type->size() + data->size();

    if (entry->closed || !entry->packets || entry->kind != (datagram ? Socket_Kind::Datagram : Socket_Kind::Stream) ||
        entry->packets->output.size() >= gPacketMaxPending || entry->packets->queuedBytes + frame_size > gPacketMaxQueued) {
        return false;
    }

    Queued_Packet packet = {};
    if (!Packet_Encode(*type, static_cast<u32>(slots[3]), { reinterpret_cast<const u8*>(data->data()), data->size() }, packet.bytes)) {
        return false;
    }

    if (datagram) {
        packet.address = static_cast<u32>(slots[6]);
        packet.port = static_cast<u16>(slots[7]);
    }

    entry->packets->queuedBytes += frame_size;
    entry->packets->output.push_back(std::move(packet));
    return true;
}

NativeSymbol sNatives[] = {
    Native("tcp_connect", "(IIii)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        std::optional<std::string_view> host = module.Text(slots[0], slots[1]);

        slots[0] = host ? Add(module, Socket::Connect(std::string(*host).c_str(), static_cast<u16>(slots[2]), static_cast<u32>(slots[3]))) : 0;
    }),
    Native("tcp_listen", "(IIi)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        auto host = module.Text(slots[0], slots[1]);
        if (!host) {
            slots[0] = 0;
            return;
        }

        std::string address(*host);
        Socket socket = Socket::Listen(!address.empty() ? address.c_str() : nullptr, static_cast<u16>(slots[2]));
        slots[0] = socket.Set_Blocking(false) ? Add(module, std::move(socket), Socket_Kind::Listener) : 0;
    }),
    Native("tcp_accept", "(ii)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        Raw_Access access(module, slots[0]);
        auto listener = access.entry;
        slots[0] = Wait(listener, static_cast<u32>(slots[1])) ? Add(module, listener->socket.Accept()) : 0;
    }),
    Native("socket_port", "(i)i", [](wasm_exec_env_t exec_env, u64* slots) {
        auto socket = Socket_Of(Module_Of(exec_env), slots[0]);
        slots[0] = socket != nullptr && !socket->closed ? socket->socket.Port() : 0;
    }),
    Native("socket_send", "(iII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        Raw_Access access(module, slots[0]);
        auto socket = access.entry;
        const void* data = module.Memory(slots[1], slots[2]);
        slots[0] = static_cast<u64>(socket != nullptr && !socket->closed && data != nullptr ? socket->socket.Send(data, slots[2]) : -1);
    }),
    Native("socket_receive", "(iIIi)I", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        Raw_Access access(module, slots[0]);
        auto socket = access.entry;
        void* buffer = module.Memory(slots[1], slots[2]);

        if (socket == nullptr || socket->closed || buffer == nullptr) {
            slots[0] = static_cast<u64>(-1);
        }
        else {
            slots[0] = static_cast<u64>(Wait(socket, static_cast<u32>(slots[3])) ? socket->socket.Receive(buffer, slots[2]) : -2);
        }
    }),
    Native("udp_open", "(i)i", [](wasm_exec_env_t exec_env, u64* slots) {
        Socket socket = Socket::Udp(static_cast<u16>(slots[0]));
        slots[0] = socket.Set_Blocking(false) ? Add(Module_Of(exec_env), std::move(socket), Socket_Kind::Datagram) : 0;
    }),
    Native("udp_send_to", "(iIIiII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        Raw_Access access(module, slots[0]);
        auto socket = access.entry;
        std::optional<std::string_view> host = module.Text(slots[1], slots[2]);
        const void* data = module.Memory(slots[4], slots[5]);
        slots[0] = static_cast<u64>(
            socket != nullptr && !socket->closed && host && data != nullptr ? socket->socket.Send_To(std::string(*host).c_str(), static_cast<u16>(slots[3]), data, slots[5]) : -1
        );
    }),
    // output is: address, port
    Native("udp_receive_from", "(iIIIi)I", [](wasm_exec_env_t exec_env, u64* slots) {
        Module& module = Module_Of(exec_env);
        Raw_Access access(module, slots[0]);
        auto socket = access.entry;
        void* buffer = module.Memory(slots[1], slots[2]);
        u32* sender = slots[3] != 0 ? module.Memory_As<u32>(slots[3], 2) : nullptr;
        u32 address = 0;
        u16 port = 0;
        s64 result;

        if (socket == nullptr || socket->closed || buffer == nullptr) {
            slots[0] = static_cast<u64>(-1);
            return;
        }

        if (!Wait(socket, static_cast<u32>(slots[4]))) {
            slots[0] = static_cast<u64>(-2);
            return;
        }

        result = socket->socket.Receive_From(buffer, slots[2], address, port);
        if (result >= 0 && sender != nullptr) {
            sender[0] = address;
            sender[1] = port;
        }
        slots[0] = static_cast<u64>(result);
    }),
    Native("socket_close", "(i)", [](wasm_exec_env_t exec_env, u64* slots) {
        Close(Module_Of(exec_env), slots[0]);
    }),
    Native("socket_packets", "(i)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Enable_Packets(Module_Of(exec_env), slots[0]) ? 1 : 0;
    }),
    Native("socket_packet_send", "(iIIiII)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Send_Packet(Module_Of(exec_env), slots, false) ? 1 : 0;
    }),
    Native("socket_packet_send_to", "(iIIiIIii)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Send_Packet(Module_Of(exec_env), slots, true) ? 1 : 0;
    }),
};

struct Watched_Socket {
    Module* owner;
    u32 handle;
    std::shared_ptr<Module_Socket> entry;
};

bool Active(const Watched_Socket& watched) {
    return !watched.owner->stopping && !watched.owner->disabled && !watched.entry->closed;
}

void Closed(const Watched_Socket& watched, u64 time) {
    if (!watched.entry->Close()) {
        return;
    }

    {
        std::lock_guard lock(watched.owner->resourceLock);
        watched.owner->sockets.erase(watched.handle);
    }

    if (!watched.owner->stopping && !watched.owner->disabled) {
        ModLoader_Net_Record record = {};
        record.header = { time, Time_Monotonic_Milliseconds() };
        record.kind = MODLOADER_NET_CLOSED;
        record.socket = watched.handle;
        watched.owner->Deliver(MODLOADER_EVENT_NET, &record, sizeof(record), false);
    }
}

void Packet_Record(std::vector<u8>& out, u32 socket, u64 time, const Packet_View& packet, u32 address = 0, u16 port = 0,
    ModLoader_Net_Transport transport = MODLOADER_NET_TCP) {
    ModLoader_Net_Record record = {};
    record.header = { time, Time_Monotonic_Milliseconds() };
    record.kind = MODLOADER_NET_PACKET;
    record.socket = socket;
    record.address = address;
    record.port = port;
    record.transport = transport;
    record.version = packet.version;
    record.size = packet.payload.size();
    Text_Copy(record.type, packet.type);
    out.resize(sizeof(record) + packet.payload.size());
    memcpy(out.data(), &record, sizeof(record));
    if (!packet.payload.empty()) {
        memcpy(out.data() + sizeof(record), packet.payload.data(), packet.payload.size());
    }
}

bool Flush_Packets(const Watched_Socket& watched, u64& bytes, u32& work) {
    std::lock_guard lock(watched.entry->lock);
    Packet_State& state = *watched.entry->packets;
    u64 remaining = std::min(bytes, gSocketByteBudget);
    for (u32 index = 0; index < gSocketEventBudget && work != 0 && remaining != 0 && !state.output.empty() && Active(watched); index++) {
        Queued_Packet& packet = state.output.front();
        u64 size = packet.bytes.size() - state.outputOffset;
        s64 sent;

        if (watched.entry->kind == Socket_Kind::Datagram) {
            if (size > remaining) {
                break;
            }

            work--;
            sent = watched.entry->socket.Send_Datagram(packet.address, packet.port, packet.bytes.data(), size);
            if (sent == -2) {
                break;
            }

            if (sent != static_cast<s64>(size)) {
                return false;
            }
        }
        else {
            work--;
            sent = watched.entry->socket.Send_Some(packet.bytes.data() + state.outputOffset, std::min(size, remaining));
            if (sent < 0) {
                return false;
            }

            if (sent == 0) {
                break;
            }
        }

        state.outputOffset += static_cast<usize>(sent);
        remaining -= static_cast<u64>(sent);
        bytes -= static_cast<u64>(sent);
        if (state.outputOffset == packet.bytes.size()) {
            state.queuedBytes -= packet.bytes.size();
            state.output.pop_front();
            state.outputOffset = 0;
        }
    }
    return true;
}

void Accept_Packets(const Watched_Socket& watched, u64 time, u32& work) {
    for (u32 index = 0; index < gSocketEventBudget && work != 0 && Active(watched); index++) {
        work--;
        Socket socket = watched.entry->socket.Accept();

        if (!socket.Is_Open()) {
            break;
        }

        u32 accepted = Add(*watched.owner, std::move(socket), Socket_Kind::Stream, true);
        if (accepted == 0) {
            break;
        }

        if (!Active(watched)) {
            Close(*watched.owner, accepted);
            break;
        }
        ModLoader_Net_Record record = {};
        record.header = { time, Time_Monotonic_Milliseconds() };
        record.kind = MODLOADER_NET_ACCEPTED;
        record.socket = watched.handle;
        record.accepted = accepted;
        watched.owner->Deliver(MODLOADER_EVENT_NET, &record, sizeof(record), false);
    }
}

void Read_Stream(const Watched_Socket& watched, bool readable, u64 time, u64& bytes, u32& work) {
    u64 remaining = std::min(bytes, gSocketByteBudget);
    u32 delivered = 0;
    std::vector<u8> record;
    Packet_State& state = *watched.entry->packets;

    while (work != 0 && bytes != 0 && delivered < gSocketEventBudget && Active(watched)) {
        bool closed = false;
        Packet_View packet;
        u64 size = 0;
        Packet_Result result = Packet_Decode(std::span<const u8>(state.input).subspan(state.inputOffset), packet, size);

        if (result == Packet_Result::Complete) {
            Packet_Record(record, watched.handle, time, packet);
            state.inputOffset += static_cast<usize>(size);
            if (state.inputOffset == state.input.size()) {
                state.input.clear();
                state.inputOffset = 0;
            }
            work--;
            delivered++;
            bytes -= std::min<u64>(bytes, packet.payload.size());
        }
        else if (result == Packet_Result::Invalid) {
            work--;
            closed = true;
        }
        else {
            if (!readable || remaining == 0) {
                break;
            }
            if (state.inputOffset != 0) {
                state.input.erase(state.input.begin(), state.input.begin() + static_cast<std::ptrdiff_t>(state.inputOffset));
                state.inputOffset = 0;
            }
            u64 count = std::min({ bytes, remaining, gPacketReadChunk, gPacketMaxQueued - state.input.size() });
            usize start = state.input.size();
            if (count == 0) {
                work--;
                closed = true;
            }
            else {
                state.input.resize(start + static_cast<usize>(count));
                work--;
                s64 received = watched.entry->socket.Receive_Some(state.input.data() + start, count);
                state.input.resize(start + (received > 0 ? static_cast<usize>(received) : 0));
                if (received > 0) {
                    remaining -= static_cast<u64>(received);
                    bytes -= static_cast<u64>(received);
                    continue;
                }

                if (received == 0) {
                    break;
                }
                closed = true;
            }
        }

        if (closed) {
            Closed(watched, time);
            break;
        }

        if (Active(watched)) {
            watched.owner->Deliver(MODLOADER_EVENT_NET, record.data(), record.size(), false);
        }
    }
}

void Read_Datagrams(const Watched_Socket& watched, u64 time, u64& bytes, u32& work) {
    u8 buffer[gPacketReadChunk];
    u64 remaining = std::min(bytes, gSocketByteBudget);
    std::vector<u8> record;
    for (u32 index = 0; index < gSocketEventBudget && work != 0 && remaining >= gPacketMaxDatagram && Active(watched); index++) {
        u32 address = 0;
        u16 port = 0;
        work--;
        s64 received = watched.entry->socket.Receive_Datagram(buffer, sizeof(buffer), address, port);

        if (received == -2) {
            break;
        }

        if (received < 0) {
            Closed(watched, time);
            break;
        }

        remaining -= static_cast<u64>(received);
        bytes -= static_cast<u64>(received);
        Packet_View packet;
        u64 size = 0;

        if (Packet_Decode({ buffer, static_cast<usize>(received) }, packet, size) != Packet_Result::Complete || size != static_cast<u64>(received)) {
            continue;
        }

        Packet_Record(record, watched.handle, time, packet, address, port, MODLOADER_NET_UDP);
        if (Active(watched)) {
            watched.owner->Deliver(MODLOADER_EVENT_NET, record.data(), record.size(), false);
        }
    }
}

} // namespace

void Net_Register_Natives() {
    Register_Natives(sNatives, "network");
}

void Net_Release_Owner(Module& owner) {
    std::unordered_map<u32, std::shared_ptr<Module_Socket>> closing;
    {
        std::lock_guard lock(owner.resourceLock);
        closing.swap(owner.sockets);
    }

    for (auto& [handle, entry] : closing) {
        entry->Close();
    }
}

void Net_Dispatch(Runtime& runtime, u64 time) {
    if (runtime.networkDispatching) {
        return;
    }

    std::vector<Watched_Socket> watched;
    std::vector<Socket_Poll_Entry> polling;

    for (const auto& module : runtime.modules) {
        if (module->stopping || module->disabled) {
            continue;
        }

        std::lock_guard resources(module->resourceLock);
        for (const auto& [handle, entry] : module->sockets) {
            std::lock_guard lock(entry->lock);
            if (entry->packets && !entry->closed) {
                watched.push_back({ module.get(), handle, entry });
                polling.push_back({ entry->socket.Native(), !entry->packets->output.empty(), false, false, false });
            }
        }
    }

    if (watched.empty() || Socket_Poll(polling, 0) < 0) {
        return;
    }

    runtime.networkDispatching = true;
    usize start = runtime.networkCursor % watched.size();
    u64 bytes = 2 * 1024 * 1024;
    u32 work = 128;

    for (usize offset = 0; offset < watched.size() && work != 0 && bytes != 0; offset++) {
        usize index = (start + offset) % watched.size();
        const Watched_Socket& socket = watched[index];
        const Socket_Poll_Entry& ready = polling[index];

        runtime.networkCursor = index + 1;
        if (!Active(socket)) {
            continue;
        }

        if (ready.writable && !Flush_Packets(socket, bytes, work)) {
            Closed(socket, time);
            continue;
        }
        
        if (socket.entry->kind == Socket_Kind::Stream) {
            Read_Stream(socket, ready.readable || ready.failed, time, bytes, work);
        }
        else if (ready.readable || ready.failed) {
            if (socket.entry->kind == Socket_Kind::Listener) {
                Accept_Packets(socket, time, work);
                if (ready.failed) {
                    Closed(socket, time);
                }
            }
            else {
                Read_Datagrams(socket, time, bytes, work);
            }
        }
    }
    runtime.networkDispatching = false;
}
