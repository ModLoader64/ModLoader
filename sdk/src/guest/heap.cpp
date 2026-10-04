#include <cstdlib>
#include <cstring>

#include <modloader/guest/heap.h>
#include <modloader/guest/memory.h>
#include <modloader/memory.h>

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <utility>

using namespace ModLoader;
using Memory::Align_Up;


namespace {

constexpr Guest::usize gGranule = 16;
constexpr u32 gIndexBits = 20;
constexpr u32 gIndexMask = (1u << gIndexBits) - 1;
constexpr u32 gGenerationMask = (1u << (32 - gIndexBits)) - 1;

enum Block_Flags : u16 {
    Block_Used = 1 << 1,
    Block_Pinned = 1 << 2,
};

u32 Size_Class(Guest::usize size) {
    return static_cast<u32>(std::bit_width(size) - 1);
}

} // namespace

struct ModLoader::Guest::Heap::Block {
    Guest::usize offset; // from base
    Guest::usize size;
    u32 generation;
    u32 previous; // address order, index + 1
    u32 next;
    u32 binPrevious; // size class bin, index + 1
    u32 binNext;
    u16 flags;
    Guest::usize alignment;
    Move_Callback* moveCallback;
};

ModLoader::Guest::Heap::~Heap() {
    Destroy();
}

bool ModLoader::Guest::Heap::Create(Guest::uptr region_base, Guest::usize region_size) {
    Guest::usize lost;
    u32 index;
    Block* extent;

    if (notifying) {
        return false;
    }

    Destroy();
    if (region_base == 0 || region_size > std::numeric_limits<Guest::uptr>::max() - region_base ||
        region_base > std::numeric_limits<Guest::uptr>::max() - (gGranule - 1)) {
        return false;
    }

    base = Align_Up(region_base, gGranule);
    lost = base - region_base;
    size = region_size > lost ? (region_size - lost) & ~(gGranule - 1) : 0;
    if (size == 0) {
        return false;
    }

    index = New_Record();
    if (index == 0) {
        Destroy();
        return false;
    }

    extent = &blocks[index - 1];
    extent->offset = 0;
    extent->size = size;
    first = index;
    Bin_Insert(index);
    return true;
}

void ModLoader::Guest::Heap::Destroy() {
    if (notifying) {
        return;
    }

    notifying = true;
    for (u32 index = 0; index < capacity; index++) {
        delete std::exchange(blocks[index].moveCallback, nullptr);
    }

    free(blocks);
    base = 0;
    size = 0;
    blocks = nullptr;
    capacity = 0;
    first = 0;
    unusedRecords = 0;
    __builtin_memset(bins, 0, sizeof(bins));
    used = 0;
    blockCount = 0;
    notifying = false;
}

// Returns index + 1, or 0 on failure
u32 ModLoader::Guest::Heap::New_Record() {
    Block* record;
    u32 generation;

    if (unusedRecords == 0) {
        u32 grown_capacity = capacity < 64 ? 64 : capacity * 2;
        Block* grown;

        if (grown_capacity > gIndexMask) {
            return 0;
        }

        grown = static_cast<Block*>(realloc(blocks, sizeof(Block) * grown_capacity));
        if (grown == nullptr) {
            return 0;
        }

        __builtin_memset(grown + capacity, 0, sizeof(Block) * (grown_capacity - capacity));
        for (u32 index = grown_capacity; index > capacity; index--) {
            grown[index - 1].next = unusedRecords;
            unusedRecords = index;
        }

        blocks = grown;
        capacity = grown_capacity;
    }

    u32 index = unusedRecords;
    record = &blocks[index - 1];
    unusedRecords = record->next;
    generation = record->generation;
    __builtin_memset(record, 0, sizeof(Block));
    record->generation = generation;
    return index;
}

void ModLoader::Guest::Heap::Release_Record(u32 index) {
    if (index != 0) {
        blocks[index - 1].flags = 0;
        blocks[index - 1].next = unusedRecords;
        unusedRecords = index;
    }
}

void ModLoader::Guest::Heap::Bin_Insert(u32 index) {
    Block* extent = &blocks[index - 1];
    u32 bin = Size_Class(extent->size);

    extent->binPrevious = 0;
    extent->binNext = bins[bin];
    if (bins[bin] != 0) {
        blocks[bins[bin] - 1].binPrevious = index;
    }
    bins[bin] = index;
}

void ModLoader::Guest::Heap::Bin_Remove(u32 index) {
    Block* extent = &blocks[index - 1];
    u32 bin = Size_Class(extent->size);

    if (extent->binPrevious != 0) {
        blocks[extent->binPrevious - 1].binNext = extent->binNext;
    }
    else {
        bins[bin] = extent->binNext;
    }
    if (extent->binNext != 0) {
        blocks[extent->binNext - 1].binPrevious = extent->binPrevious;
    }
    extent->binPrevious = 0;
    extent->binNext = 0;
}

