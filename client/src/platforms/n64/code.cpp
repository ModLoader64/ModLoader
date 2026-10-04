#include "n64.h"

#include "assembler.h"
#include "disassembler.h"
#include "signature.h"

#include <string.h>

namespace {

bool Resolve_Symbol(void* user, const char* name, u32 name_length, u32* out_value) {
    std::optional<u64> address = static_cast<const Runtime*>(user)->Symbol_Find(std::string_view(name, name_length));
    if (!address || *address > UINT32_MAX) {
        return false;
    }

    *out_value = static_cast<u32>(*address);
    return true;
}

bool Require_N64(wasm_exec_env_t exec_env, const char* what) {
    if (!Require_Emulation_Thread(exec_env, what)) {
        return false;
    }

    if (Module_Of(exec_env).runtime.platform.identifier != "n64") {
        wasm_runtime_set_exception(wasm_runtime_get_module_inst(exec_env), "Platform not N64");
        return false;
    }

    return true;
}

s64 Assemble(Module& module, u32 origin, u64 source, u64 source_length, u64 output, u64 capacity, u64 error, u64 error_capacity) {
    std::optional<std::string_view> text = module.Text(source, source_length);
    char* error_text = error_capacity != 0 ? module.Memory_As<char>(error, error_capacity) : nullptr;
    u8* destination;
    N64::Assembler assembler(Resolve_Symbol, &module.runtime);
    std::string copy;
    s64 size;

    if (!text) {
        return -1;
    }

    copy = *text;
    if (!assembler.Assemble(copy.c_str(), origin)) {
        if (error_text != nullptr) {
            Text_Copy({ error_text, error_capacity }, Text_Format("line %u: %s", assembler.Error_Line(), assembler.Error()));
        }
        return -1;
    }

    size = assembler.Size();
    destination = size != 0 && static_cast<u64>(size) <= capacity ? module.Memory_As<u8>(output, capacity) : nullptr;
    if (destination != nullptr) {
        memcpy(destination, assembler.Data(), assembler.Size());
    }

    return size != 0 && static_cast<u64>(size) <= capacity && destination == nullptr ? -1 : size;
}

// Space 0: CPU RAM; space 1: CPU/PI ROM
u32 Sig_Scan(Module& module, u32 space, u64 pattern, u64 length, u32 from, u32 to, bool unique = false) {
    N64_Platform& n64 = static_cast<N64_Platform&>(module.runtime.platform);
    std::optional<std::string_view> text = module.Text(pattern, length);
    const ModLoader_Space_Descriptor* searched = space == 0 ? &module.runtime.Ram() : space == 1 ? n64.Rom() : nullptr;
    u32 mask = space == 0 ? 0x1FFFFFFFu : 0x0FFFFFFFu;
    u32 base = space == 0 ? gKseg0 : gRomCpu;
    N64::Signature signature;
    u32 found;
    if (!text || searched == nullptr) {
        return 0;
    }

    std::string copy(*text);
    if (!signature.Parse(copy.c_str())) {
        return 0;
    }

    u32 start = from & mask;
    u64 end = to != 0 && (to & mask) < searched->usableSize ? to & mask : searched->usableSize;
    if (start >= end || end > UINT32_MAX) {
        return 0;
    }

    u64 view_size = space == 0 ? (end + 3) & ~3ull : end;
    if (view_size > searched->usableSize || view_size > UINT32_MAX || view_size > static_cast<u64>(UINT32_MAX) + 1 - base) {
        return 0;
    }

    N64::Memory_View view = { searched->hostBase, base, static_cast<u32>(view_size), space == 0 };
    if (!signature.Find(view, &found, start) || static_cast<u64>(found - base) + signature.Size() > end) {
        return 0;
    }

    u32 another;
    u64 next = static_cast<u64>(found - base) + 4;
    if (unique && next < end && signature.Find(view, &another, static_cast<u32>(next)) &&
        static_cast<u64>(another - base) + signature.Size() <= end) {
        return 0;
    }
    
    return found;
}

NativeSymbol sTools[] = {
    Native("asm_assemble", "(IIIIIII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = static_cast<u64>(Assemble(Module_Of(exec_env), static_cast<u32>(slots[0]), slots[1], slots[2], slots[3], slots[4], slots[5], slots[6]));
    }),
    Native("asm_disassemble", "(iIII)i", [](wasm_exec_env_t exec_env, u64* slots) {
        u64 capacity = slots[3] < 256 ? slots[3] : 256;
        char* out = capacity != 0 ? Module_Of(exec_env).Memory_As<char>(slots[2], capacity) : nullptr;
        char text[256];

        N64::Disassembler instruction(static_cast<u32>(slots[0]), static_cast<u32>(slots[1]));
        slots[0] = instruction.Format(text, sizeof(text));
        if (out != nullptr) {
            Text_Copy({ out, capacity }, text);
        }
    }),
};

NativeSymbol sNatives[] = {
    Native("sig_scan", "(iIIII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Require_N64(exec_env, "ModLoader::N64::Signature")
            ? Sig_Scan(Module_Of(exec_env), static_cast<u32>(slots[0]), slots[1], slots[2], static_cast<u32>(slots[3]), static_cast<u32>(slots[4])) : 0;
    }),
    Native("sig_scan_unique", "(iIIII)I", [](wasm_exec_env_t exec_env, u64* slots) {
        slots[0] = Require_N64(exec_env, "ModLoader::N64::Signature::Find_Unique")
            ? Sig_Scan(Module_Of(exec_env), static_cast<u32>(slots[0]), slots[1], slots[2], static_cast<u32>(slots[3]), static_cast<u32>(slots[4]), true) : 0;
    }),
};

} // namespace

void N64_Platform::Register_Natives() {
    ::Register_Natives(sNatives, "N64");
}

void N64_Tools_Register_Natives() {
    Register_Natives(sTools, "N64 tools");
}


