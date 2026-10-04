#include "internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
extern unsigned char __heap_base;
}

namespace {

constexpr size_t gWasmPageSize = 65536;
constexpr size_t gHeaderSize = 16;
constexpr size_t gSmallClassCount = 8; // 32, 64, -> 4096 bytes of payload
constexpr size_t gSmallestClass = 32;

struct Block_Header {
    size_t size; // payload bytes; for an aligned block, the address of the block it lives in
    Block_Header* next; // free list link while free; gAlignedMark in front of an aligned block
};

// Odd, so never a free list link
Block_Header* const gAlignedMark = reinterpret_cast<Block_Header*>(static_cast<uintptr_t>(0xA119E3D1));

struct Heap_State {
    unsigned char* top;
    unsigned char* end;
    Block_Header* smallFree[gSmallClassCount];
    Block_Header* largeFree;
};

Heap_State sHeap;
Libc::Spin_Lock sLock;

size_t Align_Up(size_t value, size_t alignment) {
    size_t rounded;
    return __builtin_add_overflow(value, alignment - 1, &rounded) ? 0 : rounded & ~(alignment - 1);
}

int Small_Class(size_t size) {
    size_t class_size = gSmallestClass;

    for (int index = 0; index < static_cast<int>(gSmallClassCount); index++) {
        if (size <= class_size) {
            return index;
        }
        class_size <<= 1;
    }
    return -1;
}

size_t Small_Class_Size(int index) {
    return gSmallestClass << index;
}

void* Heap_Take(size_t bytes) {
    if (sHeap.top == nullptr) {
        sHeap.top = reinterpret_cast<unsigned char*>(Align_Up(reinterpret_cast<size_t>(&__heap_base), 16));
        sHeap.end = reinterpret_cast<unsigned char*>(__builtin_wasm_memory_size(0) * gWasmPageSize);
    }

    size_t required;
    if (__builtin_add_overflow(reinterpret_cast<size_t>(sHeap.top), bytes, &required)) {
        return nullptr;
    }

    if (required > reinterpret_cast<size_t>(sHeap.end)) {
        size_t end = Align_Up(required, gWasmPageSize);
        if (end == 0) {
            return nullptr;
        }

        size_t pages = (end - reinterpret_cast<size_t>(sHeap.end)) / gWasmPageSize;
        if (__builtin_wasm_memory_grow(0, pages) == static_cast<size_t>(-1)) {
            return nullptr;
        }
        sHeap.end = reinterpret_cast<unsigned char*>(end);
    }

    void* result = sHeap.top;
    sHeap.top = reinterpret_cast<unsigned char*>(required);
    return result;
}

Block_Header* Header_Of(void* pointer) {
    return reinterpret_cast<Block_Header*>(static_cast<unsigned char*>(pointer) - gHeaderSize);
}

void* Payload_Of(Block_Header* header) {
    return reinterpret_cast<unsigned char*>(header) + gHeaderSize;
}

void* Allocate(size_t size) {
    int small_class;
    size_t rounded;
    Block_Header** link;
    Block_Header* header;

    if (size == 0) {
        size = 1;
    }

    small_class = Small_Class(size);
    if (small_class >= 0) {
        size_t class_size;

        header = sHeap.smallFree[small_class];
        if (header != nullptr) {
            sHeap.smallFree[small_class] = header->next;
            header->next = nullptr;
            return Payload_Of(header);
        }

        class_size = Small_Class_Size(small_class);
        header = static_cast<Block_Header*>(Heap_Take(gHeaderSize + class_size));
        if (header == nullptr) {
            return nullptr;
        }

        header->size = class_size;
        header->next = nullptr;
        return Payload_Of(header);
    }

    rounded = Align_Up(size, 16);
    if (rounded == 0 || rounded > static_cast<size_t>(-1) - gHeaderSize) {
        return nullptr;
    }

    link = &sHeap.largeFree;
    while (*link != nullptr) {
        Block_Header* candidate = *link;

        if (candidate->size >= rounded) {
            *link = candidate->next;
            candidate->next = nullptr;
            return Payload_Of(candidate);
        }
        link = &candidate->next;
    }

    header = static_cast<Block_Header*>(Heap_Take(gHeaderSize + rounded));
    if (header == nullptr) {
        return nullptr;
    }

    header->size = rounded;
    header->next = nullptr;
    return Payload_Of(header);
}

void Release(void* pointer) {
    Block_Header* header = Header_Of(pointer);
    int small_class;

    if (header->next == gAlignedMark) {
        Release(reinterpret_cast<void*>(header->size));
        return;
    }

    small_class = Small_Class(header->size);
    if (small_class >= 0 && Small_Class_Size(small_class) == header->size) {
        header->next = sHeap.smallFree[small_class];
        sHeap.smallFree[small_class] = header;
        return;
    }
    header->next = sHeap.largeFree;
    sHeap.largeFree = header;
}

size_t Usable(void* pointer) {
    Block_Header* header = Header_Of(pointer);

    if (header->next == gAlignedMark) {
        unsigned char* outer = reinterpret_cast<unsigned char*>(header->size);

        return Header_Of(outer)->size - static_cast<size_t>(static_cast<unsigned char*>(pointer) - outer);
    }

    return header->size;
}

} // namespace