void ModLoader::Guest::Heap::Merge_Free(u32 index) {
    Block* extent = &blocks[index - 1];
    u32 next = extent->next;
    u32 previous;

    if (next != 0 && (blocks[next - 1].flags & Block_Used) == 0) {
        Block* neighbour = &blocks[next - 1];

        Bin_Remove(next);
        extent->size += neighbour->size;
        extent->next = neighbour->next;
        if (neighbour->next != 0) {
            blocks[neighbour->next - 1].previous = index;
        }
        Release_Record(next);
    }

    previous = extent->previous;
    if (previous != 0 && (blocks[previous - 1].flags & Block_Used) == 0) {
        Block* neighbour = &blocks[previous - 1];

        Bin_Remove(previous);
        neighbour->size += extent->size;
        neighbour->next = extent->next;
        if (extent->next != 0) {
            blocks[extent->next - 1].previous = previous;
        }
        Release_Record(index);
        index = previous;
    }
    Bin_Insert(index);
}

ModLoader::Guest::Heap::Handle ModLoader::Guest::Heap::Alloc(Guest::usize bytes, Guest::usize alignment) {
    Guest::usize needed;
    u32 spare_before;
    u32 spare_after;
    u32 found = 0;
    Guest::usize padding = 0;
    Block* extent;
    if (notifying || blocks == nullptr || bytes == 0 || bytes > size) {
        return 0;
    }

    if (alignment < gGranule) {
        alignment = gGranule;
    }

    if ((alignment & (alignment - 1)) != 0) {
        return 0;
    }

    needed = Align_Up(bytes, gGranule);
    spare_before = New_Record();
    spare_after = New_Record();
    if (spare_before == 0 || spare_after == 0) {
        Release_Record(spare_before);
        Release_Record(spare_after);
        return 0;
    }

    for (u32 bin = Size_Class(needed); bin < gBinCount && found == 0; bin++) {
        for (u32 index = bins[bin]; index != 0; index = blocks[index - 1].binNext) {
            Block* candidate = &blocks[index - 1];
            Guest::uptr address = base + candidate->offset;
            Guest::usize pad = (alignment - (address & (alignment - 1))) & (alignment - 1);

            if (pad <= candidate->size && needed <= candidate->size - pad) {
                found = index;
                padding = pad;
                break;
            }
        }
    }

    if (found == 0) {
        Release_Record(spare_before);
        Release_Record(spare_after);
        return 0;
    }

    Bin_Remove(found);
    extent = &blocks[found - 1];
    if (padding != 0) {
        Block* gap = &blocks[spare_before - 1];

        gap->offset = extent->offset;
        gap->size = padding;
        gap->previous = extent->previous;
        gap->next = found;
        if (extent->previous != 0) {
            blocks[extent->previous - 1].next = spare_before;
        }
        else {
            first = spare_before;
        }
        extent->previous = spare_before;
        extent->offset += padding;
        extent->size -= padding;
        Bin_Insert(spare_before);
    }
    else {
        Release_Record(spare_before);
    }

    if (extent->size > needed) {
        Block* rest = &blocks[spare_after - 1];

        rest->offset = extent->offset + needed;
        rest->size = extent->size - needed;
        rest->previous = found;
        rest->next = extent->next;
        if (extent->next != 0) {
            blocks[extent->next - 1].previous = spare_after;
        }
        extent->next = spare_after;
        extent->size = needed;
        Bin_Insert(spare_after);
    }
    else {
        Release_Record(spare_after);
    }

    extent->flags = Block_Used;
    extent->alignment = alignment;
    used += extent->size;
    blockCount++;
    return found | ((extent->generation & gGenerationMask) << gIndexBits);
}

void ModLoader::Guest::Heap::Swap_Locations(u32 left, u32 right) {
    Block& first_block = blocks[left - 1];
    Block& second_block = blocks[right - 1];
    std::swap(first_block.offset, second_block.offset);
    std::swap(first_block.size, second_block.size);
    std::swap(first_block.previous, second_block.previous);
    std::swap(first_block.next, second_block.next);
    auto remap = [left, right](u32 index) {
        return index == left ? right : index == right ? left : index;
    };

    first_block.previous = remap(first_block.previous);
    first_block.next = remap(first_block.next);
    second_block.previous = remap(second_block.previous);
    second_block.next = remap(second_block.next);
    for (u32 index : {left, right}) {
        Block& block = blocks[index - 1];
        if (block.previous != 0) {
            blocks[block.previous - 1].next = index;
        }
        else {
            first = index;
        }
        if (block.next != 0) {
            blocks[block.next - 1].previous = index;
        }
    }
}

