#pragma once

#include <functional>

namespace ModLoader {
// Owning, copyable callback
template<typename Signature>
using Function = std::function<Signature>;
} // namespace ModLoader
