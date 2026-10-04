#ifndef MODLOADER_LIBC_STRINGS_H
#define MODLOADER_LIBC_STRINGS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int strcasecmp(const char* left, const char* right);
int strncasecmp(const char* left, const char* right, size_t size);
void bzero(void* destination, size_t size);
int bcmp(const void* left, const void* right, size_t size);
void bcopy(const void* source, void* destination, size_t size);
int ffs(int value);

#ifdef __cplusplus
}
#endif

#endif
