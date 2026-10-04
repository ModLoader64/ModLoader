#pragma once

#include <modloader/types.h>

#include <string>

namespace ModLoader::Text {

// printf-style formatting
std::string Format(const char* format, ...) __attribute__((format(printf, 1, 2)));
std::string Format_List(const char* format, __builtin_va_list arguments);
void Append(std::string& text, const char* format, ...) __attribute__((format(printf, 2, 3)));

} // namespace ModLoader::Text
