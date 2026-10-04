// LLVM's printf core
#include "internal.h"

#include "src/__support/arg_list.h"
#include "src/stdio/printf_core/error_mapper.h"
#include "src/stdio/printf_core/printf_main.h"
#include "src/stdio/printf_core/writer.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Printf = LIBC_NAMESPACE::printf_core;

namespace LIBC_NAMESPACE_DECL {
namespace printf_core {

// long double prints as double
template <WriteMode write_mode>
int convert_float(Writer<write_mode>* writer, const FormatSection& section) {
    char local[128];
    char* text = local;
    double value = section.length_modifier == LengthModifier::L ? static_cast<double>(fputil::FPBits<long double>(section.conv_val_raw).get_val())
                                                                : fputil::FPBits<double>(static_cast<uint64_t>(section.conv_val_raw)).get_val();
    uint64_t bits = cpp::bit_cast<uint64_t>(value);
    int64_t length = ModLoader_Host_Format_Float(local, sizeof(local), bits, section.conv_name, section.flags, section.min_width, section.precision);
    int result;

    if (length < 0) {
        return FILE_WRITE_ERROR;
    }
    if (static_cast<uint64_t>(length) >= sizeof(local)) {
        text = static_cast<char*>(malloc(static_cast<size_t>(length) + 1));
        if (text == nullptr) {
            return ALLOCATION_ERROR;
        }
        ModLoader_Host_Format_Float(text, static_cast<uint64_t>(length) + 1, bits, section.conv_name, section.flags, section.min_width, section.precision);
    }
    result = writer->write(cpp::string_view(text, static_cast<size_t>(length)));
    if (text != local) {
        free(text);
    }
    return result;
}

} // namespace printf_core
} // namespace LIBC_NAMESPACE_DECL

namespace {

constexpr size_t gStreamChunk = 512;

struct Stream_Target {
    Libc::Format_Sink sink;
    void* context;
};

int Write_To_Sink(LIBC_NAMESPACE::cpp::string_view text, void* target) {
    Stream_Target* stream = static_cast<Stream_Target*>(target);
    return stream->sink(text.data(), text.size(), stream->context) == 0 ? Printf::WRITE_OK : -1;
}

int Finish(LIBC_NAMESPACE::ErrorOr<size_t> result) {
    if (!result.has_value()) {
        errno = Printf::internal_error_to_errno(result.error());
        return -1;
    }

    if (result.value() > static_cast<size_t>(INT_MAX)) {
        errno = EOVERFLOW;
        return -1;
    }
    return static_cast<int>(result.value());
}

} // namespace

int Libc::Format_To(Format_Sink sink, void* context, const char* format, va_list arguments) {
    char chunk[gStreamChunk];
    Stream_Target target = { sink, context };
    LIBC_NAMESPACE::internal::ArgList list(arguments);
    Printf::FlushingBuffer buffer(chunk, sizeof(chunk), Write_To_Sink, &target);
    Printf::Writer writer(buffer);
    LIBC_NAMESPACE::ErrorOr<size_t> result = Printf::printf_main_modular(&writer, format, list);

    if (result.has_value() && buffer.flush_to_stream() != Printf::WRITE_OK) {
        errno = EIO;
        return -1;
    }
    return Finish(result);
}

extern "C" {

int vsnprintf(char* __restrict buffer, size_t size, const char* __restrict format, va_list arguments) {
    LIBC_NAMESPACE::internal::ArgList list(arguments);
    // With a size of 0 the buffer may be null; nothing is written then
    Printf::DropOverflowBuffer output(buffer, size > 0 ? size - 1 : 0);
    Printf::Writer writer(output);
    LIBC_NAMESPACE::ErrorOr<size_t> result = Printf::printf_main_modular(&writer, format, list);

    if (size > 0) {
        output.buff[output.buff_cur] = '\0';
    }
    return Finish(result);
}

int snprintf(char* __restrict buffer, size_t size, const char* __restrict format, ...) {
    va_list arguments;
    int length;

    va_start(arguments, format);
    length = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return length;
}

int vsprintf(char* __restrict buffer, const char* __restrict format, va_list arguments) {
    return vsnprintf(buffer, static_cast<size_t>(INT_MAX), format, arguments);
}

int sprintf(char* __restrict buffer, const char* __restrict format, ...) {
    va_list arguments;
    int length;

    va_start(arguments, format);
    length = vsprintf(buffer, format, arguments);
    va_end(arguments);
    return length;
}

int vasprintf(char** __restrict out_text, const char* __restrict format, va_list arguments) {
    va_list measure;
    int length;
    char* text;

    va_copy(measure, arguments);
    length = vsnprintf(nullptr, 0, format, measure);
    va_end(measure);
    *out_text = nullptr;
    if (length < 0) {
        return -1;
    }

    text = static_cast<char*>(malloc(static_cast<size_t>(length) + 1));
    if (text == nullptr) {
        errno = ENOMEM;
        return -1;
    }
    
    vsnprintf(text, static_cast<size_t>(length) + 1, format, arguments);
    *out_text = text;
    return length;
}

int asprintf(char** __restrict out_text, const char* __restrict format, ...) {
    va_list arguments;
    int length;

    va_start(arguments, format);
    length = vasprintf(out_text, format, arguments);
    va_end(arguments);
    return length;
}

} // extern "C"
