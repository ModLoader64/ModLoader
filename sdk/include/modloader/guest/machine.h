#pragma once

#include <modloader/types.h>
#include <modloader_platform.h>

namespace ModLoader::Machine {

using Description = ModLoader_Platform_Description;
using Space = ModLoader_Module_Space;
using Processor = ModLoader_Module_Processor;

// Runtime platform discovery
// Indices identify the current machine's spaces/processors
bool Describe(Description* description);
bool Space_Describe(u32 index, Space* space);
bool Processor_Describe(u32 index, Processor* processor);

// Platform defined query; returns bytes written, or a negative error
s64 Query(u32 query, void* data, u64 capacity);
// CPU-address reads/writes in game byte order; returns bytes transferred
u64 Peek(u32 processor, u64 address, void* buffer, u64 size);
u64 Poke(u32 processor, u64 address, const void* data, u64 size); // Poke invalidates changed code
// Resolve to a fixed guest space address; 0 when unmapped
u64 Translate(u32 processor, u64 address);

} // namespace ModLoader::Machine
