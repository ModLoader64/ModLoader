#pragma once

#include "base.h"

struct Run_Options {
    std::string romPath;
    std::string adapterPath;
    std::string dataDirectory;
    std::vector<std::string> modulePaths;
    bool server = false;
    bool host = false;
    std::string connect;
    std::string nickname;
    std::string lobby;
    std::string password;
    u16 hostPort = 0;
    std::string hostAddress;

    bool Online() const {
        return !connect.empty() || host;
    }
};

s32 Session_Run(const Run_Options& options);
s32 Dedicated_Run(const Run_Options& options);

std::string Aot_Compiler_Path();
