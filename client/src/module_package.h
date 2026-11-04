#pragma once

#include "base.h"

struct Module_Package {
    std::string root;
    std::string modulePath;
    std::string fingerprint;
    std::vector<u8> manifest;
};

std::optional<Module_Package> Open_Module_Package(const std::string& path, const std::string& data_directory);
std::optional<std::vector<u8>> Read_Module_Package_Manifest(const std::string& path);
