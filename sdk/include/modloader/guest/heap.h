#pragma once

#include <modloader/guest/types.h>
#include <modloader/function.h>

namespace ModLoader::Guest {

// Bookkeeping is not in guest memory
class Heap {
public:
    using Handle = u32; // 0: none

    enum class Move_Phase : u32 {
        Prepare,  // Before copying; save references needed for rollback
        Commit,   // Bytes are at Move::to; update external references
        Rollback, // Move failed; restore references to Move::from
    };

    enum class Move_Reason : u32 {
        Realloc,
        Compact,
    };

    struct Move {
        Handle block;
        uptr from;
        uptr to;
        usize oldSize;
        usize newSize;
        Move_Reason reason;
    };

    using Move_Callback = Function<bool(Move_Phase, const Move&)>;

    Heap() = default;
    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;
    ~Heap();

    // Replaces the current heap with [base, base + size), trimmed to 16-byte boundaries
    bool Create(uptr base, usize size);
    // Handles/references into the old heap become invalid
    void Destroy();

    // Sizes round up to 16 bytes. Power-of-two alignment is clamped to at least 16
    Handle Alloc(usize size, usize alignment = 16);
    // Keeps the handle and alignment
    bool Realloc(Handle block, usize size);
    void Free(Handle block);

    // Freed/stale handles return 0; Size includes rounding. If you store a copy of this, realloc/compact may change it!
    uptr Address(Handle block) const;
    usize Size(Handle block) const;

    // Pinned blocks may resize in place, but cannot move
    void Pin(Handle block);
    void Unpin(Handle block);

    // prepare allows you to accept or veto the move
    // commit allows you to update cached pointers or references
    // rollback happens on move fail
    bool Set_Move_Callback(Handle block, Move_Callback callback);

    // Returns the largest resulting gap; pinned and vetoed blocks do not get compacted
    usize Compact();

    usize Used_Bytes() const; // Includes allocation rounding
    usize Free_Bytes() const;
    usize Largest_Free() const; // Largest gap, before alignment padding
    u32 Block_Count() const;

private:
    struct Block;
    static constexpr u32 gBinCount = sizeof(usize) * 8;

    u32 New_Record();
    void Release_Record(u32 index);
    void Bin_Insert(u32 index);
    void Bin_Remove(u32 index);
    void Merge_Free(u32 index);
    Block* Record(Handle block) const;
    void Swap_Locations(u32 left, u32 right);
    bool Notify_Move(Move_Phase phase, const Move& move);
    bool Move_Block(const Move& move);

    uptr base = 0;
    usize size = 0;
    Block* blocks = nullptr; // records: used and free extents, linked in address order
    u32 capacity = 0;
    u32 first = 0;            // index + 1 of the lowest extent
    u32 unusedRecords = 0;    // index + 1 of a free record list
    u32 bins[gBinCount] = {}; // index + 1 of the first free extent per size class
    usize used = 0;
    u32 blockCount = 0;
    bool notifying = false;
};

} // namespace ModLoader::Guest

