#include "wasm_object.h"

#include <string.h>

namespace {

constexpr u8 gHeader[]{0, 'a', 's', 'm', 1, 0, 0, 0};

enum class Symbol : u8 {
    Function,
    Data,
    Global,
    Section,
    Tag,
    Table,
};

constexpr u32 gHidden = 4;
constexpr u32 gUndefined = 16;
constexpr u32 gExported = 32;
constexpr u32 gExplicitName = 64;

struct Reader {
    std::span<const u8> bytes;
    usize position = 0;
    bool valid = true;

    u8 Byte() {
        if (position == bytes.size()) {
            valid = false;
            return 0;
        }
        return bytes[position++];
    }

    u64 Number(u32 bits = 32) {
        u64 value = 0;
        for (u32 shift = 0; shift < bits; shift += 7) {
            u8 byte = Byte();
            if (!valid || (bits - shift < 7 && byte >= (1u << (bits - shift)))) {
                valid = false;
                return 0;
            }

            value |= static_cast<u64>(byte & 127) << shift;
            if ((byte & 128) == 0) {
                return value;
            }
        }

        valid = false;
        return 0;
    }

    std::span<const u8> Take(u64 size) {
        if (size > bytes.size() - position) {
            valid = false;
            return {};
        }

        auto result = bytes.subspan(position, static_cast<usize>(size));
        position += static_cast<usize>(size);
        return result;
    }

    std::string_view Text() {
        auto text = Take(Number());
        return {reinterpret_cast<const char*>(text.data()), text.size()};
    }
};

void Number(std::vector<u8>& bytes, u64 number) {
    do {
        u8 byte = number & 127;
        number >>= 7;
        bytes.push_back(byte | (number != 0 ? 128 : 0));
    } while (number != 0);
}

void Text(std::vector<u8>& bytes, std::string_view text) {
    Number(bytes, text.size());
    bytes.insert(bytes.end(), text.begin(), text.end());
}

void Section(std::vector<u8>& bytes, u8 kind, std::span<const u8> payload) {
    bytes.push_back(kind);
    Number(bytes, payload.size());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}

bool Symbols(Reader& input, std::vector<u8>& output, std::string_view prefix) {
    u64 count = input.Number();
    if (count > input.bytes.size() - input.position) {
        return false;
    }

    Number(output, count);
    for (u64 index = 0; index < count && input.valid; index++) {
        auto kind = static_cast<Symbol>(input.Byte());
        u64 flags = input.Number();
        bool defined = (flags & gUndefined) == 0;
        bool hidden = defined && (flags & gHidden) != 0 && (flags & gExported) == 0;
        output.push_back(static_cast<u8>(kind));
        Number(output, flags);
        auto name = [&]() {
            std::string_view symbol = input.Text();
            Text(output, hidden ? std::string(prefix) + std::string(symbol) : std::string(symbol));
        };

        switch (kind) {
        case Symbol::Function:
        case Symbol::Global:
        case Symbol::Tag:
        case Symbol::Table:
            Number(output, input.Number());
            if (defined || (flags & gExplicitName) != 0) {
                name();
            }
            break;
        case Symbol::Data:
            name();
            if (defined) {
                Number(output, input.Number());
                Number(output, input.Number(64));
                Number(output, input.Number(64));
            }
            break;
        case Symbol::Section:
            Number(output, input.Number());
            break;
        default:
            return false;
        }
    }

    return input.valid && input.position == input.bytes.size();
}

bool Comdats(Reader& input, std::vector<u8>& output, std::string_view prefix) {
    u64 count = input.Number();
    if (count > input.bytes.size() - input.position) {
        return false;
    }

    Number(output, count);
    for (u64 index = 0; index < count && input.valid; index++) {
        Text(output, std::string(prefix) + std::string(input.Text()));
        Number(output, input.Number());
        u64 entries = input.Number();
        if (entries > input.bytes.size() - input.position) {
            return false;
        }

        Number(output, entries);
        for (u64 jndex = 0; jndex < entries && input.valid; jndex++) {
            output.push_back(input.Byte());
            Number(output, input.Number());
        }
    }

    return input.valid && input.position == input.bytes.size();
}

bool Linking(Reader& input, std::vector<u8>& output, std::string_view prefix) {
    u64 version = input.Number();
    if (version != 2) {
        return false;
    }

    Number(output, version);
    bool symbols = false;
    while (input.valid && input.position < input.bytes.size()) {
        u8 kind = input.Byte();
        auto bytes = input.Take(input.Number());
        Reader subsection{bytes};
        std::vector<u8> rewritten;
        if (kind == 8) {
            if (symbols || !Symbols(subsection, rewritten, prefix)) {
                return false;
            }
            symbols = true;
        }
        else if (kind == 7) {
            if (!Comdats(subsection, rewritten, prefix)) {
                return false;
            }
        }
        else {
            rewritten.assign(bytes.begin(), bytes.end());
        }

        Section(output, kind, rewritten);
    }

    return input.valid && symbols;
}

} // namespace

bool Wasm_Read_Metadata(std::span<const u8> bytes, Wasm_Metadata& metadata) {
    metadata = {};
    if (bytes.size() < sizeof(gHeader) || memcmp(bytes.data(), gHeader, sizeof(gHeader)) != 0) {
        return false;
    }

    Reader input{bytes, sizeof(gHeader)};
    bool has_module = false;
    bool has_runtime = false;
    while (input.valid && input.position < bytes.size()) {
        u8 kind = input.Byte();
        Reader section{input.Take(input.Number())};
        if (kind != 0) {
            continue;
        }

        std::string_view name = section.Text();
        if (!section.valid) {
            return false;
        }

        auto payload = section.Take(section.bytes.size() - section.position);
        if (name == "modloader.module") {
            if (has_module || payload.empty()) {
                return false;
            }
            metadata.module = payload;
            has_module = true;
        }
        else if (name == "modloader.runtime") {
            if (has_runtime || payload.empty()) {
                return false;
            }
            metadata.runtime = payload;
            has_runtime = true;
        }
        else if (name == "linking") {
            metadata.relocatable = true;
        }
    }

    return input.valid && !(has_module && has_runtime);
}

bool Wasm_Localize(std::vector<u8>& object, std::string_view module) {
    if (object.size() < sizeof(gHeader) || memcmp(object.data(), gHeader, sizeof(gHeader)) != 0) {
        return false;
    }

    Reader input{object, sizeof(gHeader)};
    std::vector<u8> output(std::begin(gHeader), std::end(gHeader));
    std::string prefix = "__modloader_private." + std::to_string(module.size()) + "." + std::string(module) + ".";
    bool linking = false;
    while (input.valid && input.position < object.size()) {
        u8 kind = input.Byte();
        auto bytes = input.Take(input.Number());
        Reader section{bytes};
        std::string_view name = kind == 0 ? section.Text() : std::string_view{};
        if (kind == 0 && name == "linking") {
            std::vector<u8> rewritten;
            Text(rewritten, name);
            if (linking || !Linking(section, rewritten, prefix)) {
                return false;
            }
            Section(output, kind, rewritten);
            linking = true;
        }
        else {
            Section(output, kind, bytes);
        }

        if (!section.valid) {
            return false;
        }
    }

    if (!input.valid || !linking) {
        return false;
    }
    
    object = std::move(output);
    return true;
}
