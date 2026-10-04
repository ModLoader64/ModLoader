#pragma once

namespace ModLoader::size_literals {

// ex:
// using namespace ModLoader::size_literals;
// auto bytes = 8_MiB;
consteval unsigned long long operator""_KiB(unsigned long long value) {
    return value << 10;
}

consteval unsigned long long operator""_MiB(unsigned long long value) {
    return value << 20;
}

consteval unsigned long long operator""_GiB(unsigned long long value) {
    return value << 30;
}

consteval unsigned long long operator""_TiB(unsigned long long value) {
    return value << 40;
}

} // namespace ModLoader::size_literals
