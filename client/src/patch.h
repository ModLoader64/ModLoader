#pragma once

#include "base.h"

// .bps, .zpf
bool Patch_Path(std::string_view path);
// Patches the image if the patch is for it, to no more than limit bytes
bool Patch_Apply(std::span<const u8> patch, std::vector<u8>& image, u64 limit = 1ull << 30);

bool Bps_Apply(std::span<const u8> patch, std::vector<u8>& image, u64 limit);
bool Zpf_Apply(std::span<const u8> patch, std::vector<u8>& image, u64 limit);
