#pragma once

#include <memory>
#include <stdio.h>

struct File_Deleter {
    void operator()(FILE* file) const {
        fclose(file);
    }
};

using File_Handle = std::unique_ptr<FILE, File_Deleter>;
