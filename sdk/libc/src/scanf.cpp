// LLVM's scanf core
#include "float_text.h"
#include "internal.h"

#include "src/__support/arg_list.h"
#include "src/stdio/scanf_core/reader.h"
#include "src/stdio/scanf_core/scanf_main.h"
#include "src/stdio/scanf_core/string_reader.h"

#include <stdarg.h>
#include <stdio.h>

namespace Scanf = LIBC_NAMESPACE::scanf_core;

namespace {

// Reads a FILE a character at a time; the end reads as '\0', which stops the parse
class File_Reader : public Scanf::Reader<File_Reader> {
public:
    explicit File_Reader(FILE* stream)
        : stream(stream) {
    }

    char getc() {
        int character = fgetc(stream);

        return character == EOF ? '\0' : static_cast<char>(character);
    }

    void ungetc(int character) {
        if (character != '\0') {
            ::ungetc(character, stream);
        }
    }

private:
    FILE* stream;
};

} // namespace

extern "C" {

int vsscanf(const char* __restrict text, const char* __restrict format, va_list arguments) {
    int conversions;

    LIBC_NAMESPACE::internal::ArgList list(arguments);
    Scanf::StringReader reader(text, static_cast<size_t>(-1));

    conversions = Scanf::scanf_main(&reader, format, list);
    // Running out of input before the first conversion is EOF rather than 0
    if (conversions == 0 && text[reader.chars_read()] == '\0') {
        return EOF;
    }
    return conversions;
}

int sscanf(const char* __restrict text, const char* __restrict format, ...) {
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vsscanf(text, format, arguments);
    va_end(arguments);
    return result;
}

int vfscanf(FILE* __restrict stream, const char* __restrict format, va_list arguments) {
    int conversions;

    LIBC_NAMESPACE::internal::ArgList list(arguments);
    File_Reader reader(stream);

    conversions = Scanf::scanf_main(&reader, format, list);
    if (conversions == 0 && feof(stream)) {
        return EOF;
    }
    return conversions;
}

int fscanf(FILE* __restrict stream, const char* __restrict format, ...) {
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vfscanf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int vscanf(const char* __restrict format, va_list arguments) {
    return vfscanf(stdin, format, arguments);
}

int scanf(const char* __restrict format, ...) {
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vfscanf(stdin, format, arguments);
    va_end(arguments);
    return result;
}

} // extern "C"
