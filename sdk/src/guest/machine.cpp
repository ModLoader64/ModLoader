#include "../internal.h"
#include <modloader/guest/machine.h>

using namespace ModLoader;

bool Machine::Describe(Description* description) {
    ModLoader_Host_Platform_Describe(description, sizeof(*description));
    return description->abiVersion == MODLOADER_MODULE_ABI_VERSION;
}

bool Machine::Space_Describe(u32 index, Space* space) {
    return ModLoader_Host_Platform_Space_Describe(index, space, sizeof(*space)) == 0;
}

bool Machine::Processor_Describe(u32 index, Processor* processor) {
    return ModLoader_Host_Platform_Processor_Describe(index, processor, sizeof(*processor)) == 0;
}

s64 Machine::Query(u32 query, void* data, u64 capacity) {
    return ModLoader_Host_Platform_Query(query, data, capacity);
}

u64 Machine::Peek(u32 processor, u64 address, void* buffer, u64 size) {
    return ModLoader_Host_Memory_Peek(processor, address, buffer, size);
}

u64 Machine::Poke(u32 processor, u64 address, const void* data, u64 size) {
    return ModLoader_Host_Memory_Poke(processor, address, data, size);
}

u64 Machine::Translate(u32 processor, u64 address) {
    return ModLoader_Host_Translate_Address(processor, address);
}
