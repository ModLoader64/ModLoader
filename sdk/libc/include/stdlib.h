// exit()/abort() disable the calling module
#ifndef MODLOADER_LIBC_STDLIB_H
#define MODLOADER_LIBC_STDLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 0x7FFFFFFF
#define MB_CUR_MAX ((size_t)4)

typedef struct {
    int quot;
    int rem;
} div_t;

typedef struct {
    long quot;
    long rem;
} ldiv_t;

typedef struct {
    long long quot;
    long long rem;
} lldiv_t;

void* malloc(size_t size);
void* calloc(size_t count, size_t size);
void* realloc(void* pointer, size_t size);
void free(void* pointer);
void* aligned_alloc(size_t alignment, size_t size);
int posix_memalign(void** out_pointer, size_t alignment, size_t size);
size_t malloc_usable_size(void* pointer);

__attribute__((noreturn)) void abort(void);
__attribute__((noreturn)) void exit(int status);
__attribute__((noreturn)) void _Exit(int status);
__attribute__((noreturn)) void quick_exit(int status);
int atexit(void (*function)(void));
int at_quick_exit(void (*function)(void)); // quick_exit() skips handlers.
char* getenv(const char* name);
int system(const char* command);

int abs(int value);
long labs(long value);
long long llabs(long long value);
div_t div(int numerator, int denominator);
ldiv_t ldiv(long numerator, long denominator);
lldiv_t lldiv(long long numerator, long long denominator);

int atoi(const char* text);
long atol(const char* text);
long long atoll(const char* text);
double atof(const char* text);
long strtol(const char* __restrict text, char** __restrict end, int base);
unsigned long strtoul(const char* __restrict text, char** __restrict end, int base);
long long strtoll(const char* __restrict text, char** __restrict end, int base);
unsigned long long strtoull(const char* __restrict text, char** __restrict end, int base);
float strtof(const char* __restrict text, char** __restrict end);
double strtod(const char* __restrict text, char** __restrict end);
long double strtold(const char* __restrict text, char** __restrict end); // Parsed with double precision

void qsort(void* base, size_t count, size_t size, int (*compare)(const void*, const void*));
void qsort_r(void* base, size_t count, size_t size, int (*compare)(const void*, const void*, void*), void* context);
void* bsearch(const void* key, const void* base, size_t count, size_t size, int (*compare)(const void*, const void*));

int rand(void);
int rand_r(unsigned int* seed);
void srand(unsigned int seed);

int mblen(const char* text, size_t size);
int mbtowc(wchar_t* __restrict out_character, const char* __restrict text, size_t size);
int wctomb(char* text, wchar_t character);
size_t mbstowcs(wchar_t* __restrict destination, const char* __restrict source, size_t size);
size_t wcstombs(char* __restrict destination, const wchar_t* __restrict source, size_t size);

#ifdef __cplusplus
}
#endif

#endif
