#include <modloader/guest/signature.h>
#include <modloader/guest/machine.h>

#include <algorithm>
#include <ctype.h>

using namespace ModLoader;

namespace {

constexpr u64 gWindowSize = 64 * 1024;
constexpr u32 gNibbleBits = 4;
constexpr u8 gNibbleMask = 0xF;
constexpr u8 gByteMask = 0xFF;

s32 Hex_Value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

} // namespace

Guest::Signature::Signature(std::string_view pattern) {
    usize nibbles = 0;
    for (char character : pattern) {
        if (isspace(static_cast<unsigned char>(character))) {
            continue;
        }

        bool wildcard = character == '?' || character == '.';
        s32 value = wildcard ? 0 : Hex_Value(character);
        if (value < 0) {
            bytes.clear();
            mask.clear();
            return;
        }

        u32 shift = nibbles % 2 == 0 ? gNibbleBits : 0;
        if (shift != 0) {
            bytes.push_back(0);
            mask.push_back(0);
        }
        bytes.back() |= static_cast<u8>(value << shift);
        if (!wildcard) {
            mask.back() |= static_cast<u8>(gNibbleMask << shift);
        }
        nibbles++;
    }

    if (nibbles % 2 != 0) {
        bytes.clear();
        mask.clear();
    }
}

Guest::Signature::Signature(std::span<const u8> pattern, std::span<const u8> pattern_mask) {
    if (pattern_mask.empty() || pattern_mask.size() == pattern.size()) {
        bytes.assign(pattern.begin(), pattern.end());
        mask.assign(pattern_mask.begin(), pattern_mask.end());
        mask.resize(bytes.size(), gByteMask);
    }
}

std::optional<u64> Guest::Signature::Find(u64 from, u64 to, u32 alignment, u32 processor) const {
    return Scan(from, to, alignment, processor, false);
}

std::optional<u64> Guest::Signature::Find_Unique(u64 from, u64 to, u32 alignment, u32 processor) const {
    return Scan(from, to, alignment, processor, true);
}

std::optional<u64> Guest::Signature::Scan(u64 from, u64 to, u32 alignment, u32 processor, bool unique) const {
    const u64 size = bytes.size();
    if (size == 0 || alignment == 0 || to <= from || to - from < size) {
        return std::nullopt;
    }

    const u64 extra = std::min<u64>({gWindowSize - 1, to - from - size, SIZE_MAX - size});
    std::vector<u8> window(static_cast<usize>(size + extra));
    std::optional<u64> found;
    u64 position = from;
    while (to - position >= size) {
        u64 wanted = std::min<u64>(window.size(), to - position);
        u64 read = Machine::Peek(processor, position, window.data(), wanted);
        u64 next = 0;

        for (; next <= read && size <= read - next; next += alignment) {
            const u8* candidate = window.data() + next;
            bool matches = true;
            for (u64 index = 0; index < size && matches; index++) {
                matches = ((candidate[index] ^ bytes[index]) & mask[index]) == 0;
            }

            if (matches) {
                if (!unique) {
                    return position + next;
                }
                if (found) {
                    return std::nullopt;
                }
                found = position + next;
            }
        }

        if (read < wanted) {
            next = std::max(next, (read / alignment + 1) * alignment);
        }

        if (next == 0 || next >= to - position) {
            break;
        }
        position += next;
    }
    return found;
}
