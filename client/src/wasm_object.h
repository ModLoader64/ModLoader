#pragma once

#include "base.h"

struct Wasm_Metadata {
    std::span<const u8> module;
    std::span<const u8> runtime;
    bool relocatable = false;
};

bool Wasm_Read_Metadata(std::span<const u8> bytes, Wasm_Metadata& metadata);
bool Wasm_Localize(std::vector<u8>& object, std::string_view module);
