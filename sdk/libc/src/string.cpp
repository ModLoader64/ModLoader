#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern "C" void* memcpy(void* __restrict destination, const void* __restrict source, size_t size) {
    return __builtin_memcpy(destination, source, size);
}

extern "C" void* memmove(void* destination, const void* source, size_t size) {
    return __builtin_memmove(destination, source, size);
}

extern "C" void* memset(void* destination, int value, size_t size) {
    return __builtin_memset(destination, value, size);
}

extern "C" int memcmp(const void* left, const void* right, size_t size) {
    const unsigned char* left_bytes = static_cast<const unsigned char*>(left);
    const unsigned char* right_bytes = static_cast<const unsigned char*>(right);
    for (size_t index = 0; index < size; index++) {
        if (left_bytes[index] != right_bytes[index]) {
            return left_bytes[index] < right_bytes[index] ? -1 : 1;
        }
    }
    return 0;
}

extern "C" void* memchr(const void* memory, int value, size_t size) {
    const unsigned char* bytes = static_cast<const unsigned char*>(memory);
    for (size_t index = 0; index < size; index++) {
        if (bytes[index] == static_cast<unsigned char>(value)) {
            return const_cast<unsigned char*>(bytes + index);
        }
    }
    return nullptr;
}

extern "C" void* memrchr(const void* memory, int value, size_t size) {
    const unsigned char* bytes = static_cast<const unsigned char*>(memory);
    while (size > 0) {
        size--;
        if (bytes[size] == static_cast<unsigned char>(value)) {
            return const_cast<unsigned char*>(bytes + size);
        }
    }
    return nullptr;
}

extern "C" void* memmem(const void* haystack, size_t haystack_size, const void* needle, size_t needle_size) {
    const unsigned char* bytes = static_cast<const unsigned char*>(haystack);
    if (needle_size == 0) {
        return const_cast<void*>(haystack);
    }
    for (size_t index = 0; index + needle_size <= haystack_size; index++) {
        if (memcmp(bytes + index, needle, needle_size) == 0) {
            return const_cast<unsigned char*>(bytes + index);
        }
    }
    return nullptr;
}

extern "C" void* mempcpy(void* __restrict destination, const void* __restrict source, size_t size) {
    return static_cast<char*>(memcpy(destination, source, size)) + size;
}

extern "C" void* memccpy(void* __restrict destination, const void* __restrict source, int value, size_t size) {
    unsigned char* out = static_cast<unsigned char*>(destination);
    const unsigned char* in = static_cast<const unsigned char*>(source);
    for (size_t index = 0; index < size; index++) {
        out[index] = in[index];
        if (in[index] == static_cast<unsigned char>(value)) {
            return out + index + 1;
        }
    }
    return nullptr;
}

