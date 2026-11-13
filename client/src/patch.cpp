#include "patch.h"

#include <ctype.h>
#include <algorithm>

bool Patch_Path(std::string_view path) {
    std::string lower(path);

    std::ranges::transform(lower, lower.begin(), [](char letter) {
        return static_cast<char>(tolower(letter));
    });
    return lower.ends_with(".bps") || lower.ends_with(".zpf");
}

bool Patch_Apply(std::span<const u8> patch, std::vector<u8>& image, u64 limit) {
    return Bps_Apply(patch, image, limit) || Zpf_Apply(patch, image, limit);
}
