#ifndef MODLOADER_LIBC_UCHAR_H
#define MODLOADER_LIBC_UCHAR_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#else
typedef unsigned short char16_t;
typedef unsigned int char32_t;
typedef unsigned char char8_t;
#endif

// UTF-8 conversions, the char16_t variants support only U+0000->U+FFFF
size_t mbrtoc16(char16_t* __restrict out_character, const char* __restrict text, size_t size, mbstate_t* __restrict state);
size_t c16rtomb(char* __restrict out_text, char16_t character, mbstate_t* __restrict state);
size_t mbrtoc32(char32_t* __restrict out_character, const char* __restrict text, size_t size, mbstate_t* __restrict state);
size_t c32rtomb(char* __restrict out_text, char32_t character, mbstate_t* __restrict state);

#ifdef __cplusplus
}
#endif

#endif
