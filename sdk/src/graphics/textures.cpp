#include "../listener_list.h"

#include <modloader/graphics/textures.h>

#include <utility>

using namespace ModLoader;

namespace {

[[clang::no_destroy]] Runtime::Listener_List<Textures::Source_Change> sChanges({MODLOADER_EVENT_TEXTURE_SOURCE});

} // namespace

Textures::Source::~Source() {
    Destroy();
}

Textures::Source::Source(Source&& other) noexcept : handle(std::exchange(other.handle, 0)) {
}

Textures::Source& Textures::Source::operator=(Source&& other) noexcept {
    if (this != &other) {
        Destroy();
        handle = std::exchange(other.handle, 0);
    }
    return *this;
}

bool Textures::Source::Create_In(std::string_view folder, std::string_view id, std::string_view name, std::string_view path,
    bool enabled) {
    if (handle != 0) {
        return false;
    }

    handle = ModLoader_Host_Texture_Source_Create(folder.data(), folder.size(), id.data(), id.size(), name.data(), name.size(), path.data(), path.size(), enabled);
    return handle != 0;
}

bool Textures::Source::Enable() {
    return ModLoader_Host_Texture_Source_Enable(handle, 1) != 0;
}

bool Textures::Source::Disable() {
    return ModLoader_Host_Texture_Source_Enable(handle, 0) != 0;
}

bool Textures::Source::Reload() {
    return ModLoader_Host_Texture_Source_Reload(handle) != 0;
}

void Textures::Source::Destroy() {
    if (u32 retired = std::exchange(handle, 0)) {
        ModLoader_Host_Texture_Source_Destroy(retired);
    }
}

Textures::State Textures::Source::State() const {
    return static_cast<Textures::State>(ModLoader_Host_Texture_Source_State(handle));
}

bool Textures::Source::Is_Enabled() const {
    return ModLoader_Host_Texture_Source_Enabled(handle) != 0;
}

Subscription Textures::Changed_Event::Subscribe(Function<void(const Source_Change&)> callback) const {
    return sChanges.Subscribe(std::move(callback));
}

void ModLoader::Runtime::Textures_Event(const void* bytes, u64 size) {
    if (size < sizeof(ModLoader_Texture_Event)) {
        return;
    }
    ModLoader_Texture_Event record;
    __builtin_memcpy(&record, bytes, sizeof(record));
    sChanges.Call({record.source, static_cast<Textures::State>(record.state), record.enabled != 0, record.priority});
}

void ModLoader::Runtime::Textures_Shutdown() {
    sChanges.Close();
}
