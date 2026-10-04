#pragma once

namespace ModLoader::Guest {

// Common lifetime controls for platform patches, ex: N64::Patch
// Creation starts disabled; Disable restores original code
class Patch {
public:
    virtual ~Patch() = default;
    virtual bool Enable() = 0;
    virtual bool Disable() = 0;
    virtual bool Destroy() = 0;
    virtual bool Is_Created() const = 0;
    virtual bool Is_Enabled() const = 0;
};

} // namespace ModLoader::Guest
