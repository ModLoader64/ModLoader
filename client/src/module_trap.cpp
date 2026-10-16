#include "module.h"
#include "base/json.h"

namespace {

struct Trap_Frame {
    std::string text;
    u32 offset;
};

Json_Document Symbolize(const Module& module, std::span<const Trap_Frame> frames) {
#if defined(_WIN32)
    const char* executable = "llvm-symbolizer.exe";
#else
    const char* executable = "llvm-symbolizer";
#endif
    std::string symbolizer = Path_Join(Path_Executable_Directory(), executable);
    if (!File_Exists(symbolizer)) {
        return {};
    }

    std::vector<std::string> arguments = {
        "--output-style=JSON", "--inlines", "--skip-line-zero", "--obj=" + module.wasmPath
    };
    for (const auto& frame : frames) {
        arguments.push_back(Text_Format("0x%x", frame.offset));
    }

    Temporary_Directory temporary;
    if (!temporary.Create(Path_Join(Path_Directory(module.wasmPath), Text_Format(".trace-%u-%llu", Process_Current_Id(),
        static_cast<unsigned long long>(Thread_Current_Id()))))) {
        return {};
    }

    std::string output = Path_Join(temporary.Path(), "symbols.json");
    if (Process_Run(symbolizer, arguments, output) != 0) {
        return {};
    }
    return Json_Document(yyjson_read_file(output.c_str(), 0, nullptr, nullptr));
}

void Log_Stack(Module& module, wasm_exec_env_t context) {
    u32 size = wasm_runtime_get_call_stack_buf_size(context);
    std::string stack(size, '\0');
    if (size == 0 || wasm_runtime_dump_call_stack_to_buf(context, stack.data(), size) == 0) {
        return;
    }

    std::vector<Trap_Frame> frames;
    std::string_view lines(stack);
    while (!lines.empty()) {
        usize end = lines.find('\n');
        std::string line(lines.substr(0, end));
        u32 index, offset;
        if (sscanf(line.c_str(), "#%u: 0x%x", &index, &offset) == 2) {
            frames.push_back({std::move(line), offset});
        }
        lines.remove_prefix(end == std::string_view::npos ? lines.size() : end + 1);
    }
    if (frames.empty()) {
        return;
    }

    Json_Document symbols = Symbolize(module, frames);
    for (usize index = 0; index < frames.size(); ++index) {
        auto* resolved = yyjson_obj_get(yyjson_arr_get(yyjson_doc_get_root(symbols.get()), index), "Symbol");
        bool found = false;
        usize inline_index, count;
        yyjson_val* location;
        yyjson_arr_foreach(resolved, inline_index, count, location) {
            const char* file = yyjson_get_str(yyjson_obj_get(location, "FileName"));
            const char* function = yyjson_get_str(yyjson_obj_get(location, "FunctionName"));
            u32 line = static_cast<u32>(yyjson_get_uint(yyjson_obj_get(location, "Line")));
            u32 column = static_cast<u32>(yyjson_get_uint(yyjson_obj_get(location, "Column")));
            if (file != nullptr && file[0] != '\0' && line != 0) {
                Log_Error(module.name.c_str(), "  #%zu %s at %s:%u:%u%s", index,
                    function != nullptr && function[0] != '\0' ? function : frames[index].text.c_str(), file, line, column,
                    inline_index + 1 < count ? " (inlined)" : "");
                found = true;
            }
        }
        if (!found) {
            Log_Error(module.name.c_str(), "  %s", frames[index].text.c_str());
        }
    }
}

} // namespace

void Module::Trap(wasm_exec_env_t context, const char* what, bool stack) {
    wasm_module_inst_t trapped = wasm_runtime_get_module_inst(context);
    const char* exception = wasm_runtime_get_exception(trapped);
    disabled = true;
    Log_Error(name.c_str(), "disabled after %s: %s", what, exception != nullptr ? exception : "unknown trap");
    if (stack) {
        Log_Stack(*this, context);
    }
    wasm_runtime_clear_exception(trapped);
}