bool ModLoader::Guest::Heap::Realloc(Handle block, Guest::usize bytes) {
    Block* record = Record(block);
    if (notifying || record == nullptr || bytes == 0 || bytes > size) {
        return false;
    }

    Guest::usize needed = Align_Up(bytes, gGranule);
    if (needed == record->size) {
        return true;
    }

    Guest::usize old_size = record->size;
    u32 index = block & gIndexMask;
    u32 next = record->next;
    bool next_free = next != 0 && (blocks[next - 1].flags & Block_Used) == 0;
    Move move{block, base + record->offset, base + record->offset, old_size, needed, Move_Reason::Realloc};
    if (needed < old_size) {
        u32 gap = next_free ? next : New_Record();
        if (gap == 0) {
            return false;
        }

        if (!Move_Block(move)) {
            if (!next_free) {
                Release_Record(gap);
            }
            return false;
        }

        record = Record(block);
        Block& remainder = blocks[gap - 1];
        if (next_free) {
            Bin_Remove(gap);
            remainder.size += old_size - needed;
        }
        else {
            remainder.size = old_size - needed;
            remainder.previous = index;
            remainder.next = next;
            if (next != 0) {
                blocks[next - 1].previous = gap;
            }
            record->next = gap;
        }

        remainder.offset = record->offset + needed;
        record->size = needed;
        used -= old_size - needed;
        Bin_Insert(gap);
        return true;
    }

    Guest::usize extra = needed - old_size;
    if (next_free && extra <= blocks[next - 1].size) {
        if (!Move_Block(move)) {
            return false;
        }

        Block& remainder = blocks[next - 1];
        Bin_Remove(next);
        if (extra == remainder.size) {
            record->next = remainder.next;
            if (remainder.next != 0) {
                blocks[remainder.next - 1].previous = index;
            }
            Release_Record(next);
        }
        else {
            remainder.offset += extra;
            remainder.size -= extra;
            Bin_Insert(next);
        }
        record->size = needed;
        used += extra;
        return true;
    }

    if ((record->flags & Block_Pinned) != 0) {
        return false;
    }

    Handle replacement = Alloc(needed, record->alignment);
    if (replacement == 0) {
        return false;
    }

    move.to = Address(replacement);
    if (!Move_Block(move)) {
        Free(replacement);
        return false;
    }

    Swap_Locations(index, replacement & gIndexMask);
    Free(replacement);
    return true;
}

ModLoader::Guest::Heap::Block* ModLoader::Guest::Heap::Record(Handle block) const {
    u32 index = block & gIndexMask;
    Block* record;

    if (index == 0 || index > capacity) {
        return nullptr;
    }

    record = &blocks[index - 1];
    if ((record->flags & Block_Used) == 0 || (record->generation & gGenerationMask) != block >> gIndexBits) {
        return nullptr;
    }
    return record;
}

void ModLoader::Guest::Heap::Free(Handle block) {
    Block* record = Record(block);

    if (notifying || record == nullptr) {
        return;
    }

    Move_Callback* callback = std::exchange(record->moveCallback, nullptr);
    used -= record->size;
    blockCount--;
    record->flags = 0;
    record->generation++;
    Merge_Free(block & gIndexMask);
    notifying = true;
    delete callback;
    notifying = false;
}

Guest::uptr ModLoader::Guest::Heap::Address(Handle block) const {
    Block* record = Record(block);
    return record != nullptr ? base + record->offset : 0;
}

Guest::usize ModLoader::Guest::Heap::Size(Handle block) const {
    Block* record = Record(block);
    return record != nullptr ? record->size : 0;
}

void ModLoader::Guest::Heap::Pin(Handle block) {
    if (!notifying) {
        if (Block* record = Record(block)) {
            record->flags |= Block_Pinned;
        }
    }
}

void ModLoader::Guest::Heap::Unpin(Handle block) {
    if (!notifying) {
        if (Block* record = Record(block)) {
            record->flags &= static_cast<u16>(~Block_Pinned);
        }
    }
}

bool ModLoader::Guest::Heap::Set_Move_Callback(Handle block, Move_Callback callback) {
    Block* record = Record(block);
    if (notifying || record == nullptr) {
        return false;
    }

    Move_Callback* replacement = nullptr;
    if (callback) {
        replacement = new (std::nothrow) Move_Callback(std::move(callback));
        if (replacement == nullptr) {
            return false;
        }
    }
    notifying = true;
    delete std::exchange(record->moveCallback, replacement);
    notifying = false;
    return true;
}

bool ModLoader::Guest::Heap::Notify_Move(Move_Phase phase, const Move& move) {
    Block* record = Record(move.block);
    if (record->moveCallback == nullptr) {
        return true;
    }
    notifying = true;
    bool accepted = (*record->moveCallback)(phase, move);
    notifying = false;
    return accepted;
}

