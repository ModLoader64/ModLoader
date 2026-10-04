#include <modloader/text.h>

#include <stdio.h>

namespace {

void Append_List(std::string& text, const char* format, __builtin_va_list arguments) {
    __builtin_va_list measured;
    usize start = text.size();
    s32 length;

    __builtin_va_copy(measured, arguments);
    length = vsnprintf(nullptr, 0, format, measured);
    __builtin_va_end(measured);
    if (length <= 0) {
        return;
    }
    
    text.resize(start + static_cast<usize>(length));
    vsnprintf(text.data() + start, static_cast<usize>(length) + 1, format, arguments);
}

} // namespace

std::string ModLoader::Text::Format_List(const char* format, __builtin_va_list arguments) {
    std::string text;

    Append_List(text, format, arguments);
    return text;
}

std::string ModLoader::Text::Format(const char* format, ...) {
    __builtin_va_list arguments;
    std::string text;

    __builtin_va_start(arguments, format);
    Append_List(text, format, arguments);
    __builtin_va_end(arguments);
    return text;
}

void ModLoader::Text::Append(std::string& text, const char* format, ...) {
    __builtin_va_list arguments;

    __builtin_va_start(arguments, format);
    Append_List(text, format, arguments);
    __builtin_va_end(arguments);
}
