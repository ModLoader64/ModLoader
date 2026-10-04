#include "n64.h"
#include "sha1.h"

#include <algorithm>
#include <utility>

bool N64_Platform::Load_Image(const std::string& path, Game_Image& out_image) {
    std::optional<std::vector<u8>> file = File_Read(path);
    std::vector<u8> data;
    Sha1 hash;
    auto header_text = [&](usize offset, usize length) {
        std::string text(reinterpret_cast<const char*>(data.data() + offset), length);

        text.erase(std::min(text.find('\0'), text.size()));
        text.erase(text.find_last_not_of(' ') + 1);
        return text;
    };

    if (!file) {
        Log_Error("rom", "cannot read %s", path.c_str());
        return false;
    }

    data = std::move(*file);
    if (data.size() < 0x1000) {
        Log_Error("rom", "%s is too small for an N64 ROM", path.c_str());
        return false;
    }

    if (data[0] == 0x37 && data[1] == 0x80 && data[2] == 0x40 && data[3] == 0x12) {
        // .v64: bytes swapped in pairs
        for (usize index = 0; index + 1 < data.size(); index += 2) {
            std::swap(data[index], data[index + 1]);
        }
    }
    else if (data[0] == 0x40 && data[1] == 0x12 && data[2] == 0x37 && data[3] == 0x80) {
        // .n64: little-endian words
        for (usize index = 0; index + 3 < data.size(); index += 4) {
            std::swap(data[index], data[index + 3]);
            std::swap(data[index + 1], data[index + 2]);
        }
    }
    else if (data[0] != 0x80 || data[1] != 0x37 || data[2] != 0x12 || data[3] != 0x40) {
        Log_Error("rom", "invalid N64 header in %s", path.c_str());
        return false;
    }
    
    out_image.title = header_text(0x20, 20);
    Text_Copy(image.title, out_image.title);
    Text_Copy(image.gameCode, header_text(0x3B, 4));
    image.version = data[0x3F];
    image.size = data.size();
    hash.Update(data.data(), data.size());
    out_image.sha1 = hash.Hex_Digest();
    Text_Copy(image.sha1, out_image.sha1);
    out_image.data = std::move(data);
    return true;
}
