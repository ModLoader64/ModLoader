#include "module.h"

namespace {

u32 Create(Module& module, const u64* slots) {
    auto folder = module.Owner_Folder(slots[0], slots[1]);
    auto id = module.Text(slots[2], slots[3]);
    auto name = module.Text(slots[4], slots[5]);
    auto path = module.Text(slots[6], slots[7]);
    if (!folder || !id || !name || !path) {
        return 0;
    }

    auto resolved = Module_Mounted_Path(module, *folder, *path);
    if (!resolved) {
        return 0;
    }

    return module.runtime.textures.Create(&module, *folder, std::string(*id), std::string(*name), std::string(*path), std::move(*resolved), slots[8] != 0);
}

Texture_Manager* Manager(wasm_exec_env_t exec_env, u32 handle) {
    Module& module = Module_Of(exec_env);
    return Require_Emulation_Thread(exec_env, "Textures::Source") && module.runtime.textures.Owns(module, handle)
        ? &module.runtime.textures : nullptr;
}

NativeSymbol sNatives[] = {
    Native("texture_source_create", "(IIIIIIIIi)i", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Require_Emulation_Thread(exec_env, "Textures::Source::Create") ? Create(Module_Of(exec_env), slots) : 0;
    }),
    Native("texture_source_enable", "(ii)i", [](wasm_exec_env_t exec_env, u64* slots) {
        u32 handle = static_cast<u32>(slots[0]);
        Texture_Manager* manager = Manager(exec_env, handle);
        slots[0] = manager != nullptr && manager->Enable(handle, slots[1] != 0);
    }),
    Native("texture_source_reload", "(i)i", [](wasm_exec_env_t exec_env, u64* slots) {
        u32 handle = static_cast<u32>(slots[0]);
        Texture_Manager* manager = Manager(exec_env, handle);
        slots[0] = manager != nullptr && manager->Reload(handle);
    }),
    Native("texture_source_destroy", "(i)", [](wasm_exec_env_t exec_env, u64* slots) {
        u32 handle = static_cast<u32>(slots[0]);
        if (Texture_Manager* manager = Manager(exec_env, handle)) {
            manager->Destroy(handle);
        }
    }),
    Native("texture_source_state", "(i)i", [](wasm_exec_env_t exec_env, u64* slots) {
        u32 handle = static_cast<u32>(slots[0]);
        Texture_Manager* manager = Manager(exec_env, handle);
        slots[0] = manager != nullptr ? manager->State(handle) : MODLOADER_TEXTURE_DISABLED;
    }),
    Native("texture_source_enabled", "(i)i", [](wasm_exec_env_t exec_env, u64* slots) {
        u32 handle = static_cast<u32>(slots[0]);
        Texture_Manager* manager = Manager(exec_env, handle);
        slots[0] = manager != nullptr && manager->Is_Enabled(handle);
    }),
};

} // namespace

void Textures_Register_Natives() {
    Register_Natives(sNatives, "texture sources");
}
