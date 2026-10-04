#include "base.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

static Log_Level sLogLevel = Log_Level::Info;
static std::mutex sLogLock;

void Log_Set_Level(Log_Level level) {
    sLogLevel = level;
}

Log_Level Log_Get_Level() {
    return sLogLevel;
}

void Log_Write_List(Log_Level level, const char* source, const char* format, va_list arguments) {
    static const char* const level_names[] = { "error", "warning", "info", "debug" };
    FILE* stream = level <= Log_Level::Warning ? stderr : stdout;

    if (level > sLogLevel) {
        return;
    }
    
    std::lock_guard lock(sLogLock);
#if defined(_WIN32)
    s32 descriptor = _fileno(stream);
    HANDLE console = descriptor >= 0 ? reinterpret_cast<HANDLE>(_get_osfhandle(descriptor)) : INVALID_HANDLE_VALUE;
    CONSOLE_SCREEN_BUFFER_INFO previous = {};
    bool colored = level <= Log_Level::Warning && GetConsoleScreenBufferInfo(console, &previous);
    if (colored) {
        WORD foreground = FOREGROUND_RED | FOREGROUND_INTENSITY;
        if (level == Log_Level::Warning) {
            foreground |= FOREGROUND_GREEN;
        }
        WORD background = previous.wAttributes & ~(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        colored = SetConsoleTextAttribute(console, background | foreground) != 0;
    }
#else
    bool colored = level <= Log_Level::Warning && isatty(fileno(stream));
    if (colored) {
        fputs(level == Log_Level::Error ? "\x1b[31m" : "\x1b[33m", stream);
    }
#endif
    fprintf(stream, "[%s][%s]: ", level_names[static_cast<u32>(level)], source);
    vfprintf(stream, format, arguments);
#if !defined(_WIN32)
    if (colored) {
        fputs("\x1b[39m", stream);
    }
#endif
    fputc('\n', stream);
    fflush(stream);
#if defined(_WIN32)
    if (colored) {
        SetConsoleTextAttribute(console, previous.wAttributes);
    }
#endif
}

void Log_Write(Log_Level level, const char* source, const char* format, ...) {
    va_list arguments;

    va_start(arguments, format);
    Log_Write_List(level, source, format, arguments);
    va_end(arguments);
}

void Log_Error(const char* source, const char* format, ...) {
    va_list arguments;

    va_start(arguments, format);
    Log_Write_List(Log_Level::Error, source, format, arguments);
    va_end(arguments);
}

void Log_Warning(const char* source, const char* format, ...) {
    va_list arguments;

    va_start(arguments, format);
    Log_Write_List(Log_Level::Warning, source, format, arguments);
    va_end(arguments);
}

void Log_Info(const char* source, const char* format, ...) {
    va_list arguments;

    va_start(arguments, format);
    Log_Write_List(Log_Level::Info, source, format, arguments);
    va_end(arguments);
}

