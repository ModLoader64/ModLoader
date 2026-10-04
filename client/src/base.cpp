#include "base.h"

#include <chrono>
#include <string.h>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

u64 Memory_Page_Size() {
#if defined(_WIN32)
    SYSTEM_INFO info = {};
    GetSystemInfo(&info);
    return info.dwPageSize;
#else
    return static_cast<u64>(sysconf(_SC_PAGESIZE));
#endif
}

std::string Text_Format_List(const char* format, va_list arguments) {
    va_list copy;
    std::string text;
    s32 length;

    va_copy(copy, arguments);
    length = vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (length > 0) {
        text.resize(static_cast<usize>(length));
        vsnprintf(text.data(), text.size() + 1, format, arguments);
    }
    return text;
}

std::string Text_Format(const char* format, ...) {
    va_list arguments;
    std::string text;

    va_start(arguments, format);
    text = Text_Format_List(format, arguments);
    va_end(arguments);
    return text;
}

void Text_Copy(std::span<char> destination, std::string_view text) {
    if (destination.empty()) {
        return;
    }

    usize length = text.size() < destination.size() ? text.size() : destination.size() - 1;
    memcpy(destination.data(), text.data(), length);
    destination[length] = '\0';
}

bool Text_Is_Valid(std::string_view text, usize maximum) {
    if (text.empty() || text.size() > maximum) {
        return false;
    }
    
    for (u8 character : text) {
        if (character < 0x20 || character == 0x7F) {
            return false;
        }
    }
    return true;
}

std::optional<u64> Text_Parse_U64(std::string_view text) {
    u64 value = 0;
    u32 base = 10;
    bool has_digit = false;

    if (text.starts_with("0x") || text.starts_with("0X")) {
        base = 16;
        text.remove_prefix(2);
    }

    if (text.empty()) {
        return std::nullopt;
    }

    for (char character : text) {
        u32 digit;

        if (character == '_') {
            continue;
        }

        if (character >= '0' && character <= '9') {
            digit = static_cast<u32>(character - '0');
        }
        else if (base == 16 && character >= 'a' && character <= 'f') {
            digit = static_cast<u32>(character - 'a' + 10);
        }
        else if (base == 16 && character >= 'A' && character <= 'F') {
            digit = static_cast<u32>(character - 'A' + 10);
        }
        else {
            return std::nullopt;
        }

        if (value > (UINT64_MAX - digit) / base) {
            return std::nullopt;
        }
        
        value = value * base + digit;
        has_digit = true;
    }
    return has_digit ? std::optional(value) : std::nullopt;
}

u64 Time_Monotonic_Microseconds() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

u64 Time_Monotonic_Milliseconds() {
    return Time_Monotonic_Microseconds() / 1000;
}

u64 Time_Wall_Microseconds() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

void Time_Sleep_Milliseconds(u32 milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

