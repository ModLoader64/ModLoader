#pragma once

#include "base.h"
#include "module_execution.h"

#include <chrono>
#include <condition_variable>

class Frontend_Aot_Preparation {
public:
    ~Frontend_Aot_Preparation();

    void Request(std::span<const std::string> paths, const std::string& data_directory, Module_Execution mode);
    bool Poll(std::string& out_status);
    bool Stop();

private:
    void Run();

    Thread worker;
    std::mutex lock;
    std::condition_variable changed;
    std::vector<std::string> paths;
    std::string dataDirectory;
    Module_Execution mode = Module_Execution::Interpreter;
    std::chrono::steady_clock::time_point deadline;
    std::string status;
    u64 revision = 0;
    bool pending = false;
    bool stopping = false;
    bool started = false;
    bool active = false;
};
