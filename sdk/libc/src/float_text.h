#pragma once

#include "src/__support/str_to_float.h"

namespace LIBC_NAMESPACE_DECL {
namespace internal {

template <> StrToNumResult<float> strtofloatingpoint<float, char>(const char* __restrict src);
template <> StrToNumResult<double> strtofloatingpoint<double, char>(const char* __restrict src);
template <> StrToNumResult<long double> strtofloatingpoint<long double, char>(const char* __restrict src);
template <> StrToNumResult<float> strtofloatingpoint<float, wchar_t>(const wchar_t* __restrict src);
template <> StrToNumResult<double> strtofloatingpoint<double, wchar_t>(const wchar_t* __restrict src);
template <> StrToNumResult<long double> strtofloatingpoint<long double, wchar_t>(const wchar_t* __restrict src);

} // namespace internal
} // namespace LIBC_NAMESPACE_DECL
