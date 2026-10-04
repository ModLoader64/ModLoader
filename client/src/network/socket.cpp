#include "network/socket.h"

#include <string.h>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
static_assert(sizeof(SOCKET) == sizeof(Native_Socket), "Native_Socket holds a SOCKET");
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

constexpr u64 gMaxChunk = 0x40000000;
#if defined(MSG_NOSIGNAL)
constexpr int gSendFlags = MSG_NOSIGNAL;
#else
constexpr int gSendFlags = 0;
#endif

int Chunk(u64 size) {
    return static_cast<int>(size < gMaxChunk ? size : gMaxChunk);
}

void Set_No_Delay(Native_Socket socket) {
    int no_delay = 1;

    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&no_delay), sizeof(no_delay));
#if defined(SO_NOSIGPIPE)
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<const char*>(&no_delay), sizeof(no_delay));
#endif
}

bool Would_Block() {
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR;
#endif
}

class Addresses {
public:
    Addresses(const char* host, u16 port, int type, bool passive) {
        addrinfo hints = {};
        std::string service = Text_Format("%u", port);

        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = type;
        hints.ai_flags = passive ? AI_PASSIVE : 0;
        if (getaddrinfo(host, service.c_str(), &hints, &list) != 0) {
            list = nullptr;
        }
    }

    Addresses(const Addresses&) = delete;
    Addresses& operator=(const Addresses&) = delete;
    ~Addresses() {
        if (list != nullptr) {
            freeaddrinfo(list);
        }
    }

    addrinfo* First() const {
        return list;
    }

private:
    addrinfo* list = nullptr;
};

}

bool Network_Start() {
#if defined(_WIN32)
    static std::mutex start_lock;
    static bool started = false;
    std::lock_guard lock(start_lock);

    if (!started) {
        WSADATA data;

        started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    return started;
#else
    return true;
#endif
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        Close();
        handle = other.Release();
    }
    return *this;
}

Native_Socket Socket::Release() {
    return std::exchange(handle, gInvalidSocket);
}

void Socket::Close() {
    if (handle == gInvalidSocket) {
        return;
    }
#if defined(_WIN32)
    closesocket(handle);
#else
    close(handle);
#endif
    handle = gInvalidSocket;
}

void Socket::Shutdown() const {
    if (!Is_Open()) {
        return;
    }
#if defined(_WIN32)
    shutdown(handle, SD_BOTH);
#else
    shutdown(handle, SHUT_RDWR);
#endif
}

bool Socket::Set_Blocking(bool blocking) const {
    if (!Is_Open()) {
        return false;
    }
#if defined(_WIN32)
    u_long non_blocking = blocking ? 0 : 1;

    return ioctlsocket(handle, FIONBIO, &non_blocking) == 0;
#else
    int flags = fcntl(handle, F_GETFL, 0);

    return flags >= 0 && fcntl(handle, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK)) == 0;
#endif
}

bool Socket::Wait(bool for_write, u32 timeout_milliseconds) const {
    Socket_Poll_Entry entry = { handle, for_write, false, false, false };

    if (Socket_Poll({ &entry, 1 }, timeout_milliseconds) <= 0) {
        return false;
    }
    return (for_write ? entry.writable : entry.readable) || entry.failed;
}

