#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <uchar.h>
#include <wchar.h>
#include <wctype.h>

namespace {

constexpr size_t gInvalid = static_cast<size_t>(-1);
constexpr size_t gIncomplete = static_cast<size_t>(-2);

constexpr struct {
    const char* name;
    int (*test)(wint_t);
} gCharacterClasses[] = {
    { "alnum", iswalnum }, { "alpha", iswalpha }, { "blank", iswblank }, { "cntrl", iswcntrl },
    { "digit", iswdigit }, { "graph", iswgraph }, { "lower", iswlower }, { "print", iswprint },
    { "punct", iswpunct }, { "space", iswspace }, { "upper", iswupper }, { "xdigit", iswxdigit },
};
constexpr wctype_t gCharacterClassCount = sizeof(gCharacterClasses) / sizeof(gCharacterClasses[0]);

size_t Decode(uint32_t* out_code_point, const char* text, size_t size, mbstate_t* state) {
    static thread_local mbstate_t sState;
    const unsigned char* bytes;
    size_t used = 0;
    uint32_t code_point;
    uint32_t remaining;

    if (state == nullptr) {
        state = &sState;
    }

    if (text == nullptr) {
        state->pending = 0;
        state->remaining = 0;
        return 0;
    }

    if (size == 0) {
        return gIncomplete;
    }

    bytes = reinterpret_cast<const unsigned char*>(text);
    code_point = state->pending;
    remaining = state->remaining;
    if (remaining == 0) {
        unsigned char lead = bytes[used++];

        if (lead < 0x80) {
            *out_code_point = lead;
            return lead == 0 ? 0 : 1;
        }
        if (lead >= 0xC2 && lead <= 0xDF) {
            code_point = lead & 0x1F;
            remaining = 1;
        }
        else if ((lead & 0xF0) == 0xE0) {
            code_point = lead & 0x0F;
            remaining = 2;
        }
        else if ((lead & 0xF8) == 0xF0) {
            code_point = lead & 0x07;
            remaining = 3;
        }
        else {
            errno = EILSEQ;
            return gInvalid;
        }
    }

    while (remaining > 0) {
        unsigned char next;

        if (used == size) {
            state->pending = code_point;
            state->remaining = remaining;
            return gIncomplete;
        }
        next = bytes[used++];
        if ((next & 0xC0) != 0x80 ||
            (remaining == 2 && code_point == 0 && next < 0xA0) ||
            (remaining == 3 && code_point == 0 && next < 0x90)) {
            state->pending = 0;
            state->remaining = 0;
            errno = EILSEQ;
            return gInvalid;
        }
        code_point = (code_point << 6) | (next & 0x3F);
        remaining--;
    }

    state->pending = 0;
    state->remaining = 0;
    if (code_point > 0x10FFFF || (code_point >= 0xD800 && code_point <= 0xDFFF)) {
        errno = EILSEQ;
        return gInvalid;
    }
    *out_code_point = code_point;
    return code_point == 0 ? 0 : used;
}

} // namespace

