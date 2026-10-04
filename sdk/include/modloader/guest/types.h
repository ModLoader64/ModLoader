#pragma once

#include <modloader/types.h>

#include <type_traits>

#if defined(MODLOADER_PLATFORM_SPACES_HEADER)
#include MODLOADER_PLATFORM_SPACES_HEADER
#else
#error "no ModLoader platform"
#endif

namespace ModLoader::Guest {

// Unsigned integer with a pointer's address width, ex: Address_Of<space<rdram> void*>
// space<S> selects guest layout and byte order; addrspace_cast converts between spaces
template<typename Pointer>
    requires (std::is_pointer_v<Pointer> && (sizeof(Pointer) == 4 || sizeof(Pointer) == 8))
using Address_Of = std::conditional_t<sizeof(Pointer) == 4, u32, u64>;

using uptr = Address_Of<space<cpu> void*>; // Default CPU address type
using usize = uptr; // Byte counts and offsets in the default CPU space

} // namespace ModLoader::Guest

