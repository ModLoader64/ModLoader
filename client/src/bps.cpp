#include "patch.h"

#include <string.h>
#include <zlib.h>

// Interpreted from RomPatcher.js's BPS module and https://www.romhacking.net/documents/746/

namespace {

constexpr usize gFooterSize = 12; // source, target and patch checksums

u32 Crc32(std::span<const u8> data) {
    return static_cast<u32>(crc32_z(0, data.data(), data.size()));
}

u32 Word(std::span<const u8> data, usize offset) {
    return data[offset] | data[offset + 1] << 8 | data[offset + 2] << 16 | static_cast<u32>(data[offset + 3]) << 24;
}

// Read a VLV
bool Number(std::span<const u8> data, usize& position, u64& out_number) {
    u64 shift = 1;

    out_number = 0;
    while (position < data.size()) {
        u8 byte = data[position++];
        out_number += (byte & 0x7F) * shift;
        if ((byte & 0x80) != 0) {
            return true;
        }
        shift <<= 7;
        out_number += shift;
    }
    return false;
}

} // namespace

bool Bps_Apply(std::span<const u8> patch, std::vector<u8>& image, u64 limit) {
    // patch checksum mismatch
    if (patch.size() < 4 + gFooterSize || memcmp(patch.data(), "BPS1", 4) != 0 || Crc32(patch.first(patch.size() - 4)) != Word(patch, patch.size() - 4)) {
        return false;
    }

    std::span<const u8> body = patch.first(patch.size() - gFooterSize);
    usize position = 4;
    u64 source_size;
    u64 target_size;
    u64 metadata_size;
    // source ROM checksum mismatch
    if (!Number(body, position, source_size) || !Number(body, position, target_size) || !Number(body, position, metadata_size) ||
        metadata_size > body.size() - position || target_size > limit || source_size != image.size() || Crc32(image) != Word(patch, body.size())) {
        return false;
    }

    position += metadata_size;
    std::vector<u8> output(target_size);
    u64 written = 0;
    u64 offsets[2] = {}; // source and target relative offsets
    u64 action;
    u64 move;
    for (u64 length; position < body.size(); written += length) {
        if (!Number(body, position, action) || (length = (action >> 2) + 1) > target_size - written) {
            return false;
        }

        switch (action & 3) {
        case 0: // source read
            if (written + length > source_size) {
                return false;
            }
            memcpy(output.data() + written, image.data() + written, length);
            break;
        case 1: // target read
            if (length > body.size() - position) {
                return false;
            }
            memcpy(output.data() + written, body.data() + position, length);
            position += length;
            break;
        default: // source copy, target copy
            {
                bool from_output = (action & 3) == 3;
                u64& offset = offsets[from_output];
                if (!Number(body, position, move)) {
                    return false;
                }

                offset += (move & 1) != 0 ? 0 - (move >> 1) : move >> 1;
                if (from_output ? offset >= written : offset > source_size || length > source_size - offset) {
                    return false;
                }

                const u8* from = from_output ? output.data() : image.data();
                for (u64 index = 0; index < length; index++) {
                    output[written + index] = from[offset + index];
                }
                offset += length;
            }
            break;
        }
    }

    // target ROM checksum mismatch
    if (written != target_size || Crc32(output) != Word(patch, body.size() + 4)) {
        return false;
    }

    image = std::move(output);
    return true;
}
