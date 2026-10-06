#include "base.h"

#include <errno.h>
#include <string.h>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

Library::~Library() {
    Close();
}

bool Library::Open(const std::string& path, std::string& error) {
    Close();
    error.clear();
#if defined(_WIN32)
    DWORD capacity = GetFullPathNameA(path.c_str(), 0, nullptr, nullptr);
    if (capacity == 0) {
        error = std::system_category().message(GetLastError());
        return false;
    }

    std::vector<char> absolute(capacity);
    DWORD length = GetFullPathNameA(path.c_str(), capacity, absolute.data(), nullptr);
    if (length == 0 || length >= capacity) {
        error = std::system_category().message(length == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER);
        return false;
    }

    // Adapter dependencies live beside the adapter DLL
    handle = LoadLibraryExA(absolute.data(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (handle == nullptr) {
        error = std::system_category().message(GetLastError());
    }
#else
    handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        error = dlerror();
    }
#endif
    return handle != nullptr;
}

void Library::Close() {
    if (handle == nullptr) {
        return;
    }

#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
    handle = nullptr;
}

void* Library::Symbol(const char* name) const {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

const char* Library_Prefix() {
#if defined(_WIN32)
    return "";
#else
    return "lib";
#endif
}

const char* Library_Extension() {
#if defined(_WIN32)
    return ".dll";
#else
    return ".so";
#endif
}

static constexpr usize gThreadStackSize = 8 * 1024 * 1024;

bool Thread::Start(std::function<void()> body) {
    Join();
    function = std::move(body);
#if defined(_WIN32)
    handle = CreateThread(
        nullptr,
        gThreadStackSize,
        [](LPVOID parameter) -> DWORD {
            static_cast<Thread*>(parameter)->function();
            return 0;
        },
        this,
        0,
        nullptr
    );
    started = handle != nullptr;
#else
    pthread_attr_t attributes;
    pthread_t thread = {};

    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, gThreadStackSize);
    started = pthread_create(
                  &thread,
                  &attributes,
                  [](void* parameter) -> void* {
                      static_cast<Thread*>(parameter)->function();
                      return nullptr;
                  },
                  this
            ) == 0;
    pthread_attr_destroy(&attributes);
    static_assert(sizeof(thread) <= sizeof(posixHandle));
    memcpy(&posixHandle, &thread, sizeof(thread));
#endif
    return started;
}

void Thread::Join() {
    if (!started) {
        return;
    }

#if defined(_WIN32)
    WaitForSingleObject(handle, INFINITE);
    CloseHandle(handle);
    handle = nullptr;
#else
    pthread_t thread;
    memcpy(&thread, &posixHandle, sizeof(thread));
    pthread_join(thread, nullptr);
#endif
    started = false;
}

u64 Thread_Current_Id() {
#if defined(_WIN32)
    return GetCurrentThreadId();
#else
    pthread_t thread = pthread_self();
    u64 id = 0;
    static_assert(sizeof(thread) <= sizeof(id));
    memcpy(&id, &thread, sizeof(thread));
    return id;
#endif
}

u32 Thread_Cpu_Count() {
#if defined(_WIN32)
    DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
#else
    long count = sysconf(_SC_NPROCESSORS_ONLN);
#endif

    return count > 0 ? static_cast<u32>(count) : 1;
}

#if defined(_WIN32)
// CRT parsing requires doubled backslashes before quotes
static void Process_Append_Argument(std::string& line, const std::string& argument) {
    usize backslashes = 0;

    line += '"';
    for (char character : argument) {
        if (character == '\\') {
            backslashes++;
            continue;
        }

        if (character == '"') {
            line.append(backslashes * 2 + 1, '\\');
        }
        else {
            line.append(backslashes, '\\');
        }
        
        line += character;
        backslashes = 0;
    }
    line.append(backslashes * 2, '\\');
    line += '"';
}
#endif

bool Process::Spawn(const std::string& executable, std::span<const std::string> arguments, const std::string& output) {
    Release();
#if defined(_WIN32)
    STARTUPINFOA startup = {};
    PROCESS_INFORMATION information = {};
    std::string line;

    Process_Append_Argument(line, executable);
    for (const std::string& argument : arguments) {
        line += ' ';
        Process_Append_Argument(line, argument);
    }

    startup.cb = sizeof(startup);
    HANDLE output_handle = INVALID_HANDLE_VALUE;
    if (!output.empty()) {
        SECURITY_ATTRIBUTES attributes = {sizeof(attributes), nullptr, TRUE};
        output_handle = CreateFileA(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output_handle == INVALID_HANDLE_VALUE) {
            Log_Error("process", "cannot open %s: %s", output.c_str(), std::system_category().message(GetLastError()).c_str());
            return false;
        }
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = output_handle;
        startup.hStdError = output_handle;
    }
    bool created = CreateProcessA(executable.c_str(), line.data(), nullptr, nullptr, !output.empty(), CREATE_NO_WINDOW, nullptr, nullptr, &startup, &information) != 0;
    DWORD error = created ? ERROR_SUCCESS : GetLastError();
    if (output_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(output_handle);
    }
    if (!created) {
        Log_Error("process", "cannot start %s: %s", executable.c_str(), std::system_category().message(error).c_str());
        return false;
    }
    CloseHandle(information.hThread);
    handle = information.hProcess;
#else
    std::vector<char*> values;
    pid_t child;

    values.reserve(arguments.size() + 2);
    values.push_back(const_cast<char*>(executable.c_str()));
    for (const std::string& argument : arguments) {
        values.push_back(const_cast<char*>(argument.c_str()));
    }

    values.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (error != 0) {
        Log_Error("process", "cannot prepare %s: %s", executable.c_str(), strerror(error));
        return false;
    }
    if (!output.empty()) {
        error = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, output.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (error == 0) {
            error = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
        }
    }
    if (error == 0) {
        error = posix_spawnp(&child, executable.c_str(), &actions, nullptr, values.data(), environ);
    }
    posix_spawn_file_actions_destroy(&actions);
    if (error != 0) {
        Log_Error("process", "cannot start %s: %s", executable.c_str(), strerror(error));
        return false;
    }
    id = child;
#endif
    running = true;
    return true;
}

std::optional<s32> Process::Poll() {
#if defined(_WIN32)
    DWORD exit_code = 1;

    if (!running || WaitForSingleObject(handle, 0) != WAIT_OBJECT_0) {
        return std::nullopt;
    }

    GetExitCodeProcess(handle, &exit_code);
    Release();
    return static_cast<s32>(exit_code);
#else
    int status = 0;
    if (!running) {
        return std::nullopt;
    }

    pid_t result = waitpid(static_cast<pid_t>(id), &status, WNOHANG);
    if (result == 0 || (result < 0 && errno == EINTR)) {
        return std::nullopt;
    }
    running = false;
    return result > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

void Process::Release() {
#if defined(_WIN32)
    if (running) {
        CloseHandle(handle);
    }

    handle = nullptr;
#endif
    running = false;
}

u32 Process_Current_Id() {
#if defined(_WIN32)
    return GetCurrentProcessId();
#else
    return static_cast<u32>(getpid());
#endif
}

s32 Process_Run(const std::string& executable, std::span<const std::string> arguments, const std::string& output) {
    Process process;
    std::optional<s32> result;

    if (!process.Spawn(executable, arguments, output)) {
        return -1;
    }

    while (!(result = process.Poll())) {
        Time_Sleep_Milliseconds(10);
    }
    
    return *result;
}
