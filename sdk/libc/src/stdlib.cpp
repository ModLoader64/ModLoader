#include "float_text.h"
#include "internal.h"

// LLVM libc's parsers (header-only; built with LIBC_FULL_BUILD and LIBC_NAMESPACE)
#include "src/__support/str_to_integer.h"
#include "src/stdlib/qsort_util.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

namespace {

struct Exit_Handler {
    void (*plain)(void); // from atexit
    void (*withArgument)(void*); // from __cxa_atexit
    void* argument;
    Exit_Handler* next;
};

Exit_Handler* sExitHandlers;
Libc::Spin_Lock sExitLock;

int Add_Exit_Handler(void (*plain)(void), void (*with_argument)(void*), void* argument) {
    Exit_Handler* handler = static_cast<Exit_Handler*>(malloc(sizeof(Exit_Handler)));

    if (handler == nullptr) {
        return -1;
    }
    handler->plain = plain;
    handler->withArgument = with_argument;
    handler->argument = argument;
    Libc::Scoped lock(sExitLock);
    handler->next = sExitHandlers;
    sExitHandlers = handler;
    return 0;
}

template <typename T, typename Character>
T Parse_Integer(const Character* text, Character** end, int base) {
    auto result = LIBC_NAMESPACE::internal::strtointeger<T>(text, base);

    if (result.has_error()) {
        errno = result.error;
    }
    if (end != nullptr) {
        *end = const_cast<Character*>(text + result.parsed_len);
    }
    return result.value;
}

template <typename T, typename Character>
T Parse_Float(const Character* text, Character** end) {
    auto result = LIBC_NAMESPACE::internal::strtofloatingpoint<T>(text);

    if (result.has_error()) {
        errno = result.error;
    }
    if (end != nullptr) {
        *end = const_cast<Character*>(text + result.parsed_len);
    }
    return result.value;
}

thread_local unsigned int sRandomState = 1;

} // namespace

// ending

extern "C" void abort(void) {
    Libc::Fatal("abort()");
}

extern "C" void __modloader_run_atexit(void) {
    for (;;) {
        Exit_Handler* handler;

        {
            Libc::Scoped lock(sExitLock);
            handler = sExitHandlers;
            if (handler != nullptr) {
                sExitHandlers = handler->next;
            }
        }
        if (handler == nullptr) {
            fflush(nullptr);
            return;
        }
        if (handler->plain != nullptr) {
            handler->plain();
        }
        else {
            handler->withArgument(handler->argument);
        }
        free(handler);
    }
}

extern "C" void exit(int status) {
    __modloader_run_atexit();
    char message[64];
    snprintf(message, sizeof(message), "exit(%d)", status);
    Libc::Fatal(message);
}

extern "C" void _Exit(int status) {
    char message[64];
    snprintf(message, sizeof(message), "_Exit(%d)", status);
    Libc::Fatal(message);
}

extern "C" void quick_exit(int status) {
    _Exit(status);
}

extern "C" int atexit(void (*function)(void)) {
    return Add_Exit_Handler(function, nullptr, nullptr);
}

extern "C" int at_quick_exit(void (*)(void)) {
    return 0;
}

extern "C" int __cxa_atexit(void (*function)(void*), void* argument, void*) {
    return Add_Exit_Handler(nullptr, function, argument);
}

extern "C" char* getenv(const char*) {
    return nullptr;
}

extern "C" int system(const char* command) {
    return command == nullptr ? 0 : -1;
}

// arithmetic

extern "C" int abs(int value) {
    return value < 0 ? -value : value;
}

extern "C" long labs(long value) {
    return value < 0 ? -value : value;
}

extern "C" long long llabs(long long value) {
    return value < 0 ? -value : value;
}

extern "C" intmax_t imaxabs(intmax_t value) {
    return value < 0 ? -value : value;
}

extern "C" div_t div(int numerator, int denominator) {
    return { numerator / denominator, numerator % denominator };
}

extern "C" ldiv_t ldiv(long numerator, long denominator) {
    return { numerator / denominator, numerator % denominator };
}

extern "C" lldiv_t lldiv(long long numerator, long long denominator) {
    return { numerator / denominator, numerator % denominator };
}

extern "C" imaxdiv_t imaxdiv(intmax_t numerator, intmax_t denominator) {
    return { numerator / denominator, numerator % denominator };
}

// parsing

extern "C" long strtol(const char* __restrict text, char** __restrict end, int base) {
    return Parse_Integer<long>(text, end, base);
}

extern "C" unsigned long strtoul(const char* __restrict text, char** __restrict end, int base) {
    return Parse_Integer<unsigned long>(text, end, base);
}

extern "C" long long strtoll(const char* __restrict text, char** __restrict end, int base) {
    return Parse_Integer<long long>(text, end, base);
}

extern "C" unsigned long long strtoull(const char* __restrict text, char** __restrict end, int base) {
    return Parse_Integer<unsigned long long>(text, end, base);
}

extern "C" intmax_t strtoimax(const char* __restrict text, char** __restrict end, int base) {
    return Parse_Integer<intmax_t>(text, end, base);
}

extern "C" uintmax_t strtoumax(const char* __restrict text, char** __restrict end, int base) {
    return Parse_Integer<uintmax_t>(text, end, base);
}

extern "C" float strtof(const char* __restrict text, char** __restrict end) {
    return Parse_Float<float>(text, end);
}

extern "C" double strtod(const char* __restrict text, char** __restrict end) {
    return Parse_Float<double>(text, end);
}

extern "C" long double strtold(const char* __restrict text, char** __restrict end) {
    return Parse_Float<long double>(text, end);
}

