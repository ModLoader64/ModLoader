#pragma once

#include "runtime.h"

struct Module_Preparation {
    const Runtime_Config& config;
    const Runtime_Progress* progress = nullptr;
    Runtime* owner = nullptr;
};

bool Aot_Compile_All(const Module_Preparation& preparation, std::span<const Linked_Module> modules);
std::optional<std::vector<Linked_Module>> Link_Modules(const Module_Preparation& preparation, std::span<const std::string> paths);