Socket Socket::Connect(const char* host, u16 port, u32 timeout_milliseconds) {
    int error;
    socklen_t error_length;
    bool done;

    if (!Network_Start()) {
        return Socket();
    }

    Addresses addresses(host, port, SOCK_STREAM, false);
    for (addrinfo* address = addresses.First(); address != nullptr; address = address->ai_next) {
        Socket socket(::socket(address->ai_family, address->ai_socktype, address->ai_protocol));

        if (!socket.Is_Open()) {
            continue;
        }

        if (!socket.Set_Blocking(false)) {
            continue;
        }

        done = connect(socket.handle, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0;
        if (!done && socket.Wait(true, timeout_milliseconds)) {
            error = 0;
            error_length = sizeof(error);
            getsockopt(socket.handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &error_length);
            done = error == 0;
        }

        if (done && socket.Set_Blocking(true)) {
            Set_No_Delay(socket.handle);
            return socket;
        }
    }

    return Socket();
}

Socket Socket::Listen(const char* address_text, u16 port) {
    int reuse = 1;
    int v6_only = 0;

    if (!Network_Start()) {
        return Socket();
    }

    Addresses addresses(address_text, port, SOCK_STREAM, true);
    for (addrinfo* address = addresses.First(); address != nullptr; address = address->ai_next) {
        Socket socket(::socket(address->ai_family, address->ai_socktype, address->ai_protocol));

        if (!socket.Is_Open()) {
            continue;
        }

        setsockopt(socket.handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        if (address->ai_family == AF_INET6) {
            // Accept ipv4 clients on ipv6 listeners
            setsockopt(socket.handle, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6_only), sizeof(v6_only));
        }

        if (bind(socket.handle, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0 && listen(socket.handle, 16) == 0) {
            return socket;
        }
    }

    return Socket();
}

Socket Socket::Udp(u16 port) {
    if (!Network_Start()) {
        return Socket();
    }

    Addresses addresses(nullptr, port, SOCK_DGRAM, true);
    for (addrinfo* address = addresses.First(); address != nullptr; address = address->ai_next) {
        Socket socket(address->ai_family == AF_INET ? ::socket(address->ai_family, address->ai_socktype, address->ai_protocol) : gInvalidSocket);

        if (socket.Is_Open() && bind(socket.handle, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0) {
            return socket;
        }
    }
    return Socket();
}

Socket Socket::Udp_Peer() const {
    sockaddr_storage address = {};
    socklen_t length = sizeof(address);
    if (getpeername(handle, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return {};
    }
    Socket socket(::socket(address.ss_family, SOCK_DGRAM, IPPROTO_UDP));
    if (!socket.Is_Open() || connect(socket.handle, reinterpret_cast<sockaddr*>(&address), length) != 0 ||
        !socket.Set_Blocking(false)) {
        return {};
    }
    return socket;
}

Socket Socket::Udp_Bound() const {
    sockaddr_storage address = {};
    socklen_t length = sizeof(address);
    if (getsockname(handle, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return {};
    }
    Socket socket(::socket(address.ss_family, SOCK_DGRAM, IPPROTO_UDP));
    if (!socket.Is_Open()) {
        return {};
    }
    if (address.ss_family == AF_INET6) {
        int v6_only = 0;
        socklen_t option_size = sizeof(v6_only);
        if (getsockopt(handle, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char*>(&v6_only), &option_size) != 0 ||
            setsockopt(socket.handle, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6_only), option_size) != 0) {
            return {};
        }
    }
    if (bind(socket.handle, reinterpret_cast<sockaddr*>(&address), length) != 0 || !socket.Set_Blocking(false)) {
        return {};
    }
    return socket;
}

Socket Socket::Accept() const {
    Socket accepted(accept(handle, nullptr, nullptr));

    if (accepted.Is_Open() && accepted.Set_Blocking(true)) {
        Set_No_Delay(accepted.handle);
    }
    else {
        accepted.Close();
    }

    return accepted;
}

s64 Socket::Send_Some(const void* data, u64 size) const {
    int result = send(handle, static_cast<const char*>(data), Chunk(size), gSendFlags);
    return result >= 0 ? result : Would_Block() ? 0 : -1;
}

s64 Socket::Receive_Some(void* buffer, u64 size) const {
    int result = recv(handle, static_cast<char*>(buffer), Chunk(size), 0);
    return result > 0 ? result : result < 0 && Would_Block() ? 0 : -1;
}

s64 Socket::Send(const void* data, u64 size) const {
    const char* bytes = static_cast<const char*>(data);
    u64 sent = 0;
    int result;

    while (sent < size) {
        result = send(handle, bytes + sent, Chunk(size - sent), gSendFlags);
        if (result <= 0) {
            return sent != 0 ? static_cast<s64>(sent) : -1;
        }
        
        sent += static_cast<u64>(result);
    }

    return static_cast<s64>(sent);
}

s64 Socket::Receive(void* buffer, u64 size) const {
    int result = recv(handle, static_cast<char*>(buffer), Chunk(size), 0);
    return result < 0 ? -1 : result;
}

s64 Socket::Send_To(const char* host, u16 port, const void* data, u64 size) const {
    Addresses addresses(host, port, SOCK_DGRAM, false);

    for (addrinfo* address = addresses.First(); address != nullptr; address = address->ai_next) {
        if (address->ai_family == AF_INET) {
            return sendto(handle, static_cast<const char*>(data), Chunk(size), 0, address->ai_addr, static_cast<int>(address->ai_addrlen));
        }
    }

    return -1;
}

s64 Socket::Receive_From(void* buffer, u64 size, u32& out_address, u16& out_port) const {
    s64 result = Receive_Datagram(buffer, size, out_address, out_port);
    return result < 0 ? -1 : result;
}

s64 Socket::Send_Datagram(u32 address, u16 port, const void* data, u64 size) const {
    sockaddr_in destination = {};

    destination.sin_family = AF_INET;
    destination.sin_addr.s_addr = htonl(address);
    destination.sin_port = htons(port);
    int result = sendto(handle, static_cast<const char*>(data), Chunk(size), 0, reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));

    return result >= 0 ? result : Would_Block() ? -2 : -1;
}

s64 Socket::Receive_Datagram(void* buffer, u64 size, u32& out_address, u16& out_port) const {
    sockaddr_in from = {};
    socklen_t from_length = sizeof(from);
    int result = recvfrom(handle, static_cast<char*>(buffer), Chunk(size), 0, reinterpret_cast<sockaddr*>(&from), &from_length);

    out_address = ntohl(from.sin_addr.s_addr);
    out_port = ntohs(from.sin_port);
    return result >= 0 ? result : Would_Block() ? -2 : -1;
}

s64 Socket::Send_Datagram(const Socket_Address& destination, const void* data, u64 size) const {
    sockaddr_storage address = {};
    socklen_t length;
    if (destination.ipv6) {
        auto& ipv6 = reinterpret_cast<sockaddr_in6&>(address);
        ipv6.sin6_family = AF_INET6;
        ipv6.sin6_port = htons(destination.port);
        ipv6.sin6_scope_id = destination.scope;
        memcpy(&ipv6.sin6_addr, destination.address, sizeof(ipv6.sin6_addr));
        length = sizeof(ipv6);
    }
    else {
        auto& ipv4 = reinterpret_cast<sockaddr_in&>(address);
        ipv4.sin_family = AF_INET;
        ipv4.sin_port = htons(destination.port);
        memcpy(&ipv4.sin_addr, destination.address, sizeof(ipv4.sin_addr));
        length = sizeof(ipv4);
    }
    int result = sendto(handle, static_cast<const char*>(data), Chunk(size), 0, reinterpret_cast<const sockaddr*>(&address), length);
    return result >= 0 ? result : Would_Block() ? -2 : -1;
}

s64 Socket::Receive_Datagram(void* buffer, u64 size, Socket_Address& out_address) const {
    sockaddr_storage address = {};
    socklen_t length = sizeof(address);
    int result = recvfrom(handle, static_cast<char*>(buffer), Chunk(size), 0, reinterpret_cast<sockaddr*>(&address), &length);
    if (result < 0) {
        return Would_Block() ? -2 : -1;
    }
    out_address = {};
    out_address.ipv6 = address.ss_family == AF_INET6;
    if (out_address.ipv6) {
        const auto& ipv6 = reinterpret_cast<const sockaddr_in6&>(address);
        out_address.port = ntohs(ipv6.sin6_port);
        out_address.scope = ipv6.sin6_scope_id;
        memcpy(out_address.address, &ipv6.sin6_addr, sizeof(ipv6.sin6_addr));
    }
    else {
        const auto& ipv4 = reinterpret_cast<const sockaddr_in&>(address);
        out_address.port = ntohs(ipv4.sin_port);
        memcpy(out_address.address, &ipv4.sin_addr, sizeof(ipv4.sin_addr));
    }
    return result;
}

u16 Socket::Port() const {
    sockaddr_storage address = {};
    socklen_t length = sizeof(address);

    if (getsockname(handle, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return 0;
    }

    if (address.ss_family == AF_INET) {
        return ntohs(reinterpret_cast<sockaddr_in*>(&address)->sin_port);
    }
    
    return address.ss_family == AF_INET6 ? ntohs(reinterpret_cast<sockaddr_in6*>(&address)->sin6_port) : 0;
}

s32 Socket_Poll(std::span<Socket_Poll_Entry> entries, u32 timeout_milliseconds) {
#if defined(_WIN32)
    using Poll_Record = WSAPOLLFD;
    constexpr short gRead = POLLRDNORM;
    constexpr short gWrite = POLLWRNORM;
#else
    using Poll_Record = pollfd;
    constexpr short gRead = POLLIN;
    constexpr short gWrite = POLLOUT;
#endif
    std::vector<Poll_Record> records(entries.size());
    s32 ready;
    short events;

    for (usize index = 0; index < entries.size(); index++) {
        records[index].fd = entries[index].socket;
        records[index].events = static_cast<short>(gRead | (entries[index].watchWrite ? gWrite : 0));
    }
#if defined(_WIN32)
    ready = WSAPoll(records.data(), static_cast<ULONG>(records.size()), static_cast<INT>(timeout_milliseconds));
#else
    ready = poll(records.data(), records.size(), static_cast<int>(timeout_milliseconds));
    if (ready < 0 && errno == EINTR) {
        ready = 0; // let the caller observe shutdown after a signal
    }
#endif
    for (usize index = 0; index < entries.size(); index++) {
        events = ready > 0 ? records[index].revents : 0;
        entries[index].readable = (events & gRead) != 0;
        entries[index].writable = (events & gWrite) != 0;
        entries[index].failed = (events & (POLLERR | POLLHUP | POLLNVAL)) != 0;
    }
    return ready;
}