bool ModLoader::Guest::Heap::Move_Block(const Move& move) {
    Guest::usize copied = std::min(move.oldSize, move.newSize);
    if (Record(move.block)->moveCallback == nullptr) {
        if (move.from == move.to) {
            return true;
        }

        if (move.from + move.oldSize <= move.to || move.to + copied <= move.from) {
            return Copy(move.to, move.from, copied);
        }
    }

    if (!Notify_Move(Move_Phase::Prepare, move) || !std::in_range<::usize>(move.oldSize)) {
        Notify_Move(Move_Phase::Rollback, move);
        return false;
    }

    u8* original = static_cast<u8*>(malloc(static_cast<usize>(move.oldSize)));
    if (original == nullptr) {
        Notify_Move(Move_Phase::Rollback, move);
        return false;
    }

    if (Guest::Peek(move.from, original, move.oldSize) != move.oldSize) {
        free(original);
        Notify_Move(Move_Phase::Rollback, move);
        return false;
    }

    if ((move.from == move.to || Guest::Poke(move.to, original, copied) == copied) &&
        Notify_Move(Move_Phase::Commit, move)) {
        free(original);
        return true;
    }

    Guest::Poke(move.from, original, move.oldSize);
    Notify_Move(Move_Phase::Rollback, move);
    free(original);
    return false;
}

Guest::usize ModLoader::Guest::Heap::Compact() {
    Guest::usize cursor = 0;
    u32 previous = 0;
    Guest::usize end = 0;
    auto link = [this, &previous](u32 record) {
        blocks[record - 1].previous = previous;
        blocks[record - 1].next = 0;
        if (previous != 0) {
            blocks[previous - 1].next = record;
        }
        else {
            first = record;
        }
        previous = record;
    };

    if (notifying || blocks == nullptr || used == 0 || used == size) {
        return Largest_Free();
    }

    u32 gap_capacity = blockCount + 1;
    u32* gaps = static_cast<u32*>(malloc(static_cast<usize>(gap_capacity) * sizeof(u32)));
    if (gaps == nullptr) {
        return Largest_Free();
    }

    u32 gap_count = 0;
    while (gap_count < gap_capacity) {
        u32 gap = New_Record();
        if (gap == 0) {
            while (gap_count != 0) {
                Release_Record(gaps[--gap_count]);
            }
            free(gaps);
            return Largest_Free();
        }
        gaps[gap_count++] = gap;
    }

    // Reserve all gap records before any move can commit
    for (u32 index = first; index != 0; index = blocks[index - 1].next) {
        Block* block = &blocks[index - 1];

        if ((block->flags & Block_Used) == 0) {
            continue;
        }

        if ((block->flags & Block_Pinned) == 0) {
            Guest::usize target = Align_Up(base + cursor, block->alignment) - base;

            Handle handle = index | ((block->generation & gGenerationMask) << gIndexBits);
            Move move{handle, base + block->offset, base + target, block->size, block->size, Move_Reason::Compact};
            if (target < block->offset && Move_Block(move)) {
                block->offset = target;
            }
        }
        cursor = block->offset + block->size;
    }

    __builtin_memset(bins, 0, sizeof(bins));
    u32 index = first;
    first = 0;
    while (index != 0) {
        u32 next = blocks[index - 1].next;
        Block snapshot;

        if ((blocks[index - 1].flags & Block_Used) == 0) {
            Release_Record(index);
            index = next;
            continue;
        }

        snapshot = blocks[index - 1];
        if (snapshot.offset > end) {
            u32 gap = gaps[--gap_count];

            blocks[gap - 1].offset = end;
            blocks[gap - 1].size = snapshot.offset - end;
            link(gap);
            Bin_Insert(gap);
        }

        link(index);
        end = snapshot.offset + snapshot.size;
        index = next;
    }

    if (end < size) {
        u32 gap = gaps[--gap_count];
        blocks[gap - 1].offset = end;
        blocks[gap - 1].size = size - end;
        link(gap);
        Bin_Insert(gap);
    }

    while (gap_count != 0) {
        Release_Record(gaps[--gap_count]);
    }
    free(gaps);
    return Largest_Free();
}

Guest::usize ModLoader::Guest::Heap::Used_Bytes() const {
    return used;
}

Guest::usize ModLoader::Guest::Heap::Free_Bytes() const {
    return size - used;
}

Guest::usize ModLoader::Guest::Heap::Largest_Free() const {
    for (u32 bin = gBinCount; bin > 0; bin--) {
        Guest::usize largest = 0;

        for (u32 index = bins[bin - 1]; index != 0; index = blocks[index - 1].binNext) {
            largest = blocks[index - 1].size > largest ? blocks[index - 1].size : largest;
        }
        
        if (largest != 0) {
            return largest;
        }
    }
    return 0;
}

u32 ModLoader::Guest::Heap::Block_Count() const {
    return blockCount;
}

