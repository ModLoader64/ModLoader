#pragma once

#include "runtime.h"

struct Module_Link_Input {
    Module_Manifest manifest;
    std::string fingerprint;
    std::string packageFingerprint;
    std::vector<u8> object;
    usize runtimeIndex = 0;
};

struct Module_Link_Runtime {
    std::string fingerprint;
    std::vector<u8> object;
    u64 stackSize = 0;
    u64 maxMemory = 0;
};

struct Module_Link_Plan {
    std::vector<Module_Link_Runtime> runtimes;
    std::vector<std::vector<Module_Link_Input>> groups;
};

std::optional<Module_Link_Plan> Prepare_Link(Runtime& runtime, std::span<const std::string> paths);
