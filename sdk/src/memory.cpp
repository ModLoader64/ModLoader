#include "internal.h"
#include <modloader/memory.h>
#include <cstdlib>

namespace {

constexpr usize gChunkSize = 4096;

struct Chunk {
    Chunk* next;
    usize capacity;
    usize used;
};

struct Scratch {
    Chunk* first;
    Chunk* current;
};

thread_local Scratch sScratch;

void Release_At_Thread_Exit(void*) {
    ModLoader::Runtime::Scratch_Release();
}

void* Allocate(Chunk* chunk, usize size, usize alignment) {
    if (chunk == nullptr) {
        return nullptr;
    }
    usize base = reinterpret_cast<usize>(chunk);
    usize start = ModLoader::Memory::Align_Up(base + chunk->used, alignment) - base;
    if (start > chunk->capacity || size > chunk->capacity - start) {
        return nullptr;
    }
    chunk->used = start + size;
    return reinterpret_cast<u8*>(chunk) + start;
}

} // namespace

void* ModLoader::Memory::Scratch_Alloc(usize size, usize alignment) {
    Scratch* scratch = &sScratch;

    if (alignment < 16) {
        alignment = 16;
    }

    if ((alignment & (alignment - 1)) != 0 || alignment > SIZE_MAX - sizeof(Chunk) ||
        size > SIZE_MAX - sizeof(Chunk) - alignment) {
        return nullptr;
    }
    
    if (void* data = Allocate(scratch->current, size, alignment)) {
        return data;
    }

    usize required = sizeof(Chunk) + size + alignment;
    usize capacity = required > gChunkSize ? required : gChunkSize;
    auto chunk = static_cast<Chunk*>(malloc(capacity));
    if (chunk == nullptr) {
        return nullptr;
    }

    *chunk = {nullptr, capacity, sizeof(Chunk)};
    if (scratch->first == nullptr) {
        __cxa_thread_atexit_impl(Release_At_Thread_Exit, nullptr, nullptr);
        scratch->first = chunk;
    }
    else {
        scratch->current->next = chunk;
    }

    scratch->current = chunk;
    return Allocate(chunk, size, alignment);
}

void ModLoader::Runtime::Scratch_Reset() {
    Scratch* scratch = &sScratch;
    Chunk* chunk = scratch->first != nullptr ? scratch->first->next : nullptr;

    while (chunk != nullptr) {
        Chunk* next = chunk->next;

        free(chunk);
        chunk = next;
    }
    
    if (scratch->first != nullptr) {
        scratch->first->next = nullptr;
        scratch->first->used = sizeof(Chunk);
    }
    scratch->current = scratch->first;
}

void ModLoader::Runtime::Scratch_Release() {
    Scratch_Reset();
    free(sScratch.first);
    sScratch.first = nullptr;
    sScratch.current = nullptr;
}
