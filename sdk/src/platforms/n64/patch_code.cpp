#include "patch_code.h"
#include "relocate.h"

#include <modloader/platforms/n64/platform.h>
#include <modloader/guest/memory.h>
#include <modloader_bytes.h>
#include <disassembler.h>

#include <algorithm>
#include <limits>

namespace ModLoader::N64::Detail {

namespace {

bool Range_Valid(Guest::uptr address, Guest::usize size) {
    return size != 0 && static_cast<u64>(address) + size <= static_cast<u64>(std::numeric_limits<Guest::uptr>::max()) + 1;
}

bool Is_Hypercall(u32 instruction) {
    auto function = static_cast<::N64::Special_Function>(instruction & 63);
    return instruction >> 26 == 0 && (function == ::N64::Special_Function::Hypercall || function == ::N64::Special_Function::Hypercallr);
}

bool Entry_Valid(Guest::uptr address) {
    u8 previous[4];
    return address < 4 || !Read_Code(address - 4, previous) || !::N64::Disassembler(Bytes::Read_Be32(previous), address - 4).Analyze().hasDelaySlot;
}

bool Source_Valid(Guest::uptr address, std::span<const u8> bytes) {
    for (Guest::usize offset = 0; offset < bytes.size(); offset += 4) {
        if (Is_Hypercall(Bytes::Read_Be32(bytes.data() + offset))) {
            return false;
        }
    }

    u32 first = Bytes::Read_Be32(bytes.data());
    auto operation = static_cast<::N64::Opcode>(first >> 26);
    if (operation == ::N64::Opcode::J || operation == ::N64::Opcode::Jal) {
        Guest::uptr target = ((address + 4) & 0xF0000000u) | ((first & 0x03FFFFFFu) << 2);
        u8 entry[4];
        if (Read_Code(target, entry) && Is_Hypercall(Bytes::Read_Be32(entry))) {
            return false;
        }
    }
    return true;
}

Guest::usize Context_Size(std::span<const u8> bytes, Guest::uptr address, Patch_Mode mode) {
    return mode == Patch_Mode::Detour ? Preserved_Size(bytes, address) : static_cast<Guest::usize>(bytes.size());
}

std::vector<u8> Encode(std::span<const u32> words) {
    std::vector<u8> bytes(words.size_bytes());
    Bytes::Write_Be32(bytes.data(), words.data(), words.size());
    return bytes;
}

} // namespace

bool Overlaps(Guest::uptr left, Guest::usize left_size, Guest::uptr right, Guest::usize right_size) {
    return static_cast<u64>(Physical_Address(left)) < static_cast<u64>(Physical_Address(right)) + right_size && static_cast<u64>(Physical_Address(right)) < static_cast<u64>(Physical_Address(left)) + left_size;
}

bool Ram_Range(Guest::uptr address, Guest::usize size) {
    return Range_Valid(address, size) && gPlatform.Is_Extended_Ram(address) && gPlatform.Is_Extended_Ram(address + size - 1);
}

bool Read_Code(Guest::uptr address, std::span<u8> bytes) {
    return bytes.size() <= UINT32_MAX && Ram_Range(address, static_cast<Guest::usize>(bytes.size())) && Guest::Peek(address, bytes.data(), bytes.size()) == bytes.size();
}

bool Write_Code(Guest::uptr address, std::span<const u8> bytes) {
    return bytes.size() <= UINT32_MAX && Ram_Range(address, static_cast<Guest::usize>(bytes.size())) && Guest::Poke(address, bytes.data(), bytes.size()) == bytes.size();
}

Guest::usize Preserved_Size(std::span<const u8> bytes, Guest::uptr address) {
    bool first_delay = ::N64::Disassembler(Bytes::Read_Be32(bytes.data()), address).Analyze().hasDelaySlot;
    bool second_delay = ::N64::Disassembler(Bytes::Read_Be32(bytes.data() + 4), address + 4).Analyze().hasDelaySlot;
    return !first_delay && second_delay ? 12 : 8;
}

bool Patch_Code::Begin(Guest::uptr source, Patch_Mode placement, Patch::Preserve preservation,
    std::span<const u8> bytes, bool external, Guest::uptr target, std::span<const u8> canonical) {
    if (created || (source & 3) != 0 ||
        placement > Patch_Mode::Detour || preservation > Patch::Preserve::Source || bytes.size() > UINT32_MAX ||
        (bytes.size() & 3) != 0 || (placement == Patch_Mode::Inline && (preservation != Patch::Preserve::None || bytes.empty() || external))) {
        return false;
    }

    Guest::usize source_size = placement == Patch_Mode::Inline ? static_cast<Guest::usize>(bytes.size()) : 8;
    bool needs_cave = placement == Patch_Mode::Detour && !external;
    u64 capacity = needs_cave ? bytes.size() + gMaxRelocatedWords * sizeof(u32) : 0;
    u32 jump[2];
    if (!Range_Valid(source, source_size) || capacity > UINT32_MAX ||
        (external && (!Detour_Words(source, target, jump) || Overlaps(source, source_size, target, 4))) ||
        !Entry_Valid(source) || (!external && !Payload_Valid(bytes.data(), static_cast<Guest::usize>(bytes.size()), source))) {
        return false;
    }

    std::vector<u8> saved(source_size);
    if (canonical.empty()) {
        if (!Read_Code(source, saved)) {
            return false;
        }
    }
    else {
        if (canonical.size() < source_size) {
            return false;
        }
        std::copy_n(canonical.begin(), source_size, saved.begin());
    }

    Guest::usize context_size = Context_Size(saved, source, placement);
    if (!Range_Valid(source, context_size) || (!canonical.empty() && canonical.size() != context_size)) {
        return false;
    }

    if (context_size > source_size) {
        saved.resize(context_size);
        if (!canonical.empty()) {
            std::copy(canonical.begin() + source_size, canonical.end(), saved.begin() + source_size);
        }
        else if (!Read_Code(source + source_size, std::span(saved).subspan(source_size))) {
            return false;
        }
    }

    if (!Source_Valid(source, saved)) {
        return false;
    }

    mode = placement;
    preserve = preservation;
    address = source;
    destination = target;
    patchSize = source_size;
    payloadSize = static_cast<Guest::usize>(bytes.size());
    destinationCapacity = static_cast<Guest::usize>(capacity);
    original = std::move(saved);
    payload.assign(bytes.begin(), bytes.end());
    patched.resize(source_size);
    direct = external;
    created = true;
    if (!needs_cave) {
        Relocation prepared;
        if (!Prepare(original, 0, 0, prepared)) {
            Clear();
            return false;
        }
        patched = std::move(prepared.patched);
    }
    return true;
}

bool Patch_Code::Create_Destination(Guest::uptr source, Guest::uptr target) {
    return Begin(source, Patch_Mode::Detour, Patch::Preserve::None, {}, true, target);
}

bool Patch_Code::Create_Hypercall(Guest::uptr source, Hypercall::Handle id, Patch_Mode placement, Patch::Preserve preservation,
    std::span<const u8> canonical) {
    if (id > 0xFFFFF) {
        return false;
    }
    u8 bytes[4];
    Bytes::Write_Be32(bytes, ::N64::Encode_Hypercall(id));
    return Begin(source, placement, preservation, bytes, false, 0, canonical);
}

bool Patch_Code::Create_Assembly(Guest::uptr source, std::string_view text, Patch_Mode placement, Patch::Preserve preservation,
    std::span<const u8> canonical) {
    auto words = Assembler::Assemble(source, text);
    if (!words || !Begin(source, placement, preservation, Encode(*words), false, 0, canonical)) {
        return false;
    }
    assembly = text;
    return true;
}

bool Patch_Code::Create_Bytes(Guest::uptr source, std::span<const u8> bytes) {
    return Begin(source, Patch_Mode::Inline, Patch::Preserve::None, bytes);
}

bool Patch_Code::Prepare(std::span<const u8> source, Guest::uptr cave, Guest::usize capacity, Relocation& prepared) const {
    if (mode == Patch_Mode::Inline) {
        prepared.patched = payload;
        return true;
    }
    u32 words[gMaxRelocatedWords];
    u32 instructions[3] = {Bytes::Read_Be32(source.data()), Bytes::Read_Be32(source.data() + 4),
        source.size() > 8 ? Bytes::Read_Be32(source.data() + 8) : 0};
    if (!(preserve == Patch::Preserve::Source
            ? Preserved_Detour_Words(instructions, address, cave, words)
            : Detour_Words(address, direct ? destination : cave, words))) {
        return false;
    }

    prepared.patched = Encode({words, 2});

    if (destinationCapacity == 0) {
        return true;
    }

    if (cave == 0 || !Range_Valid(cave, capacity)) {
        return false;
    }

    prepared.destination = payload;
    if (!assembly.empty()) {
        auto assembled = Assembler::Assemble(cave, assembly);
        if (!assembled) {
            return false;
        }
        prepared.destination = Encode(*assembled);
    }

    if (prepared.destination.size() > capacity || prepared.destination.size() > UINT32_MAX - cave) {
        return false;
    }

    Guest::usize payload_size = static_cast<Guest::usize>(prepared.destination.size());
    prepared.payloadSize = payload_size;
    if (!direct && !Payload_Valid(prepared.destination.data(), payload_size, cave)) {
        return false;
    }

    u32 count;
    if (preserve == Patch::Preserve::Source) {
        count = Relocate_Detour(instructions, address, cave + payload_size, words);
        if (count == 0) {
            return false;
        }
    }
    else {
        if (!Detour_Words(cave + payload_size, address + 8, words)) {
            return false;
        }
        count = 2;
    }

    if (static_cast<u64>(payload_size) + count * sizeof(u32) > capacity) {
        return false;
    }
    prepared.destination.resize(payload_size + count * sizeof(u32));
    Bytes::Write_Be32(prepared.destination.data() + payload_size, words, count);
    return true;
}

bool Patch_Code::Prepare_Relocation(Guest::uptr cave, Guest::usize capacity, Relocation& relocation) const {
    if (!created || destinationCapacity == 0 || cave == 0 || (cave & 3) != 0 ||
        !Range_Valid(cave, capacity) ||
        Overlaps(cave, capacity, address, static_cast<Guest::usize>(original.size()))) {
        return false;
    }

    Relocation prepared;
    if (!Prepare(original, cave, capacity, prepared)) {
        return false;
    }

    prepared.address = cave;
    prepared.capacity = capacity;
    relocation = std::move(prepared);
    return true;
}

void Patch_Code::Commit_Relocation(Relocation&& relocation) noexcept {
    trampoline = relocation.address;
    boundCapacity = relocation.capacity;
    payloadSize = relocation.payloadSize;
    patched = std::move(relocation.patched);
    destinationBytes = std::move(relocation.destination);
}

bool Patch_Code::Relocate(Guest::uptr cave, Guest::usize capacity) {
    Relocation relocation;
    if (!Prepare_Relocation(cave, capacity, relocation)) {
        return false;
    }

    if (enabled) {
        std::vector<u8> source(patchSize);
        if (!Read_Code(address, source) || source != patched) {
            return false;
        }
    }

    std::vector<u8> saved(relocation.destination.size());
    if (!Read_Code(cave, saved)) {
        return false;
    }

    if (!Write_Changed(cave, relocation.destination) || (enabled && !Write_Changed(address, relocation.patched))) {
        Write_Code(cave, saved);
        if (trampoline != 0 && Overlaps(cave, static_cast<Guest::usize>(saved.size()), trampoline, static_cast<Guest::usize>(destinationBytes.size()))) {
            Write_Code(trampoline, destinationBytes);
        }
        return false;
    }
    
    Commit_Relocation(std::move(relocation));
    return true;
}

Guest::usize Patch_Code::Installed_Size(std::span<const u8> bytes) const {
    if (std::equal(patched.begin(), patched.end(), bytes.begin())) {
        return patchSize;
    }
    if (mode == Patch_Mode::Detour && std::equal(patched.begin(), patched.begin() + 4, bytes.begin())) {
        return 4;
    }
    return 0;
}

bool Patch_Code::Write_Changed(Guest::uptr target, std::span<const u8> bytes) const {
    if (bytes.empty()) {
        return true;
    }

    std::vector<u8> saved(bytes.size());
    if (!Read_Code(target, saved)) {
        return false;
    }

    if (std::equal(saved.begin(), saved.end(), bytes.begin())) {
        return true;
    }

    if (Write_Code(target, bytes)) {
        return true;
    }

    Write_Code(target, saved);
    return false;
}

bool Patch_Code::Enable() {
    if (!created || (destinationCapacity != 0 && trampoline == 0) || !Entry_Valid(address)) {
        return false;
    }

    std::vector<u8> current(patchSize);
    if (!Read_Code(address, current)) {
        return false;
    }

    Guest::usize installed = Installed_Size(current);
    enabled = installed == patchSize;
    if (installed != 0 && installed != patchSize) {
        Disable();
        return false;
    }

    std::copy_n(original.begin(), installed, current.begin());
    if (!Read_Context(current)) {
        return false;
    }

    bool changed = current != original;
    Relocation prepared;
    if (changed && (!Source_Valid(address, current) || !Prepare(current, trampoline, boundCapacity, prepared))) {
        Disable();
        return false;
    }

    std::span<const u8> next_patch = changed ? prepared.patched : patched;
    std::span<const u8> next_destination = changed ? prepared.destination : destinationBytes;
    std::vector<u8> old_destination(next_destination.size());
    if (!old_destination.empty() && !Read_Code(trampoline, old_destination)) {
        return false;
    }

    bool destination_changed = !std::equal(old_destination.begin(), old_destination.end(), next_destination.begin());
    if (destination_changed && !Write_Changed(trampoline, next_destination)) {
        return false;
    }

    if (!Write_Changed(address, next_patch)) {
        if (destination_changed) {
            Write_Code(trampoline, old_destination);
        }
        return false;
    }

    if (changed) {
        original = std::move(current);
        patched = std::move(prepared.patched);
        destinationBytes = std::move(prepared.destination);
        payloadSize = prepared.payloadSize;
    }
    enabled = true;
    return true;
}

bool Patch_Code::Read_Context(std::vector<u8>& source) const {
    Guest::usize context_size = Context_Size(source, address, mode);
    if (!Range_Valid(address, context_size)) {
        return false;
    }

    source.resize(context_size);
    return context_size <= patchSize || Read_Code(address + patchSize, std::span(source).subspan(patchSize));
}

bool Patch_Code::Disable() {
    if (!created || (destinationCapacity != 0 && trampoline == 0)) {
        return true;
    }

    std::vector<u8> current(patchSize);
    if (!Read_Code(address, current)) {
        return false;
    }

    Guest::usize installed = Installed_Size(current);
    if (installed != 0 && !Write_Changed(address, {original.data(), installed})) {
        return false;
    }
    
    enabled = false;
    return true;
}

bool Patch_Code::Destroy() {
    if (!Disable()) {
        return false;
    }
    Clear();
    return true;
}

void Patch_Code::Clear() {
    mode = Patch_Mode::Inline;
    preserve = Patch::Preserve::None;
    address = 0;
    destination = 0;
    trampoline = 0;
    patchSize = 0;
    payloadSize = 0;
    destinationCapacity = 0;
    boundCapacity = 0;
    original.clear();
    payload.clear();
    patched.clear();
    destinationBytes.clear();
    assembly.clear();
    direct = false;
    created = false;
    enabled = false;
}

} // namespace ModLoader::N64::Detail