extern "C" size_t strlen(const char* text) {
    size_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

extern "C" size_t strnlen(const char* text, size_t maximum) {
    size_t length = 0;
    while (length < maximum && text[length] != '\0') {
        length++;
    }
    return length;
}

extern "C" int strcmp(const char* left, const char* right) {
    size_t index = 0;
    while (left[index] != '\0' && left[index] == right[index]) {
        index++;
    }
    return static_cast<unsigned char>(left[index]) - static_cast<unsigned char>(right[index]);
}

extern "C" int strncmp(const char* left, const char* right, size_t size) {
    for (size_t index = 0; index < size; index++) {
        if (left[index] != right[index] || left[index] == '\0') {
            return static_cast<unsigned char>(left[index]) - static_cast<unsigned char>(right[index]);
        }
    }
    return 0;
}

extern "C" int strcoll(const char* left, const char* right) {
    return strcmp(left, right);
}

extern "C" size_t strxfrm(char* __restrict destination, const char* __restrict source, size_t size) {
    size_t length = strlen(source);
    if (length < size) {
        memcpy(destination, source, length + 1);
    }
    return length;
}

extern "C" char* strcpy(char* __restrict destination, const char* __restrict source) {
    return static_cast<char*>(memcpy(destination, source, strlen(source) + 1));
}

extern "C" char* strncpy(char* __restrict destination, const char* __restrict source, size_t size) {
    stpncpy(destination, source, size);
    return destination;
}

extern "C" char* stpcpy(char* __restrict destination, const char* __restrict source) {
    size_t length = strlen(source);
    memcpy(destination, source, length + 1);
    return destination + length;
}

extern "C" char* stpncpy(char* __restrict destination, const char* __restrict source, size_t size) {
    size_t length = strnlen(source, size);
    memcpy(destination, source, length);
    memset(destination + length, 0, size - length);
    return destination + length;
}

extern "C" size_t strlcpy(char* __restrict destination, const char* __restrict source, size_t size) {
    size_t length = strlen(source);
    if (size != 0) {
        size_t copied = length < size - 1 ? length : size - 1;

        memcpy(destination, source, copied);
        destination[copied] = '\0';
    }
    return length;
}

extern "C" size_t strlcat(char* __restrict destination, const char* __restrict source, size_t size) {
    size_t existing = strnlen(destination, size);
    if (existing == size) {
        return size + strlen(source);
    }
    return existing + strlcpy(destination + existing, source, size - existing);
}

extern "C" char* strcat(char* __restrict destination, const char* __restrict source) {
    strcpy(destination + strlen(destination), source);
    return destination;
}

extern "C" char* strncat(char* __restrict destination, const char* __restrict source, size_t size) {
    char* end = destination + strlen(destination);
    size_t length = strnlen(source, size);
    memcpy(end, source, length);
    end[length] = '\0';
    return destination;
}

extern "C" char* strchr(const char* text, int character) {
    for (;; text++) {
        if (*text == static_cast<char>(character)) {
            return const_cast<char*>(text);
        }
        if (*text == '\0') {
            return nullptr;
        }
    }
}

extern "C" char* strchrnul(const char* text, int character) {
    while (*text != '\0' && *text != static_cast<char>(character)) {
        text++;
    }
    return const_cast<char*>(text);
}

extern "C" char* strrchr(const char* text, int character) {
    const char* found = nullptr;
    for (;; text++) {
        if (*text == static_cast<char>(character)) {
            found = text;
        }
        if (*text == '\0') {
            return const_cast<char*>(found);
        }
    }
}

extern "C" char* strstr(const char* haystack, const char* needle) {
    size_t needle_length = strlen(needle);
    if (needle_length == 0) {
        return const_cast<char*>(haystack);
    }
    for (; *haystack != '\0'; haystack++) {
        if (*haystack == *needle && strncmp(haystack, needle, needle_length) == 0) {
            return const_cast<char*>(haystack);
        }
    }
    return nullptr;
}

extern "C" size_t strspn(const char* text, const char* accept) {
    size_t length = 0;
    while (text[length] != '\0' && strchr(accept, text[length]) != nullptr) {
        length++;
    }
    return length;
}

extern "C" size_t strcspn(const char* text, const char* reject) {
    size_t length = 0;
    while (text[length] != '\0' && strchr(reject, text[length]) == nullptr) {
        length++;
    }
    return length;
}

extern "C" char* strpbrk(const char* text, const char* accept) {
    text += strcspn(text, accept);
    return *text != '\0' ? const_cast<char*>(text) : nullptr;
}

extern "C" char* strtok_r(char* __restrict text, const char* __restrict delimiters, char** __restrict saved) {
    if (text == nullptr) {
        text = *saved;
    }
    if (text == nullptr) {
        return nullptr;
    }
    text += strspn(text, delimiters);
    if (*text == '\0') {
        *saved = nullptr;
        return nullptr;
    }
    char* end = text + strcspn(text, delimiters);
    if (*end != '\0') {
        *end = '\0';
        *saved = end + 1;
    }
    else {
        *saved = nullptr;
    }
    return text;
}

extern "C" char* strtok(char* __restrict text, const char* __restrict delimiters) {
    static thread_local char* sSaved;
    return strtok_r(text, delimiters, &sSaved);
}

extern "C" char* strsep(char** text, const char* delimiters) {
    char* start = *text;
    if (start == nullptr) {
        return nullptr;
    }
    char* end = start + strcspn(start, delimiters);
    if (*end != '\0') {
        *end = '\0';
        *text = end + 1;
    }
    else {
        *text = nullptr;
    }
    return start;
}

extern "C" char* strdup(const char* text) {
    size_t length = strlen(text) + 1;
    char* copy = static_cast<char*>(malloc(length));
    return copy != nullptr ? static_cast<char*>(memcpy(copy, text, length)) : nullptr;
}

extern "C" char* strndup(const char* text, size_t size) {
    size_t length = strnlen(text, size);
    char* copy = static_cast<char*>(malloc(length + 1));
    if (copy != nullptr) {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

extern "C" char* strerror(int error) {
    switch (error) {
    case 0:
        return const_cast<char*>("Success");
    case EPERM:
        return const_cast<char*>("Operation not permitted");
    case ENOENT:
        return const_cast<char*>("No such file or directory");
    case EINTR:
        return const_cast<char*>("Interrupted");
    case EIO:
        return const_cast<char*>("I/O error");
    case EBADF:
        return const_cast<char*>("Bad file descriptor");
    case EAGAIN:
        return const_cast<char*>("Resource temporarily unavailable");
    case ENOMEM:
        return const_cast<char*>("Out of memory");
    case EACCES:
        return const_cast<char*>("Permission denied");
    case EEXIST:
        return const_cast<char*>("File exists");
    case EINVAL:
        return const_cast<char*>("Invalid argument");
    case ENOSPC:
        return const_cast<char*>("No space left on device");
    case EDOM:
        return const_cast<char*>("Argument out of domain");
    case ERANGE:
        return const_cast<char*>("Result out of range");
    case ENOSYS:
        return const_cast<char*>("Function not implemented");
    case EILSEQ:
        return const_cast<char*>("Illegal byte sequence");
    case ETIMEDOUT:
        return const_cast<char*>("Timed out");
    case ECONNREFUSED:
        return const_cast<char*>("Connection refused");
    case ECONNRESET:
        return const_cast<char*>("Connection reset");
    default:
        return const_cast<char*>("Unknown error");
    }
}

extern "C" int strerror_r(int error, char* buffer, size_t size) {
    return strlcpy(buffer, strerror(error), size) < size ? 0 : ERANGE;
}

extern "C" int strcasecmp(const char* left, const char* right) {
    size_t index = 0;
    while (left[index] != '\0' && tolower(left[index]) == tolower(right[index])) {
        index++;
    }
    return tolower(static_cast<unsigned char>(left[index])) - tolower(static_cast<unsigned char>(right[index]));
}

extern "C" int strncasecmp(const char* left, const char* right, size_t size) {
    for (size_t index = 0; index < size; index++) {
        int left_lower = tolower(static_cast<unsigned char>(left[index]));
        int right_lower = tolower(static_cast<unsigned char>(right[index]));

        if (left_lower != right_lower || left[index] == '\0') {
            return left_lower - right_lower;
        }
    }
    return 0;
}

extern "C" void bzero(void* destination, size_t size) {
    memset(destination, 0, size);
}

extern "C" int bcmp(const void* left, const void* right, size_t size) {
    return memcmp(left, right, size);
}

extern "C" void bcopy(const void* source, void* destination, size_t size) {
    memmove(destination, source, size);
}

extern "C" int ffs(int value) {
    return value == 0 ? 0 : __builtin_ctz(static_cast<unsigned int>(value)) + 1;
}