extern "C" {

size_t wcslen(const wchar_t* text) {
    size_t length = 0;

    while (text[length] != 0) {
        length++;
    }
    return length;
}

size_t wcsnlen(const wchar_t* text, size_t maximum) {
    size_t length = 0;

    while (length < maximum && text[length] != 0) {
        length++;
    }
    return length;
}

int wcscmp(const wchar_t* left, const wchar_t* right) {
    while (*left != 0 && *left == *right) {
        left++;
        right++;
    }
    return *left < *right ? -1 : (*left > *right ? 1 : 0);
}

int wcsncmp(const wchar_t* left, const wchar_t* right, size_t size) {
    for (size_t index = 0; index < size; index++) {
        if (left[index] != right[index] || left[index] == 0) {
            return left[index] < right[index] ? -1 : (left[index] > right[index] ? 1 : 0);
        }
    }
    return 0;
}

wchar_t* wcscpy(wchar_t* __restrict destination, const wchar_t* __restrict source) {
    return static_cast<wchar_t*>(memcpy(destination, source, (wcslen(source) + 1) * sizeof(wchar_t)));
}

wchar_t* wcsncpy(wchar_t* __restrict destination, const wchar_t* __restrict source, size_t size) {
    size_t length = wcsnlen(source, size);

    wmemcpy(destination, source, length);
    wmemset(destination + length, 0, size - length);
    return destination;
}

wchar_t* wcscat(wchar_t* __restrict destination, const wchar_t* __restrict source) {
    wcscpy(destination + wcslen(destination), source);
    return destination;
}

wchar_t* wcschr(const wchar_t* text, wchar_t character) {
    for (;; text++) {
        if (*text == character) {
            return const_cast<wchar_t*>(text);
        }
        if (*text == 0) {
            return nullptr;
        }
    }
}

wchar_t* wcsrchr(const wchar_t* text, wchar_t character) {
    const wchar_t* found = nullptr;

    for (;; text++) {
        if (*text == character) {
            found = text;
        }
        if (*text == 0) {
            return const_cast<wchar_t*>(found);
        }
    }
}

wchar_t* wcsstr(const wchar_t* haystack, const wchar_t* needle) {
    size_t needle_length = wcslen(needle);

    if (needle_length == 0) {
        return const_cast<wchar_t*>(haystack);
    }
    for (; *haystack != 0; haystack++) {
        if (wcsncmp(haystack, needle, needle_length) == 0) {
            return const_cast<wchar_t*>(haystack);
        }
    }
    return nullptr;
}

wchar_t* wmemcpy(wchar_t* __restrict destination, const wchar_t* __restrict source, size_t size) {
    return static_cast<wchar_t*>(memcpy(destination, source, size * sizeof(wchar_t)));
}

wchar_t* wmemmove(wchar_t* destination, const wchar_t* source, size_t size) {
    return static_cast<wchar_t*>(memmove(destination, source, size * sizeof(wchar_t)));
}

wchar_t* wmemset(wchar_t* destination, wchar_t value, size_t size) {
    for (size_t index = 0; index < size; index++) {
        destination[index] = value;
    }
    return destination;
}

int wmemcmp(const wchar_t* left, const wchar_t* right, size_t size) {
    for (size_t index = 0; index < size; index++) {
        if (left[index] != right[index]) {
            return left[index] < right[index] ? -1 : 1;
        }
    }
    return 0;
}

wchar_t* wmemchr(const wchar_t* memory, wchar_t value, size_t size) {
    for (size_t index = 0; index < size; index++) {
        if (memory[index] == value) {
            return const_cast<wchar_t*>(memory + index);
        }
    }
    return nullptr;
}

int mbsinit(const mbstate_t* state) {
    return state == nullptr || state->remaining == 0;
}

size_t mbrtowc(wchar_t* __restrict out_character, const char* __restrict text, size_t size, mbstate_t* __restrict state) {
    uint32_t code_point = 0;
    size_t result = Decode(&code_point, text, size, state);

    if (result != gInvalid && result != gIncomplete && out_character != nullptr && text != nullptr) {
        *out_character = static_cast<wchar_t>(code_point);
    }
    return result;
}

size_t mbrlen(const char* __restrict text, size_t size, mbstate_t* __restrict state) {
    return mbrtowc(nullptr, text, size, state);
}

size_t wcrtomb(char* __restrict out_text, wchar_t character, mbstate_t* __restrict) {
    char local[4];
    size_t length;

    if (out_text == nullptr) {
        out_text = local;
        character = 0;
    }
    length = Libc::Utf8_Encode(static_cast<uint32_t>(character), out_text);
    if (length == 0) {
        errno = EILSEQ;
        return gInvalid;
    }
    return length;
}

size_t mbsrtowcs(wchar_t* __restrict destination, const char** __restrict source, size_t size, mbstate_t* __restrict state) {
    const char* text = *source;
    size_t count = 0;

    while (destination == nullptr || count < size) {
        wchar_t character;
        size_t used = mbrtowc(&character, text, 4, state);

        if (used == gInvalid) {
            *source = text;
            return gInvalid;
        }
        if (destination != nullptr) {
            destination[count] = character;
        }
        if (used == 0) {
            if (destination != nullptr) {
                *source = nullptr;
            }
            return count;
        }
        text += used;
        count++;
    }
    *source = text;
    return count;
}

size_t wcsrtombs(char* __restrict destination, const wchar_t** __restrict source, size_t size, mbstate_t* __restrict) {
    const wchar_t* text = *source;
    size_t written = 0;

    for (;; text++) {
        char encoded[4];
        size_t length = Libc::Utf8_Encode(static_cast<uint32_t>(*text), encoded);

        if (length == 0) {
            errno = EILSEQ;
            *source = text;
            return gInvalid;
        }
        if (*text == 0) {
            if (destination != nullptr) {
                if (written < size) {
                    destination[written] = '\0';
                }
                *source = nullptr;
            }
            return written;
        }
        if (destination != nullptr) {
            if (written + length > size) {
                *source = text;
                return written;
            }
            memcpy(destination + written, encoded, length);
        }
        written += length;
    }
}

wint_t btowc(int character) {
    return character >= 0 && character < 0x80 ? static_cast<wint_t>(character) : WEOF;
}

int wctob(wint_t character) {
    return character < 0x80 ? static_cast<int>(character) : EOF;
}

int iswalnum(wint_t character) {
    return character < 0x80 && isalnum(static_cast<int>(character));
}

int iswalpha(wint_t character) {
    return character < 0x80 && isalpha(static_cast<int>(character));
}

int iswblank(wint_t character) {
    return character < 0x80 && isblank(static_cast<int>(character));
}

int iswcntrl(wint_t character) {
    return character < 0x80 && iscntrl(static_cast<int>(character));
}

int iswdigit(wint_t character) {
    return character < 0x80 && isdigit(static_cast<int>(character));
}

int iswgraph(wint_t character) {
    return character < 0x80 && isgraph(static_cast<int>(character));
}

int iswlower(wint_t character) {
    return character < 0x80 && islower(static_cast<int>(character));
}

int iswprint(wint_t character) {
    return character < 0x80 && isprint(static_cast<int>(character));
}

int iswpunct(wint_t character) {
    return character < 0x80 && ispunct(static_cast<int>(character));
}

int iswspace(wint_t character) {
    return character < 0x80 && isspace(static_cast<int>(character));
}

int iswupper(wint_t character) {
    return character < 0x80 && isupper(static_cast<int>(character));
}

int iswxdigit(wint_t character) {
    return character < 0x80 && isxdigit(static_cast<int>(character));
}

wint_t towlower(wint_t character) {
    return character < 0x80 ? static_cast<wint_t>(tolower(static_cast<int>(character))) : character;
}

wint_t towupper(wint_t character) {
    return character < 0x80 ? static_cast<wint_t>(toupper(static_cast<int>(character))) : character;
}

wctype_t wctype(const char* name) {
    for (wctype_t index = 0; index < gCharacterClassCount; index++) {
        if (strcmp(name, gCharacterClasses[index].name) == 0) {
            return index + 1;
        }
    }
    return 0;
}

int iswctype(wint_t character, wctype_t type) {
    return type != 0 && type <= gCharacterClassCount ? gCharacterClasses[type - 1].test(character) : 0;
}

size_t mbrtoc32(char32_t* __restrict out_character, const char* __restrict text, size_t size, mbstate_t* __restrict state) {
    uint32_t code_point = 0;
    size_t result = Decode(&code_point, text, size, state);

    if (result != gInvalid && result != gIncomplete && out_character != nullptr && text != nullptr) {
        *out_character = code_point;
    }
    return result;
}

size_t c32rtomb(char* __restrict out_text, char32_t character, mbstate_t* __restrict state) {
    return wcrtomb(out_text, static_cast<wchar_t>(character), state);
}

size_t mbrtoc16(char16_t* __restrict out_character, const char* __restrict text, size_t size, mbstate_t* __restrict state) {
    uint32_t code_point = 0;
    size_t result = Decode(&code_point, text, size, state);

    if (result != gInvalid && result != gIncomplete && code_point > 0xFFFF) {
        errno = EILSEQ;
        return gInvalid;
    }
    if (result != gInvalid && result != gIncomplete && out_character != nullptr && text != nullptr) {
        *out_character = static_cast<char16_t>(code_point);
    }
    return result;
}

size_t c16rtomb(char* __restrict out_text, char16_t character, mbstate_t* __restrict state) {
    if (character >= 0xD800 && character <= 0xDFFF) {
        errno = EILSEQ;
        return gInvalid;
    }
    return wcrtomb(out_text, static_cast<wchar_t>(character), state);
}

} // extern "C"
