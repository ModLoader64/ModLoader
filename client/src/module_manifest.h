#pragma once

#include "base.h"

struct Module_Manifest {
    std::string name;
    std::string version;
    std::string folder;
    std::vector<std::string> authors;
    std::string description;
    std::vector<std::string> dependencies;
    std::string icon;
    std::string banner;
    std::string sourcePath;
    std::string assetRoot;
};

struct Module_Metadata {
    Module_Manifest manifest;
    std::string runtime;
    std::string runtimeHash;
};

bool Module_Name_Is_Valid(std::string_view name);
bool Module_Relative_Path_Is_Valid(std::string_view path);
std::optional<Module_Manifest> Parse_Module_Manifest(std::span<const u8> bytes, const std::string& source);
std::optional<Module_Metadata> Read_Module_Metadata(std::span<const u8> bytes, const std::string& source);
bool Module_Manifest_Matches(const Module_Manifest& left, const Module_Manifest& right);
