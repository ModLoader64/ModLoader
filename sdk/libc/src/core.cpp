#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <fenv.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// errno and fatal

namespace {

thread_local int sErrno;

} // namespace

extern "C" int* __errno_location(void) {
    return &sErrno;
}

void Libc::Fatal(const char* message) {
    ModLoader_Host_Abort_With(message, strlen(message));
    __builtin_trap();
}

void Libc::Spin_Lock::Lock() {
    uint32_t expected = Mutex_State::Unlocked;

    if (__atomic_compare_exchange_n(&state, &expected, Mutex_State::Locked, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return;
    }

    if (expected != Mutex_State::Contended) {
        expected = __atomic_exchange_n(&state, Mutex_State::Contended, __ATOMIC_ACQUIRE);
    }

    while (expected != Mutex_State::Unlocked) {
        __builtin_wasm_memory_atomic_wait32(reinterpret_cast<int*>(&state), Mutex_State::Contended, -1);
        expected = __atomic_exchange_n(&state, Mutex_State::Contended, __ATOMIC_ACQUIRE);
    }
}

void Libc::Spin_Lock::Unlock() {
    if (__atomic_exchange_n(&state, Mutex_State::Unlocked, __ATOMIC_RELEASE) == Mutex_State::Contended) {
        __builtin_wasm_memory_atomic_notify(reinterpret_cast<int*>(&state), 1);
    }
}

extern "C" void __assert_fail(const char* expression, const char* file, unsigned int line, const char* function) {
    char message[512];
    snprintf(message, sizeof(message), "assertion failed: %s (%s:%u, %s)", expression, file, line, function);
    Libc::Fatal(message);
}

// ctype

extern "C" int isdigit(int character) {
    return character >= '0' && character <= '9';
}

extern "C" int islower(int character) {
    return character >= 'a' && character <= 'z';
}

extern "C" int isupper(int character) {
    return character >= 'A' && character <= 'Z';
}

extern "C" int isalpha(int character) {
    return islower(character) || isupper(character);
}

extern "C" int isalnum(int character) {
    return isalpha(character) || isdigit(character);
}

extern "C" int isblank(int character) {
    return character == ' ' || character == '\t';
}

extern "C" int iscntrl(int character) {
    return (character >= 0 && character < 0x20) || character == 0x7F;
}

extern "C" int isgraph(int character) {
    return character > 0x20 && character < 0x7F;
}

extern "C" int isprint(int character) {
    return character >= 0x20 && character < 0x7F;
}

extern "C" int ispunct(int character) {
    return isgraph(character) && !isalnum(character);
}

extern "C" int isspace(int character) {
    return character == ' ' || (character >= '\t' && character <= '\r');
}

extern "C" int isxdigit(int character) {
    return isdigit(character) || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
}

extern "C" int isascii(int character) {
    return character >= 0 && character < 0x80;
}

extern "C" int tolower(int character) {
    return isupper(character) ? character - 'A' + 'a' : character;
}

extern "C" int toupper(int character) {
    return islower(character) ? character - 'a' + 'A' : character;
}

extern "C" int toascii(int character) {
    return character & 0x7F;
}

// UTF-8

namespace {
constexpr uint32_t gMinimumCodePoint[5] = { 0, 0, 0x80, 0x800, 0x10000 };
} // namespace

size_t Libc::Utf8_Decode(const unsigned char* text, size_t size, uint32_t* out_code_point) {
    uint32_t lead;
    size_t length;
    uint32_t code_point;

    if (size == 0) {
        return 0;
    }

    lead = text[0];
    if (lead < 0x80) {
        *out_code_point = lead;
        return 1;
    }

    if ((lead & 0xE0) == 0xC0) {
        length = 2;
        code_point = lead & 0x1F;
    }
    else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        code_point = lead & 0x0F;
    }
    else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        code_point = lead & 0x07;
    }
    else {
        return 0;
    }

    if (size < length) {
        return 0;
    }

    for (size_t index = 1; index < length; index++) {
        if ((text[index] & 0xC0) != 0x80) {
            return 0;
        }
        code_point = (code_point << 6) | (text[index] & 0x3F);
    }

    if (code_point < gMinimumCodePoint[length] || code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF)) {
        return 0;
    }

    *out_code_point = code_point;
    return length;
}

