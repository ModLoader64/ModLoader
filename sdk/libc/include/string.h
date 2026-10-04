#ifndef MODLOADER_LIBC_STRING_H
#define MODLOADER_LIBC_STRING_H

#include <stddef.h>
#include <strings.h>

#ifdef __cplusplus
extern "C" {
#endif

void* memcpy(void* __restrict destination, const void* __restrict source, size_t size);
void* memmove(void* destination, const void* source, size_t size);
void* memset(void* destination, int value, size_t size);
int memcmp(const void* left, const void* right, size_t size);
void* memchr(const void* memory, int value, size_t size);
void* memrchr(const void* memory, int value, size_t size);
void* memmem(const void* haystack, size_t haystack_size, const void* needle, size_t needle_size);
void* mempcpy(void* __restrict destination, const void* __restrict source, size_t size);
void* memccpy(void* __restrict destination, const void* __restrict source, int value, size_t size);

size_t strlen(const char* text);
size_t strnlen(const char* text, size_t maximum);
int strcmp(const char* left, const char* right);
int strncmp(const char* left, const char* right, size_t size);
int strcoll(const char* left, const char* right); // Byte ordering in the fixed C locale.
size_t strxfrm(char* __restrict destination, const char* __restrict source, size_t size);
char* strcpy(char* __restrict destination, const char* __restrict source);
char* strncpy(char* __restrict destination, const char* __restrict source, size_t size);
char* stpcpy(char* __restrict destination, const char* __restrict source);
char* stpncpy(char* __restrict destination, const char* __restrict source, size_t size);
size_t strlcpy(char* __restrict destination, const char* __restrict source, size_t size);
size_t strlcat(char* __restrict destination, const char* __restrict source, size_t size);
char* strcat(char* __restrict destination, const char* __restrict source);
char* strncat(char* __restrict destination, const char* __restrict source, size_t size);
char* strchr(const char* text, int character);
char* strrchr(const char* text, int character);
char* strchrnul(const char* text, int character);
char* strstr(const char* haystack, const char* needle);
char* strpbrk(const char* text, const char* accept);
size_t strspn(const char* text, const char* accept);
size_t strcspn(const char* text, const char* reject);
char* strtok(char* __restrict text, const char* __restrict delimiters);
char* strtok_r(char* __restrict text, const char* __restrict delimiters, char** __restrict saved);
char* strsep(char** text, const char* delimiters);
char* strdup(const char* text);
char* strndup(const char* text, size_t size);
char* strerror(int error);
int strerror_r(int error, char* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif
