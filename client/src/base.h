#pragma once

#include "modloader_log.h"
#include <modloader/types.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using upointer = uintptr_t;

u64 Memory_Page_Size();

enum class Log_Level : u32 {
    Error = MODLOADER_LOG_ERROR,
    Warning = MODLOADER_LOG_WARNING,
    Info = MODLOADER_LOG_INFO,
    Debug = MODLOADER_LOG_DEBUG,
};

void Log_Set_Level(Log_Level level);
Log_Level Log_Get_Level();
void Log_Write(Log_Level level, const char* source, const char* format, ...) __attribute__((format(printf, 3, 4)));
void Log_Write_List(Log_Level level, const char* source, const char* format, va_list arguments);
void Log_Error(const char* source, const char* format, ...) __attribute__((format(printf, 2, 3)));
void Log_Warning(const char* source, const char* format, ...) __attribute__((format(printf, 2, 3)));
void Log_Info(const char* source, const char* format, ...) __attribute__((format(printf, 2, 3)));

std::string Text_Format(const char* format, ...) __attribute__((format(printf, 1, 2)));
std::string Text_Format_List(const char* format, va_list arguments);
void Text_Copy(std::span<char> destination, std::string_view text);
bool Text_Is_Valid(std::string_view text, usize maximum);
std::optional<u64> Text_Parse_U64(std::string_view text);

s64 File_Seek(FILE* file, s64 offset, s32 origin); // New position, or -1 on failure
std::optional<std::vector<u8>> File_Read(const std::string& path);
bool File_Write(const std::string& path, std::span<const u8> data);
bool File_Write_Text(const std::string& path, std::string_view text);
bool File_Replace(const std::string& source, const std::string& destination);
bool File_Exists(const std::string& path);
bool Directory_Exists(const std::string& path);
bool Directory_Create_All(const std::string& path);
std::vector<std::string> Directory_List(const std::string& path);

class Temporary_Directory {
public:
    Temporary_Directory() = default;
    Temporary_Directory(const Temporary_Directory&) = delete;
    Temporary_Directory& operator=(const Temporary_Directory&) = delete;
    ~Temporary_Directory();

    bool Create(const std::string& directory);
    const std::string& Path() const {
        return path;
    }

private:
    std::string path;
};

std::string Path_Join(std::string_view left, std::string_view right);
std::string Path_Directory(std::string_view path);
const char* Path_File_Name(const char* path);
std::optional<std::string> Path_Canonical(const std::string& path);
std::string Path_Executable_Directory();

u64 Time_Monotonic_Microseconds();
u64 Time_Monotonic_Milliseconds();
u64 Time_Wall_Microseconds();
void Time_Sleep_Milliseconds(u32 milliseconds);

class Library {
public:
    Library() = default;
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    ~Library();

    bool Open(const std::string& path, std::string& error);
    void Close();
    void* Symbol(const char* name) const;

private:
    void* handle = nullptr;
};

const char* Library_Prefix();
const char* Library_Extension();

class Thread {
public:
    Thread() = default;
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    ~Thread() {
        Join();
    }

    bool Start(std::function<void()> function);
    void Join();

private:
    std::function<void()> function;
    void* handle = nullptr;
    u64 posixHandle = 0;
    bool started = false;
};

u64 Thread_Current_Id();
u32 Thread_Cpu_Count();

s32 Process_Run(const std::string& executable, std::span<const std::string> arguments, const std::string& output = {});
u32 Process_Current_Id();

class Process {
public:
    Process() = default;
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    ~Process() {
        Release();
    }

    bool Spawn(const std::string& executable, std::span<const std::string> arguments, const std::string& output = {});
    std::optional<s32> Poll();
    void Release();
    bool Is_Watched() const {
        return running;
    }

private:
    void* handle = nullptr;
    s64 id = 0;
    bool running = false;
};
