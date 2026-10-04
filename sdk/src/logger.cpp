#include "internal.h"
#include <modloader/logger.h>

#include <stdio.h>

void ModLoader::Logger::Detail::Write_List(Level level, const char* source, u64 source_length, const char* format, __builtin_va_list arguments) {
    char buffer[1024];
    s32 length = vsnprintf(buffer, sizeof(buffer), format, arguments);

    if (length < 0) {
        length = 0;
    }
    else if (length >= static_cast<s32>(sizeof(buffer))) {
        length = sizeof(buffer) - 1;
    }
    ModLoader_Host_Log_Source(static_cast<u32>(level), source, source_length, buffer, static_cast<u64>(length));
}
