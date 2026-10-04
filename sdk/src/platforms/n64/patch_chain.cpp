#include "patch_chain.h"
#include "relocate.h"

#include <modloader/platforms/n64/code.h>
#include <modloader_bytes.h>
#include <algorithm>
#include <array>
#include <memory>
#include <unordered_set>
#include <utility>

namespace ModLoader::N64::Detail {

namespace {

constexpr u32 gChainMagic = 0x4D4C5043;
Hypercall::Handle sChainOwner = 0;
u32 sNextChain = 1;

enum Preservation_Flags : u32 {
    Preserve_Source = 1,
    Preserve_Chain = 2,
};

struct Node {
    Guest::uptr source = 0;
    Guest::uptr entry = 0;
    Guest::usize capacity = 0;
    Guest::usize payload = 0;
    Guest::usize span = 0;
    Guest::uptr next = 0;
    bool linked = false;
    bool preserve = false;
    bool preserveChain = false;
    std::array<u8, 12> original{};
    u64 chain = 0;
};

std::array<u32, 3> Original_Words(const Node& node) {
    return {Bytes::Read_Be32(node.original.data()), Bytes::Read_Be32(node.original.data() + 4), Bytes::Read_Be32(node.original.data() + 8)};
}

bool Source_Words(const Node& node, Guest::uptr source, Guest::uptr target, u32 words[2]) {
    auto original = Original_Words(node);
    return node.preserveChain ? Preserved_Detour_Words(original.data(), source, target, words) : Detour_Words(source, target, words);
}

enum class Found {
    Missing,
    Present,
    Invalid,
};

u32 Checksum(std::span<const u8> bytes) {
    u32 result = 2166136261u;
    for (u8 byte : bytes) {
        result = (result ^ byte) * 16777619u;
    }
    return result;
}

std::vector<u8> Encode(const Node& node) {
    std::vector<u8> bytes(Patch_Chain::Header_Size);
    u32 words[] = {gChainMagic, 1, node.source, node.entry, node.capacity, node.payload, node.span, node.next, node.linked ? 1u : 0u};
    Bytes::Write_Be32(bytes.data(), words, std::size(words));
    std::copy(node.original.begin(), node.original.end(), bytes.begin() + 36);
    Bytes::Write_Be32(bytes.data() + 48, static_cast<u32>(node.chain >> 32));
    Bytes::Write_Be32(bytes.data() + 52, static_cast<u32>(node.chain));
    Bytes::Write_Be32(bytes.data() + 56, (node.preserve ? Preserve_Source : 0u) | (node.preserveChain ? Preserve_Chain : 0u));
    Bytes::Write_Be32(bytes.data() + 60, Checksum({bytes.data(), 60}));
    return bytes;
}

Found Read_Node(Guest::uptr entry, Node& node) {
    if (entry < Patch_Chain::Header_Size || (entry & 3) != 0) {
        return Found::Missing;
    }

    std::array<u8, Patch_Chain::Header_Size> bytes{};
    if (!Read_Code(entry - Patch_Chain::Header_Size, bytes) || Bytes::Read_Be32(bytes.data()) != gChainMagic) {
        return Found::Missing;
    }

    if (Bytes::Read_Be32(bytes.data() + 4) != 1 || Bytes::Read_Be32(bytes.data() + 12) != entry ||
        Bytes::Read_Be32(bytes.data() + 60) != Checksum({bytes.data(), 60})) {
        return Found::Invalid;
    }

    node.source = Bytes::Read_Be32(bytes.data() + 8);
    node.entry = entry;
    node.capacity = Bytes::Read_Be32(bytes.data() + 16);
    node.payload = Bytes::Read_Be32(bytes.data() + 20);
    node.span = Bytes::Read_Be32(bytes.data() + 24);
    node.next = Bytes::Read_Be32(bytes.data() + 28);
    u32 linked = Bytes::Read_Be32(bytes.data() + 32);
    node.linked = linked != 0;
    std::copy_n(bytes.begin() + 36, node.original.size(), node.original.begin());
    node.chain = (static_cast<u64>(Bytes::Read_Be32(bytes.data() + 48)) << 32) | Bytes::Read_Be32(bytes.data() + 52);
    u32 preservation = Bytes::Read_Be32(bytes.data() + 56);
    node.preserve = (preservation & Preserve_Source) != 0;
    node.preserveChain = (preservation & Preserve_Chain) != 0;
    if (((node.source | node.payload | node.next) & 3) != 0 || linked > 1 ||
        (preservation & ~(Preserve_Source | Preserve_Chain)) != 0 ||
        (node.span != 8 && node.span != 12) || node.payload == 0 || (node.linked && node.chain == 0) ||
        node.capacity > UINT32_MAX - Patch_Chain::Header_Size ||
        static_cast<u64>(node.payload) + gMaxRelocatedWords * 4 > node.capacity ||
        static_cast<u64>(entry) + node.capacity > (u64{1} << 32) ||
        Overlaps(entry - Patch_Chain::Header_Size, node.capacity + Patch_Chain::Header_Size, node.source, node.span)) {
        return Found::Invalid;
    }
    return Found::Present;
}

Found Read_Head(Guest::uptr source, Node& node) {
    std::array<u8, 8> bytes{};
    if (!Read_Code(source, bytes)) {
        return Found::Missing;
    }

    u32 word = Bytes::Read_Be32(bytes.data());
    auto operation = static_cast<::N64::Opcode>(word >> 26);
    if (operation != ::N64::Opcode::J && operation != ::N64::Opcode::Jal) {
        return Found::Missing;
    }

    Guest::uptr entry = ((source + 4) & 0xF0000000u) | ((word & 0x03FFFFFFu) << 2);
    Found found = Read_Node(entry, node);
    if (found == Found::Present) {
        u32 expected[2];
        if (Physical_Address(node.source) != Physical_Address(source) || !Source_Words(node, source, entry, expected) ||
            word != expected[0] || Bytes::Read_Be32(bytes.data() + 4) != expected[1]) {
            return Found::Invalid;
        }
    }

    return found;
}

bool Same_Source(const Node& left, const Node& right) {
    return left.source == right.source && left.span == right.span && std::equal(left.original.begin(), left.original.begin() + left.span, right.original.begin());
}

bool Walk(const Node& head, std::vector<Node>& nodes) {
    std::unordered_set<Guest::uptr> visited;
    Node node = head;
    for (;;) {
        if (!node.linked || !Same_Source(head, node) || node.chain != head.chain ||
            node.preserveChain != head.preserveChain || !visited.insert(node.entry).second) {
            return false;
        }

        nodes.push_back(node);
        if (node.next == 0) {
            return true;
        }

        if (Read_Node(node.next, node) != Found::Present) {
            return false;
        }
    }
}

struct Change {
    Guest::uptr address;
    std::vector<u8> bytes;
    std::vector<u8> previous;
};

bool Apply(std::vector<Change>& changes) {
    for (Change& change : changes) {
        change.previous.resize(change.bytes.size());
        if (!Read_Code(change.address, change.previous)) {
            return false;
        }
    }

    for (usize index = 0; index < changes.size(); index++) {
        if (changes[index].bytes != changes[index].previous &&
            !Write_Code(changes[index].address, changes[index].bytes)) {
            for (usize rollback = index + 1; rollback != 0; rollback--) {
                Write_Code(changes[rollback - 1].address, changes[rollback - 1].previous);
            }
            return false;
        }
    }
    return true;
}

std::vector<u8> Jump(Guest::uptr source, Guest::uptr target) {
    u32 words[2];
    if (!Detour_Words(source, target, words)) {
        return {};
    }
    std::vector<u8> bytes(sizeof(words));
    Bytes::Write_Be32(bytes.data(), words, 2);
    return bytes;
}

std::vector<u8> Source_Jump(const Node& node, Guest::uptr target) {
    u32 words[2];
    if (!Source_Words(node, node.source, target, words)) {
        return {};
    }
    std::vector<u8> bytes(sizeof(words));
    Bytes::Write_Be32(bytes.data(), words, 2);
    return bytes;
}

std::vector<u8> Continuation(const Node& node, Guest::uptr next) {
    if (next != 0) {
        return Jump(node.entry + node.payload, next);
    }

    if (!node.preserveChain) {
        return Jump(node.entry + node.payload, node.source + 8);
    }

    auto source = Original_Words(node);
    u32 words[gMaxRelocatedWords];
    u32 count = Relocate_Detour(source.data(), node.source, node.entry + node.payload, words);
    if (count == 0 || static_cast<u64>(node.payload) + count * 4 > node.capacity) {
        return {};
    }

    std::vector<u8> bytes(count * 4);
    Bytes::Write_Be32(bytes.data(), words, count);
    return bytes;
}

Node Description(const Patch_Code& code, Guest::usize capacity) {
    Node node{};
    node.source = code.Address();
    node.entry = code.Trampoline();
    node.capacity = capacity - Patch_Chain::Header_Size;
    node.payload = code.Payload_Size();
    node.preserve = code.Preservation() == Patch::Preserve::Source;
    node.preserveChain = node.preserve;
    auto original = code.Original_Bytes();
    node.span = code.Affected_Size();
    std::ranges::copy(original, node.original.begin());
    return node;
}

bool Update_Chain(std::vector<Node>& nodes, std::vector<Change>& changes) {
    bool preserve = std::ranges::all_of(nodes, &Node::preserve);
    for (usize index = 0; index < nodes.size(); index++) {
        Node& node = nodes[index];
        node.preserveChain = preserve;
        node.next = index + 1 < nodes.size() ? nodes[index + 1].entry : 0;
        std::vector<u8> continuation = Continuation(node, node.next);
        if (continuation.empty()) {
            return false;
        }
        changes.push_back({node.entry + node.payload, std::move(continuation), {}});
        changes.push_back({node.entry - Patch_Chain::Header_Size, Encode(node), {}});
    }

    const Node& head = nodes.front();
    std::vector<u8> jump = Source_Jump(head, head.entry);
    if (jump.empty()) {
        return false;
    }

    changes.push_back({head.source, std::move(jump), {}});
    return true;
}

} // namespace

struct Patch_Chain::Relocation {
    Patch_Code::Relocation code;
    std::vector<Change> changes;
    Guest::uptr storage;
    Guest::usize capacity;
};

Patch_Chain::~Patch_Chain() {
    Cancel_Relocation();
}

bool Patch_Chain::Capture(Guest::uptr source, Guest::usize proposed_span, std::vector<u8>& canonical) {
    canonical.clear();
    if ((source & 3) != 0 || proposed_span == 0 || (proposed_span & 3) != 0 ||
        static_cast<u64>(source) + proposed_span > (u64{1} << 32)) {
        return false;
    }

    Node head;
    Found found = Read_Head(source, head);
    if (found == Found::Invalid) {
        return false;
    }

    Guest::usize span = proposed_span;
    if (found == Found::Present) {
        canonical.assign(head.original.begin(), head.original.begin() + head.span);
        span = std::max(span, head.span);
    }
    else if (span == 8) {
        std::array<u8, 8> original{};
        if (!Read_Code(source, original)) {
            return false;
        }
        span = Preserved_Size(original, source);
    }

    u64 end = static_cast<u64>(source) + span;
    if (end > (u64{1} << 32)) {
        return false;
    }

    for (u64 candidate = source >= 8 ? source - 8 : 0; candidate < end; candidate += 4) {
        if (candidate == source) {
            continue;
        }
        Node neighbor;
        Found nearby = Read_Head(static_cast<Guest::uptr>(candidate), neighbor);
        if (nearby == Found::Invalid || (nearby == Found::Present &&
            Overlaps(source, span, neighbor.source, neighbor.span))) {
            return false;
        }
    }
    return true;
}

bool Patch_Chain::Bind(Patch_Code& prepared, Guest::uptr base, Guest::usize size) {
    if (enabled || size <= Header_Size || prepared.Trampoline() != base + Header_Size ||
        prepared.Affected_Size() > 12 || prepared.Affected_Size() < 8 ||
        static_cast<u64>(base) + size > (u64{1} << 32) ||
        Overlaps(base, size, prepared.Address(), prepared.Affected_Size())) {
        return false;
    }

    Node node = Description(prepared, size);
    if (static_cast<u64>(node.payload) + gMaxRelocatedWords * 4 > node.capacity) {
        return false;
    }

    std::vector<Change> changes{{base, Encode(node), {}}};
    if (!Apply(changes)) {
        return false;
    }

    code = &prepared;
    storage = base;
    capacity = size;
    return true;
}

bool Patch_Chain::Prepare_Relocation(Guest::uptr base, Guest::usize size) {
    Cancel_Relocation();
    if (code == nullptr || size <= Header_Size || static_cast<u64>(base) + size > (u64{1} << 32) ||
        Overlaps(base, size, code->Address(), code->Affected_Size()) ||
        !(enabled ? Enable() : Disable())) {
        return false;
    }

    Node own = Description(*code, capacity);
    Node head;
    Found found = Read_Head(own.source, head);
    if (found == Found::Invalid) {
        return false;
    }

    std::vector<Node> nodes;
    if (found == Found::Present && !Walk(head, nodes)) {
        return false;
    }

    auto position = std::find_if(nodes.begin(), nodes.end(), [&own](const Node& node) {
        return node.entry == own.entry;
    });

    bool linked = position != nodes.end();
    if (linked != enabled || (linked && !Same_Source(*position, own))) {
        return false;
    }

    for (const Node& node : nodes) {
        if (node.entry != own.entry && Overlaps(base, size, node.entry - Header_Size, node.capacity + Header_Size)) {
            return false;
        }
    }

    auto prepared = std::unique_ptr<Relocation>(new (std::nothrow) Relocation);
    if (prepared == nullptr || !code->Prepare_Relocation(base + Header_Size, size - Header_Size, prepared->code)) {
        return false;
    }

    if (linked) {
        own = *position;
    }
    own.entry = base + Header_Size;
    own.capacity = size - Header_Size;
    own.payload = prepared->code.payloadSize;
    if (static_cast<u64>(own.payload) + gMaxRelocatedWords * 4 > own.capacity) {
        return false;
    }

    std::vector<u8> destination(prepared->code.destination.begin(), prepared->code.destination.begin() + own.payload);
    std::vector<u8> continuation = Continuation(own, own.next);
    if (continuation.empty()) {
        return false;
    }

    destination.insert(destination.end(), continuation.begin(), continuation.end());
    prepared->changes.push_back({own.entry, std::move(destination), {}});
    prepared->changes.push_back({base, Encode(own), {}});
    if (linked) {
        if (position == nodes.begin()) {
            std::vector<u8> jump = Source_Jump(own, own.entry);
            if (jump.empty()) {
                return false;
            }
            prepared->changes.push_back({own.source, std::move(jump), {}});
        }
        else {
            Node previous = *(position - 1);
            previous.next = own.entry;
            std::vector<u8> jump = Continuation(previous, own.entry);
            if (jump.empty()) {
                return false;
            }
            prepared->changes.push_back({previous.entry + previous.payload, std::move(jump), {}});
            prepared->changes.push_back({previous.entry - Header_Size, Encode(previous), {}});
        }
    }

    if (base != storage && !Overlaps(storage, 4, base, size)) {
        prepared->changes.push_back({storage, std::vector<u8>(4), {}});
    }

    prepared->storage = base;
    prepared->capacity = size;
    relocation = prepared.release();
    return true;
}

bool Patch_Chain::Commit_Relocation() {
    if (relocation == nullptr) {
        return false;
    }

    std::unique_ptr<Relocation> prepared(std::exchange(relocation, nullptr));
    if (!Apply(prepared->changes)) {
        return false;
    }

    code->Commit_Relocation(std::move(prepared->code));
    storage = prepared->storage;
    capacity = prepared->capacity;
    return true;
}

void Patch_Chain::Cancel_Relocation() {
    delete std::exchange(relocation, nullptr);
}

bool Patch_Chain::Enable() {
    if (code == nullptr) {
        return false;
    }

    Node own = Description(*code, capacity);
    Node head;
    Found found = Read_Head(own.source, head);
    if (found == Found::Invalid || (found == Found::Present && !Same_Source(head, own))) {
        return false;
    }

    bool stale_source = found == Found::Present && !head.linked && head.entry == own.entry;
    if (found == Found::Present && !head.linked) {
        if (!stale_source) {
            return false;
        }
        found = Found::Missing;
    }

    Node current;
    if (found == Found::Present && Read_Node(own.entry, current) == Found::Present &&
        current.linked && current.chain == head.chain && Same_Source(current, own) &&
        current.payload == own.payload && current.capacity == own.capacity && current.preserve == own.preserve &&
        current.preserveChain == head.preserveChain) {
        if (current.next != 0) {
            Node next;
            if (Read_Node(current.next, next) != Found::Present || !next.linked ||
                next.chain != current.chain || next.preserveChain != current.preserveChain || !Same_Source(next, current)) {
                return false;
            }
        }
        auto compiled = code->Destination_Bytes();
        std::vector<u8> destination(std::from_range, compiled.first(own.payload));
        std::vector<u8> continuation = Continuation(current, current.next);
        if (continuation.empty()) {
            return false;
        }

        destination.insert(destination.end(), continuation.begin(), continuation.end());
        std::vector<Change> changes{{own.entry, std::move(destination), {}}};
        if (!Apply(changes)) {
            return false;
        }

        enabled = true;
        return true;
    }

    std::vector<u8> canonical;
    if (!Capture(own.source, own.span, canonical)) {
        return false;
    }

    std::vector<Node> nodes;
    if (found == Found::Present) {
        if (!Walk(head, nodes)) {
            return false;
        }

        for (const Node& node : nodes) {
            if (node.entry == own.entry) {
                enabled = true;
                return true;
            }

            if (Overlaps(storage, capacity, node.entry - Header_Size, node.capacity + Header_Size)) {
                return false;
            }
        }
        own.chain = head.chain;
    }
    else {
        std::array<u8, 12> original{};
        if (!stale_source && (!Read_Code(own.source, {original.data(), own.span}) ||
            !std::equal(original.begin(), original.begin() + own.span, own.original.begin()))) {
            return false;
        }

        if (sNextChain == 0) {
            return false;
        }

        if (sChainOwner == 0) {
            sChainOwner = Hypercall::Allocate();
            if (sChainOwner == 0) {
                return false;
            }
        }
        own.chain = (static_cast<u64>(sChainOwner) << 32) | sNextChain++;
    }

    own.linked = true;
    auto payload = code->Destination_Bytes().first(own.payload);
    std::vector<Change> changes{{own.entry, {payload.begin(), payload.end()}, {}}};
    nodes.insert(nodes.begin(), own);
    if (!Update_Chain(nodes, changes) || !Apply(changes)) {
        return false;
    }
    enabled = true;
    return true;
}

bool Patch_Chain::Disable() {
    if (code == nullptr) {
        return true;
    }

    Node own = Description(*code, capacity);
    Node head;
    Found found = Read_Head(own.source, head);
    if (found == Found::Invalid) {
        return false;
    }

    if (found == Found::Present && !head.linked) {
        if (head.entry != own.entry || !Same_Source(head, own)) {
            return false;
        }

        std::vector<Change> stale{{own.source, {own.original.begin(), own.original.begin() + 8}, {}}, {storage, Encode(own), {}}};
        if (!Apply(stale)) {
            return false;
        }
        enabled = false;
        return true;
    }

    Node current;
    if (Read_Node(own.entry, current) == Found::Present && !current.linked && Same_Source(current, own)) {
        enabled = false;
        return true;
    }

    std::vector<Change> changes;
    if (found == Found::Present) {
        std::vector<Node> nodes;
        if (!Walk(head, nodes)) {
            return false;
        }
        auto position = std::find_if(nodes.begin(), nodes.end(), [&own](const Node& node) {
            return node.entry == own.entry;
        });

        if (position != nodes.end()) {
            if (!Same_Source(head, own)) {
                return false;
            }
            nodes.erase(position);
            if (nodes.empty()) {
                changes.push_back({own.source, {own.original.begin(), own.original.begin() + 8}, {}});
            }
            else if (!Update_Chain(nodes, changes)) {
                return false;
            }
        }
    }

    changes.push_back({storage, Encode(own), {}});
    if (!Apply(changes)) {
        return false;
    }
    enabled = false;
    return true;
}

bool Patch_Chain::Destroy() {
    Cancel_Relocation();
    if (!Disable()) {
        return false;
    }

    if (code != nullptr) {
        std::vector<Change> changes{{storage, std::vector<u8>(4), {}}};
        if (!Apply(changes)) {
            return false;
        }
    }

    code = nullptr;
    storage = 0;
    capacity = 0;
    return true;
}

bool Patch_Chain::Is_Enabled() const {
    return enabled;
}

std::vector<u8> Patch_Chain::Destination_Bytes() const {
    Node node;
    if (code == nullptr || Read_Node(code->Trampoline(), node) != Found::Present) {
        return {};
    }

    std::vector<u8> continuation = Continuation(node, node.next);
    if (continuation.empty()) {
        return {};
    }
    
    std::vector<u8> bytes(node.payload + continuation.size());
    return Read_Code(node.entry, bytes) ? bytes : std::vector<u8>{};
}

} // namespace ModLoader::N64::Detail
