#pragma once

#include "base.h"

#if defined(_WIN32)
using Native_Socket = upointer; // SOCKET
#else
using Native_Socket = int;
#endif

constexpr Native_Socket gInvalidSocket = static_cast<Native_Socket>(~static_cast<Native_Socket>(0));

bool Network_Start();

struct Socket_Address {
    u8 address[16] = {};
    u32 scope = 0;
    u16 port = 0;
    bool ipv6 = false;
};

class Socket {
public:
    Socket() = default;
    explicit Socket(Native_Socket native)
        : handle(native) {
    }
    Socket(Socket&& other) noexcept
        : handle(other.Release()) {
    }
    Socket& operator=(Socket&& other) noexcept;
    ~Socket() {
        Close();
    }

    static Socket Connect(const char* host, u16 port, u32 timeout_milliseconds);
    static Socket Listen(const char* address, u16 port);
    static Socket Udp(u16 port);
    Socket Udp_Peer() const;
    Socket Udp_Bound() const;
    Socket Accept() const;

    bool Is_Open() const {
        return handle != gInvalidSocket;
    }

    Native_Socket Native() const {
        return handle;
    }

    Native_Socket Release();

    void Close();
    void Shutdown() const;
    bool Set_Blocking(bool blocking) const;
    bool Wait(bool for_write, u32 timeout_milliseconds) const;
    s64 Send_Some(const void* data, u64 size) const;
    s64 Receive_Some(void* buffer, u64 size) const;
    s64 Send(const void* data, u64 size) const;
    s64 Receive(void* buffer, u64 size) const;
    s64 Send_To(const char* host, u16 port, const void* data, u64 size) const;
    s64 Receive_From(void* buffer, u64 size, u32& out_address, u16& out_port) const;
    s64 Send_Datagram(u32 address, u16 port, const void* data, u64 size) const;
    s64 Receive_Datagram(void* buffer, u64 size, u32& out_address, u16& out_port) const;
    s64 Send_Datagram(const Socket_Address& destination, const void* data, u64 size) const;
    s64 Receive_Datagram(void* buffer, u64 size, Socket_Address& out_address) const;
    u16 Port() const;

private:
    Native_Socket handle = gInvalidSocket;
};

struct Socket_Poll_Entry {
    Native_Socket socket;
    bool watchWrite;
    bool readable;
    bool writable;
    bool failed;
};

s32 Socket_Poll(std::span<Socket_Poll_Entry> entries, u32 timeout_milliseconds);
