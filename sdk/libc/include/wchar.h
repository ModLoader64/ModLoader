// 32-bit wide characters and UTF-8 multibyte conversions
#ifndef MODLOADER_LIBC_WCHAR_H
#define MODLOADER_LIBC_WCHAR_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MODLOADER_LIBC_MBSTATE_T
#define MODLOADER_LIBC_MBSTATE_T
typedef struct {
    unsigned int pending; // bits of a partly read UTF-8 sequence
    unsigned int remaining; // bytes still to come
} mbstate_t;
#endif

typedef unsigned int wint_t;
typedef unsigned int wctype_t;

#define WEOF ((wint_t)0xFFFFFFFFu)
#ifndef WCHAR_MIN
#define WCHAR_MIN __WCHAR_MIN__
#define WCHAR_MAX __WCHAR_MAX__
#endif

size_t wcslen(const wchar_t* text);
size_t wcsnlen(const wchar_t* text, size_t maximum);
int wcscmp(const wchar_t* left, const wchar_t* right);
int wcsncmp(const wchar_t* left, const wchar_t* right, size_t size);
wchar_t* wcscpy(wchar_t* __restrict destination, const wchar_t* __restrict source);
wchar_t* wcsncpy(wchar_t* __restrict destination, const wchar_t* __restrict source, size_t size);
wchar_t* wcscat(wchar_t* __restrict destination, const wchar_t* __restrict source);
wchar_t* wcschr(const wchar_t* text, wchar_t character);
wchar_t* wcsrchr(const wchar_t* text, wchar_t character);
wchar_t* wcsstr(const wchar_t* haystack, const wchar_t* needle);
wchar_t* wmemcpy(wchar_t* __restrict destination, const wchar_t* __restrict source, size_t size);
wchar_t* wmemmove(wchar_t* destination, const wchar_t* source, size_t size);
wchar_t* wmemset(wchar_t* destination, wchar_t value, size_t size);
int wmemcmp(const wchar_t* left, const wchar_t* right, size_t size);
wchar_t* wmemchr(const wchar_t* memory, wchar_t value, size_t size);

int mbsinit(const mbstate_t* state);
size_t mbrlen(const char* __restrict text, size_t size, mbstate_t* __restrict state);
size_t mbrtowc(wchar_t* __restrict out_character, const char* __restrict text, size_t size, mbstate_t* __restrict state);
size_t wcrtomb(char* __restrict out_text, wchar_t character, mbstate_t* __restrict state);
size_t mbsrtowcs(wchar_t* __restrict destination, const char** __restrict source, size_t size, mbstate_t* __restrict state);
size_t wcsrtombs(char* __restrict destination, const wchar_t** __restrict source, size_t size, mbstate_t* __restrict state);
wint_t btowc(int character);
int wctob(wint_t character);

long wcstol(const wchar_t* __restrict text, wchar_t** __restrict end, int base);
unsigned long wcstoul(const wchar_t* __restrict text, wchar_t** __restrict end, int base);
long long wcstoll(const wchar_t* __restrict text, wchar_t** __restrict end, int base);
unsigned long long wcstoull(const wchar_t* __restrict text, wchar_t** __restrict end, int base);
// Floating-point parsing consumes at most 511 ASCII characters; wcstold() uses double precision.
double wcstod(const wchar_t* __restrict text, wchar_t** __restrict end);
float wcstof(const wchar_t* __restrict text, wchar_t** __restrict end);
long double wcstold(const wchar_t* __restrict text, wchar_t** __restrict end);

#ifdef __cplusplus
}
#endif

#endif
