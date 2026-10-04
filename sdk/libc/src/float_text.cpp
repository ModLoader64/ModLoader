#include "float_text.h"

#include "internal.h"
#include "modloader_libc.h"

#include <string.h>

namespace {

template <typename T>
LIBC_NAMESPACE::StrToNumResult<T> Parse(const char* text) {
    ModLoader_Parsed_Float parsed = {};
    double value;
    float single;

    if constexpr (sizeof(T) == sizeof(float)) {
        ModLoader_Host_Parse_Float(text, 0, &parsed);
        memcpy(&single, &parsed.bits, sizeof(single));
        return { single, parsed.length, parsed.error };
    }
    else {
        ModLoader_Host_Parse_Float(text, 1, &parsed);
        memcpy(&value, &parsed.bits, sizeof(value));
        return { static_cast<T>(value), parsed.length, parsed.error };
    }
}

template <typename T>
LIBC_NAMESPACE::StrToNumResult<T> Parse_Wide(const wchar_t* text) {
    char narrow[512];
    size_t length = 0;

    while (length + 1 < sizeof(narrow) && text[length] > 0 && text[length] < 0x80) {
        narrow[length] = static_cast<char>(text[length]);
        length++;
    }
    narrow[length] = '\0';
    return Parse<T>(narrow);
}

} // namespace

namespace LIBC_NAMESPACE_DECL {
namespace internal {

template <>
StrToNumResult<float> strtofloatingpoint<float, char>(const char* __restrict src) {
    return Parse<float>(src);
}

template <>
StrToNumResult<double> strtofloatingpoint<double, char>(const char* __restrict src) {
    return Parse<double>(src);
}

template <>
StrToNumResult<long double> strtofloatingpoint<long double, char>(const char* __restrict src) {
    return Parse<long double>(src);
}

template <>
StrToNumResult<float> strtofloatingpoint<float, wchar_t>(const wchar_t* __restrict src) {
    return Parse_Wide<float>(src);
}

template <>
StrToNumResult<double> strtofloatingpoint<double, wchar_t>(const wchar_t* __restrict src) {
    return Parse_Wide<double>(src);
}

template <>
StrToNumResult<long double> strtofloatingpoint<long double, wchar_t>(const wchar_t* __restrict src) {
    return Parse_Wide<long double>(src);
}

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