extern "C" int atoi(const char* text) {
    return static_cast<int>(strtol(text, nullptr, 10));
}

extern "C" long atol(const char* text) {
    return strtol(text, nullptr, 10);
}

extern "C" long long atoll(const char* text) {
    return strtoll(text, nullptr, 10);
}

extern "C" double atof(const char* text) {
    return strtod(text, nullptr);
}

extern "C" long wcstol(const wchar_t* __restrict text, wchar_t** __restrict end, int base) {
    return Parse_Integer<long>(text, end, base);
}

extern "C" unsigned long wcstoul(const wchar_t* __restrict text, wchar_t** __restrict end, int base) {
    return Parse_Integer<unsigned long>(text, end, base);
}

extern "C" long long wcstoll(const wchar_t* __restrict text, wchar_t** __restrict end, int base) {
    return Parse_Integer<long long>(text, end, base);
}

extern "C" unsigned long long wcstoull(const wchar_t* __restrict text, wchar_t** __restrict end, int base) {
    return Parse_Integer<unsigned long long>(text, end, base);
}

extern "C" double wcstod(const wchar_t* __restrict text, wchar_t** __restrict end) {
    return Parse_Float<double>(text, end);
}

extern "C" float wcstof(const wchar_t* __restrict text, wchar_t** __restrict end) {
    return Parse_Float<float>(text, end);
}

extern "C" long double wcstold(const wchar_t* __restrict text, wchar_t** __restrict end) {
    return Parse_Float<long double>(text, end);
}

// sorting

extern "C" void qsort_r(void* base, size_t count, size_t size, int (*compare)(const void*, const void*, void*), void* context) {
    LIBC_NAMESPACE::internal::unstable_sort(base, count, size, compare, context);
}

extern "C" void qsort(void* base, size_t count, size_t size, int (*compare)(const void*, const void*)) {
    LIBC_NAMESPACE::internal::unstable_sort(base, count, size, compare);
}

extern "C" void* bsearch(const void* key, const void* base, size_t count, size_t size, int (*compare)(const void*, const void*)) {
    const unsigned char* bytes = static_cast<const unsigned char*>(base);
    size_t low = 0;
    size_t high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int order = compare(key, bytes + middle * size);

        if (order == 0) {
            return const_cast<unsigned char*>(bytes + middle * size);
        }
        if (order < 0) {
            high = middle;
        }
        else {
            low = middle + 1;
        }
    }
    return nullptr;
}

// random numbers

extern "C" int rand_r(unsigned int* seed) {
    unsigned int next = *seed * 1103515245u + 12345u;
    unsigned int result = next >> 16;
    next = next * 1103515245u + 12345u;
    result = (result << 15) ^ (next >> 16);
    *seed = next;
    return static_cast<int>(result & RAND_MAX);
}

extern "C" int rand(void) {
    return rand_r(&sRandomState);
}

extern "C" void srand(unsigned int seed) {
    sRandomState = seed;
}

// multibyte (UTF-8)

extern "C" int mblen(const char* text, size_t size) {
    if (text == nullptr) {
        return 0;
    }
    if (size == 0 || *text == '\0') {
        return 0;
    }
    uint32_t code_point;
    size_t length = Libc::Utf8_Decode(reinterpret_cast<const unsigned char*>(text), size, &code_point);
    return length != 0 ? static_cast<int>(length) : -1;
}

extern "C" int mbtowc(wchar_t* __restrict out_character, const char* __restrict text, size_t size) {
    if (text == nullptr) {
        return 0;
    }
    if (size == 0) {
        return -1;
    }
    
    uint32_t code_point;
    size_t length = Libc::Utf8_Decode(reinterpret_cast<const unsigned char*>(text), size, &code_point);
    if (length == 0) {
        errno = EILSEQ;
        return -1;
    }
    if (out_character != nullptr) {
        *out_character = static_cast<wchar_t>(code_point);
    }
    return code_point == 0 ? 0 : static_cast<int>(length);
}

extern "C" int wctomb(char* text, wchar_t character) {
    if (text == nullptr) {
        return 0;
    }
    size_t length = Libc::Utf8_Encode(static_cast<uint32_t>(character), text);
    if (length == 0) {
        errno = EILSEQ;
        return -1;
    }
    return static_cast<int>(length);
}

extern "C" size_t mbstowcs(wchar_t* __restrict destination, const char* __restrict source, size_t size) {
    size_t count = 0;
    const unsigned char* text = reinterpret_cast<const unsigned char*>(source);
    while (destination == nullptr || count < size) {
        uint32_t code_point;
        size_t length = Libc::Utf8_Decode(text, 4, &code_point);

        if (length == 0) {
            errno = EILSEQ;
            return static_cast<size_t>(-1);
        }
        if (destination != nullptr) {
            destination[count] = static_cast<wchar_t>(code_point);
        }
        if (code_point == 0) {
            return count;
        }
        count++;
        text += length;
    }
    return count;
}

extern "C" size_t wcstombs(char* __restrict destination, const wchar_t* __restrict source, size_t size) {
    size_t written = 0;
    for (;; source++) {
        char encoded[4];
        size_t length = Libc::Utf8_Encode(static_cast<uint32_t>(*source), encoded);

        if (length == 0) {
            errno = EILSEQ;
            return static_cast<size_t>(-1);
        }
        if (*source == 0) {
            if (destination != nullptr && written < size) {
                destination[written] = '\0';
            }
            return written;
        }
        if (destination != nullptr) {
            if (written + length > size) {
                return written;
            }
            memcpy(destination + written, encoded, length);
        }
        written += length;
    }
}