extern "C" LIBC_EXPORT("malloc") void* malloc(size_t size) {
    Libc::Scoped lock(sLock);
    void* pointer = Allocate(size);
    if (pointer == nullptr) {
        errno = ENOMEM;
    }
    return pointer;
}

extern "C" LIBC_EXPORT("free") void free(void* pointer) {
    if (pointer == nullptr) {
        return;
    }
    Libc::Scoped lock(sLock);
    Release(pointer);
}

extern "C" void* calloc(size_t count, size_t size) {
    if (size != 0 && count > static_cast<size_t>(-1) / size) {
        errno = ENOMEM;
        return nullptr;
    }

    size_t total = count * size;
    void* pointer = malloc(total);
    if (pointer != nullptr) {
        memset(pointer, 0, total);
    }
    return pointer;
}

extern "C" void* realloc(void* pointer, size_t size) {
    if (pointer == nullptr) {
        return malloc(size);
    }

    if (size == 0) {
        free(pointer);
        return nullptr;
    }

    size_t usable;
    {
        Libc::Scoped lock(sLock);
        usable = Usable(pointer);
    }

    if (size <= usable) {
        return pointer;
    }

    void* replacement = malloc(size);
    if (replacement == nullptr) {
        return nullptr;
    }

    memcpy(replacement, pointer, usable);
    free(pointer);
    return replacement;
}

extern "C" void* aligned_alloc(size_t alignment, size_t size) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        errno = EINVAL;
        return nullptr;
    }

    if (alignment <= 16) {
        return malloc(size);
    }

    size_t allocation_size;
    if (__builtin_add_overflow(size, gHeaderSize, &allocation_size) ||
        __builtin_add_overflow(allocation_size, alignment - 1, &allocation_size)) {
        errno = ENOMEM;
        return nullptr;
    }

    unsigned char* outer = static_cast<unsigned char*>(malloc(allocation_size));
    if (outer == nullptr) {
        return nullptr;
    }

    unsigned char* aligned = reinterpret_cast<unsigned char*>(Align_Up(reinterpret_cast<size_t>(outer) + gHeaderSize, alignment));
    Block_Header* header = Header_Of(aligned);
    header->size = reinterpret_cast<size_t>(outer);
    header->next = gAlignedMark;
    return aligned;
}

extern "C" int posix_memalign(void** out_pointer, size_t alignment, size_t size) {
    if (alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0) {
        return EINVAL;
    }

    void* pointer = aligned_alloc(alignment, size);
    if (pointer == nullptr) {
        return ENOMEM;
    }
    
    *out_pointer = pointer;
    return 0;
}

extern "C" size_t malloc_usable_size(void* pointer) {
    if (pointer == nullptr) {
        return 0;
    }
    Libc::Scoped lock(sLock);
    return Usable(pointer);
}
