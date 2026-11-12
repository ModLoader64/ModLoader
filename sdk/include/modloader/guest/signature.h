#pragma once

#include <modloader/types.h>

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ModLoader::Guest {

class Signature {
public:
    Signature() = default;
    // "27 BD ?? ??"
    explicit Signature(std::string_view pattern);
    explicit Signature(std::span<const u8> bytes, std::span<const u8> mask = {});

    bool Is_Valid() const {
        return !bytes.empty();
    }

    usize Size() const {
        return bytes.size();
    }

    // Processor IDs come from Machine::Processor_Describe
    std::optional<u64> Find(u64 from, u64 to, u32 alignment = 1, u32 processor = 0) const;
    std::optional<u64> Find_Unique(u64 from, u64 to, u32 alignment = 1, u32 processor = 0) const;

private:
    std::optional<u64> Scan(u64 from, u64 to, u32 alignment, u32 processor, bool unique) const;

    std::vector<u8> bytes;
    std::vector<u8> mask;
};

} // namespace ModLoader::Guest
