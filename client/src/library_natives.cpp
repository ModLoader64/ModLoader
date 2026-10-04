#include "module.h"
#include "modloader_libc.h"

#include "src/__support/str_to_float.h"
#include "src/stdio/printf_core/float_dec_converter.h"
#include "src/stdio/printf_core/float_hex_converter.h"
#include "src/stdio/printf_core/writer.h"

#include "zlib.h"

namespace Printf = LIBC_NAMESPACE::printf_core;

namespace {

// return full length or -1
void Native_Format_Float(wasm_exec_env_t exec_env, u64* slots) {
    u64 capacity = slots[1];
    char* buffer = capacity != 0 ? Module_Of(exec_env).Memory_As<char>(slots[0], capacity) : nullptr;
    char nothing[1];
    Printf::DropOverflowBuffer output(buffer != nullptr ? buffer : nothing, capacity != 0 ? static_cast<size_t>(capacity - 1) : 0);
    Printf::Writer writer(output);
    Printf::FormatSection section = {};
    int result;

    slots[0] = static_cast<u64>(-1);
    if (capacity != 0 && buffer == nullptr) {
        return;
    }

    section.has_conv = true;
    section.conv_name = static_cast<char>(slots[3]);
    section.flags = static_cast<Printf::FormatFlags>(slots[4]);
    section.min_width = static_cast<int>(slots[5]);
    section.precision = static_cast<int>(slots[6]);
    section.conv_val_raw = slots[2];
    switch (section.conv_name) {
    case 'f':
    case 'F':
        result = Printf::convert_float_decimal(&writer, section);
        break;
    case 'e':
    case 'E':
        result = Printf::convert_float_dec_exp(&writer, section);
        break;
    case 'g':
    case 'G':
        result = Printf::convert_float_dec_auto(&writer, section);
        break;
    case 'a':
    case 'A':
        result = Printf::convert_float_hex_exp(&writer, section);
        break;
    default:
        return;
    }

    if (buffer != nullptr) {
        output.buff[output.buff_cur] = '\0';
    }

    slots[0] = result < 0 ? static_cast<u64>(-1) : writer.get_chars_written();
}

void Native_Parse_Float(wasm_exec_env_t exec_env, u64* slots) {
    Module& module = Module_Of(exec_env);
    ModLoader_Parsed_Float* out = module.Memory_As<ModLoader_Parsed_Float>(slots[2]);
    const char* text = module.C_Text(slots[0]);
    ModLoader_Parsed_Float parsed = {};

    if (out == nullptr || text == nullptr) {
        return;
    }

    if (slots[1] != 0) {
        auto result = LIBC_NAMESPACE::internal::strtofloatingpoint<double>(text);

        parsed.bits = __builtin_bit_cast(u64, result.value);
        parsed.length = result.parsed_len;
        parsed.error = result.error;
    }
    else {
        auto result = LIBC_NAMESPACE::internal::strtofloatingpoint<float>(text);

        parsed.bits = __builtin_bit_cast(u32, result.value);
        parsed.length = result.parsed_len;
        parsed.error = result.error;
    }

    *out = parsed;
}

void Native_Zlib_Compress(wasm_exec_env_t exec_env, u64* slots) {
    Module& module = Module_Of(exec_env);
    u64* dest_length = module.Memory_As<u64>(slots[1]);
    Bytef* dest = dest_length != nullptr ? module.Memory_As<Bytef>(slots[0], *dest_length) : nullptr;
    const Bytef* source = module.Memory_As<Bytef>(slots[2], slots[3]);
    z_size_t length = dest_length != nullptr ? static_cast<z_size_t>(*dest_length) : 0;
    int result = Z_STREAM_ERROR;

    if (dest != nullptr && (source != nullptr || slots[3] == 0)) {
        result = compress2_z(dest, &length, source, static_cast<z_size_t>(slots[3]), static_cast<int>(slots[4]));
        *dest_length = length;
    }

    slots[0] = static_cast<u32>(result);
}

void Native_Zlib_Uncompress(wasm_exec_env_t exec_env, u64* slots) {
    Module& module = Module_Of(exec_env);
    u64* dest_length = module.Memory_As<u64>(slots[1]);
    u64* source_length = module.Memory_As<u64>(slots[3]);
    Bytef* dest = dest_length != nullptr && *dest_length != 0 ? module.Memory_As<Bytef>(slots[0], *dest_length) : nullptr;
    const Bytef* source = source_length != nullptr ? module.Memory_As<Bytef>(slots[2], *source_length) : nullptr;
    Bytef nothing[1];
    z_size_t produced;
    z_size_t consumed;
    int result = Z_STREAM_ERROR;

    if (dest_length != nullptr && source != nullptr && (dest != nullptr || *dest_length == 0)) {
        produced = static_cast<z_size_t>(*dest_length);
        consumed = static_cast<z_size_t>(*source_length);
        result = uncompress2_z(dest != nullptr ? dest : nothing, &produced, source, &consumed);
        *dest_length = produced;
        *source_length = consumed;
    }

    slots[0] = static_cast<u32>(result);
}

NativeSymbol sNatives[] = {
    Native("libc_format_float", "(IIIiiii)I", Native_Format_Float),
    Native("libc_parse_float", "(IiI)", Native_Parse_Float),
    Native("zlib_compress", "(IIIIi)i", Native_Zlib_Compress),
    Native("zlib_uncompress", "(IIII)i", Native_Zlib_Uncompress),
};

} // namespace

void Library_Register_Natives() {
    Register_Natives(sNatives, "shared libraries");
}
