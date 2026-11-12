#include "module.h"

#include "assembler.h"
#include "disassembler.h"

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

} // namespace

void N64_Tools_Register_Natives() {
    Register_Natives(sTools, "N64 tools");
}