size_t Libc::Utf8_Encode(uint32_t code_point, char* out_text) {
    if (code_point < 0x80) {
        out_text[0] = static_cast<char>(code_point);
        return 1;
    }
    
    if (code_point < 0x800) {
        out_text[0] = static_cast<char>(0xC0 | (code_point >> 6));
        out_text[1] = static_cast<char>(0x80 | (code_point & 0x3F));
        return 2;
    }

    if (code_point < 0x10000) {
        if (code_point >= 0xD800 && code_point <= 0xDFFF) {
            return 0;
        }
        out_text[0] = static_cast<char>(0xE0 | (code_point >> 12));
        out_text[1] = static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out_text[2] = static_cast<char>(0x80 | (code_point & 0x3F));
        return 3;
    }

    if (code_point <= 0x10FFFF) {
        out_text[0] = static_cast<char>(0xF0 | (code_point >> 18));
        out_text[1] = static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
        out_text[2] = static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out_text[3] = static_cast<char>(0x80 | (code_point & 0x3F));
        return 4;
    }

    return 0;
}

// fenv

extern "C" int feclearexcept(int) {
    return 0;
}

extern "C" int fegetexceptflag(fexcept_t* out_flags, int) {
    *out_flags = 0;
    return 0;
}

extern "C" int feraiseexcept(int) {
    return 0;
}

extern "C" int fesetexceptflag(const fexcept_t*, int) {
    return 0;
}

extern "C" int fetestexcept(int) {
    return 0;
}

extern "C" int fegetround(void) {
    return FE_TONEAREST;
}

extern "C" int fesetround(int mode) {
    return mode == FE_TONEAREST ? 0 : 1;
}

extern "C" int fegetenv(fenv_t* out_environment) {
    *out_environment = 0;
    return 0;
}

extern "C" int feholdexcept(fenv_t* out_environment) {
    *out_environment = 0;
    return 0;
}

extern "C" int fesetenv(const fenv_t*) {
    return 0;
}

extern "C" int feupdateenv(const fenv_t*) {
    return 0;
}

// locale

namespace {

char sDot[] = ".";
char sEmpty[] = "";
char sC[] = "C";

lconv sLconv = {
    sDot, sEmpty, sEmpty, sEmpty, sEmpty, sEmpty, sEmpty, sEmpty, sEmpty, sEmpty, 127, 127,
    127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127
};

} // namespace

extern "C" char* setlocale(int, const char* locale) {
    if (locale == nullptr || locale[0] == '\0' || strcmp(locale, "C") == 0 || strcmp(locale, "POSIX") == 0 || strcmp(locale, "C.UTF-8") == 0) {
        return sC;
    }
    return nullptr;
}

extern "C" lconv* localeconv(void) {
    return &sLconv;
}

// signal

extern "C" sighandler_t signal(int, sighandler_t) {
    return SIG_DFL;
}

extern "C" int raise(int signal_number) {
    if (signal_number == SIGABRT) {
        Libc::Fatal("raise(SIGABRT)");
    }
    return 0;
}

// unistd

extern "C" ssize_t write(int descriptor, const void* data, size_t size) {
    if (descriptor != STDOUT_FILENO && descriptor != STDERR_FILENO) {
        errno = EBADF;
        return -1;
    }
    return static_cast<ssize_t>(fwrite(data, 1, size, descriptor == STDOUT_FILENO ? stdout : stderr));
}

extern "C" ssize_t read(int, void*, size_t) {
    errno = EBADF;
    return -1;
}

extern "C" int close(int) {
    errno = EBADF;
    return -1;
}

extern "C" int isatty(int) {
    return 0;
}

extern "C" unsigned int sleep(unsigned int seconds) {
    ModLoader_Host_Thread_Sleep(seconds * 1000);
    return 0;
}

extern "C" int usleep(useconds_t microseconds) {
    ModLoader_Host_Thread_Sleep(static_cast<uint32_t>(microseconds / 1000));
    return 0;
}

extern "C" pid_t getpid(void) {
    return 1;
}

extern "C" long sysconf(int name) {
    switch (name) {
    case _SC_PAGESIZE:
        return 65536;
    case _SC_NPROCESSORS_CONF:
    case _SC_NPROCESSORS_ONLN:
        return static_cast<long>(ModLoader_Host_Cpu_Count());
    default:
        errno = EINVAL;
        return -1;
    }
}

extern "C" int getpagesize(void) {
    return 65536;
}
