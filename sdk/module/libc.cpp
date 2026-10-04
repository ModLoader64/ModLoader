#include <modloader/detail/identity.h>
#include <modloader/detail/libc.h>

#include <errno.h>
#include <unistd.h>

namespace {
constexpr auto gFolder = ModLoader::Detail::gModuleFolder;
constexpr auto gSource = ModLoader::Detail::gModuleName;
} // namespace

extern "C" {

__attribute__((visibility("hidden"))) FILE* fopen(const char* __restrict path, const char* __restrict mode) {
    return __modloader_fopen(gFolder.data(), path, mode);
}

__attribute__((visibility("hidden"))) FILE* freopen(const char* __restrict path, const char* __restrict mode, FILE* __restrict stream) {
    return __modloader_freopen(gFolder.data(), path, mode, stream);
}

__attribute__((visibility("hidden"))) int remove(const char* path) {
    return __modloader_remove(gFolder.data(), path);
}

__attribute__((visibility("hidden"))) int rename(const char* from, const char* to) {
    return __modloader_rename(gFolder.data(), from, to);
}

__attribute__((visibility("hidden"))) FILE* tmpfile(void) {
    return __modloader_tmpfile(gFolder.data());
}

__attribute__((visibility("hidden"))) FILE* __modloader_stdin(void) {
    static FILE* stream = __modloader_standard_stream(gSource.data(), gSource.size(), 0);
    return stream;
}

__attribute__((visibility("hidden"))) FILE* __modloader_stdout(void) {
    static FILE* stream = __modloader_standard_stream(gSource.data(), gSource.size(), 1);
    return stream;
}

__attribute__((visibility("hidden"))) FILE* __modloader_stderr(void) {
    static FILE* stream = __modloader_standard_stream(gSource.data(), gSource.size(), 2);
    return stream;
}

__attribute__((visibility("hidden"))) int vprintf(const char* __restrict format, va_list arguments) {
    return vfprintf(stdout, format, arguments);
}

__attribute__((visibility("hidden"))) int printf(const char* __restrict format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int length = vfprintf(stdout, format, arguments);
    va_end(arguments);
    return length;
}

__attribute__((visibility("hidden"))) int puts(const char* text) {
    if (fputs(text, stdout) == EOF) {
        return EOF;
    }
    return fputc('\n', stdout) == EOF ? EOF : 0;
}

__attribute__((visibility("hidden"))) int putchar(int character) {
    return fputc(character, stdout);
}

__attribute__((visibility("hidden"))) void perror(const char* prefix) {
    __modloader_perror(stderr, prefix);
}

__attribute__((visibility("hidden"))) ssize_t write(int descriptor, const void* data, size_t size) {
    if (descriptor != STDOUT_FILENO && descriptor != STDERR_FILENO) {
        errno = EBADF;
        return -1;
    }
    return static_cast<ssize_t>(fwrite(data, 1, size, descriptor == STDOUT_FILENO ? stdout : stderr));
}

__attribute__((visibility("hidden"))) int getchar(void) {
    return fgetc(stdin);
}

__attribute__((visibility("hidden"))) int vscanf(const char* __restrict format, va_list arguments) {
    return vfscanf(stdin, format, arguments);
}

__attribute__((visibility("hidden"))) int scanf(const char* __restrict format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int count = vfscanf(stdin, format, arguments);
    va_end(arguments);
    return count;
}

}
