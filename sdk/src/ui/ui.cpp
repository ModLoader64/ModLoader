#include "../internal.h"
#include "../listener_list.h"

#include <modloader/ui/ui.h>

namespace {
struct Ui_Frame {};
[[clang::no_destroy]] ModLoader::Runtime::Listener_List<Ui_Frame> sFrames({MODLOADER_EVENT_UI});
} // namespace

ModLoader::Subscription ModLoader::Ui::Subscribe(Function<void()> frame) {
    if (!frame) {
        return {};
    }
    return sFrames.Subscribe([frame = std::move(frame)](const Ui_Frame&) { frame(); });
}

void ModLoader::Runtime::Ui_Shutdown() {
    sFrames.Close();
}

void ModLoader::Runtime::Ui_Frames() {
    sFrames.Call({});
}
ImTextureID ModLoader::Ui::Texture_Create(u32 width, u32 height, const void* pixels) {
    return ModLoader_Host_Texture_Create(width, height, pixels);
}

bool ModLoader::Ui::Texture_Update(ImTextureID texture, const void* pixels) {
    return ModLoader_Host_Texture_Update(texture, pixels) != 0;
}

void ModLoader::Ui::Texture_Destroy(ImTextureID texture) {
    ModLoader_Host_Texture_Destroy(texture);
}

ModLoader::Ui::Window ModLoader::Ui::Window::Create(std::string_view title, u32 width, u32 height) {
    if (title.empty()) {
        title = " ";
    }
    return Window(ModLoader_Host_Window_Create(title.data(), title.size(), width, height));
}

ModLoader::Ui::Window ModLoader::Ui::Window::Overlay() {
    return Window(ModLoader_Host_Window_Create(nullptr, 0, 0, 0));
}

void ModLoader::Ui::Window::Destroy() {
    ModLoader_Host_Window_Destroy(handle);
    handle = 0;
}

void ModLoader::Ui::Window::Show(bool shown) const {
    ModLoader_Host_Window_Show(handle, shown ? 1 : 0);
}

bool ModLoader::Ui::Window::Shown() const {
    return ModLoader_Host_Window_Shown(handle) != 0;
}

