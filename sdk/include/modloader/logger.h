#pragma once

#include <modloader/types.h>
#include <modloader/detail/identity.h>
#include <modloader_log.h>

namespace ModLoader::Logger {

// printf-style logging, tagged with the calling module's name; messages are truncated to 1023 bytes
enum class Level : u32 {
    Error = MODLOADER_LOG_ERROR,
    Warning = MODLOADER_LOG_WARNING,
    Info = MODLOADER_LOG_INFO,
    Debug = MODLOADER_LOG_DEBUG,
};

namespace Detail {
void Write_List(Level level, const char* source, u64 source_length, const char* format, __builtin_va_list arguments);
} // namespace Detail

static inline void Write_List(Level level, const char* format, __builtin_va_list arguments) {
    constexpr auto source = ModLoader::Detail::gModuleName;
    Detail::Write_List(level, source.data(), source.size(), format, arguments);
}

__attribute__((format(printf, 2, 3)))
static inline void Write(Level level, const char* format, ...) {
    __builtin_va_list arguments;
    __builtin_va_start(arguments, format);
    Write_List(level, format, arguments);
    __builtin_va_end(arguments);
}

__attribute__((format(printf, 1, 2)))
static inline void Error(const char* format, ...) {
    __builtin_va_list arguments;
    __builtin_va_start(arguments, format);
    Write_List(Level::Error, format, arguments);
    __builtin_va_end(arguments);
}

__attribute__((format(printf, 1, 2)))
static inline void Warning(const char* format, ...) {
    __builtin_va_list arguments;
    __builtin_va_start(arguments, format);
    Write_List(Level::Warning, format, arguments);
    __builtin_va_end(arguments);
}

__attribute__((format(printf, 1, 2)))
static inline void Info(const char* format, ...) {
    __builtin_va_list arguments;
    __builtin_va_start(arguments, format);
    Write_List(Level::Info, format, arguments);
    __builtin_va_end(arguments);
}

__attribute__((format(printf, 1, 2)))
static inline void Debug(const char* format, ...) {
    __builtin_va_list arguments;
    __builtin_va_start(arguments, format);
    Write_List(Level::Debug, format, arguments);
    __builtin_va_end(arguments);
}

} // namespace ModLoader::Logger
