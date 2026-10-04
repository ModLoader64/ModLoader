#!/usr/bin/env python3
# TODO: NOT THIS
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HEADER = ROOT / "third_party" / "imgui" / "imgui.h"
CLIENT_OUTPUT = ROOT / "client" / "src" / "ui" / "imgui_natives.cpp"
SDK_OUTPUT = ROOT / "sdk" / "src" / "ui" / "imgui_functions.cpp"

# What modloader_imgui_config.h defines
DEFINED = {"IMGUI_DISABLE_OBSOLETE_FUNCTIONS", "ImDrawIdx"}
SCOPES = ("ImGui", "ImDrawList")

MANUAL = {
    "ImGui::GetCurrentContext", "ImGui::SetCurrentContext", "ImGui::GetIO", "ImGui::GetStyle",
    "ImGui::GetStyleColorVec4", "ImGui::TableGetSortSpecs", "ImGui::AcceptDragDropPayload",
    "ImGui::GetDragDropPayload", "ImGui::DebugCheckVersionAndDataLayout", "ImGui::MemAlloc", "ImGui::MemFree",
}

EXCLUDED = {
    "ImGui::CreateContext", "ImGui::DestroyContext", "ImGui::GetPlatformIO", "ImGui::NewFrame", "ImGui::EndFrame",
    "ImGui::Render", "ImGui::GetDrawData", "ImGui::GetFontBaked", "ImGui::BeginMultiSelect", "ImGui::EndMultiSelect",
    "ImGui::GetDrawListSharedData", "ImGui::SetStateStorage", "ImGui::GetStateStorage",
    "ImGui::LoadIniSettingsFromDisk", "ImGui::LoadIniSettingsFromMemory", "ImGui::SaveIniSettingsToDisk",
    "ImGui::SaveIniSettingsToMemory", "ImGui::LogToFile", "ImGui::LogButtons", "ImGui::SetAllocatorFunctions",
    "ImGui::GetAllocatorFunctions", "ImGui::UpdatePlatformWindows", "ImGui::RenderPlatformWindowsDefault",
    "ImGui::DestroyPlatformWindows", "ImGui::FindViewportByPlatformHandle", "ImDrawList::AddCallback",
    "ImDrawList::CloneOutput", "ImDrawList::PrimReserve", "ImDrawList::PrimUnreserve", "ImDrawList::PrimRect",
    "ImDrawList::PrimRectUV", "ImDrawList::PrimQuadUV",
}

HOST_CALLS = {
    "ImGui::TextLinkOpenURL": "Imgui_Text_Link_Open_Url({arguments})",
    "ImGui::PushClipRect": "Imgui_Push_Clip_Rect(nullptr, {arguments})",
    "ImGui::PopClipRect": "Imgui_Pop_Clip_Rect(&call, nullptr)",
    "ImDrawList::PushClipRect": "Imgui_Push_Clip_Rect(list, {arguments})",
    "ImDrawList::PushClipRectFullScreen": "Imgui_Push_Clip_Rect_Full_Screen(list)",
    "ImDrawList::PopClipRect": "Imgui_Pop_Clip_Rect(&call, list)",
    "ImDrawList::PushTexture": "Imgui_Push_Texture(list, {arguments})",
    "ImDrawList::PopTexture": "Imgui_Pop_Texture(&call, list)",
}

GUARDS = {
    "ImGui::End": "Imgui_Guard_Window(&call, 0)",
    "ImGui::EndChild": "Imgui_Guard_Window(&call, ImGuiWindowFlags_ChildWindow)",
    "ImGui::EndListBox": "Imgui_Guard_Window(&call, ImGuiWindowFlags_ChildWindow)",
    "ImGui::EndTooltip": "Imgui_Guard_Window(&call, ImGuiWindowFlags_Tooltip)",
    "ImGui::EndPopup": "Imgui_Guard_Window(&call, ImGuiWindowFlags_Popup)",
    "ImGui::EndMenuBar": "Imgui_Guard_Menu_Bar(&call)",
    "ImGui::EndGroup": "Imgui_Guard_Group(&call)",
    "ImGui::TreePop": "Imgui_Guard_Tree(&call)",
    "ImGui::EndTabItem": "Imgui_Guard_Tab_Item(&call)",
    "ImGui::EndDragDropSource": "Imgui_Guard_Drag_Drop(&call, true)",
    "ImGui::EndDragDropTarget": "Imgui_Guard_Drag_Drop(&call, false)",
    "ImGui::BeginTable": "Imgui_Guard_Range(&call, {columns}, 1, IMGUI_TABLE_MAX_COLUMNS)",
    "ImGui::TableNextRow": "Imgui_Guard_Table(&call)",
    "ImGui::TableAngledHeadersRow": "Imgui_Guard_Table(&call)",
    "ImGui::TableHeader": "Imgui_Guard_Table_Column(&call, -1, true)",
    "ImGui::TableSetColumnIndex": "Imgui_Guard_Table_Set_Column(&call, {column_n})",
    "ImGui::TableGetColumnName": "Imgui_Guard_Table_Column(&call, {column_n}, false)",
    "ImGui::TableGetColumnFlags": "Imgui_Guard_Table_Column(&call, {column_n}, false)",
    "ImGui::TableSetColumnEnabled": "Imgui_Guard_Table_Column(&call, {column_n}, false)",
    "ImGui::TableSetBgColor": "Imgui_Guard_Table_Background(&call, {target}, {column_n})",
    "ImGui::TableSetupScrollFreeze": "Imgui_Guard_Table_Freeze(&call, {cols}, {rows})",
    "ImGui::Columns": "Imgui_Guard_Range(&call, {count}, 1, 65)",
    "ImGui::GetColumnWidth": "Imgui_Guard_Column(&call, {column_index}, false)",
    "ImGui::GetColumnOffset": "Imgui_Guard_Column(&call, {column_index}, false)",
    "ImGui::SetColumnWidth": "Imgui_Guard_Column(&call, {column_index}, true)",
    "ImGui::SetColumnOffset": "Imgui_Guard_Column(&call, {column_index}, true)",
}

TYPE_GUARDS = {
    "ImGuiCol": "Imgui_Guard_Range(&call, {value}, 0, ImGuiCol_COUNT)",
    "ImGuiStyleVar": "Imgui_Guard_Range(&call, {value}, 0, ImGuiStyleVar_COUNT)",
    "ImGuiMouseButton": "Imgui_Guard_Range(&call, {value}, 0, ImGuiMouseButton_COUNT)",
    "ImGuiMouseCursor": "Imgui_Guard_Range(&call, {value}, ImGuiMouseCursor_None, ImGuiMouseCursor_COUNT)",
    "ImGuiDir": "Imgui_Guard_Range(&call, {value}, ImGuiDir_None, ImGuiDir_COUNT)",
    "ImGuiTableBgTarget": "Imgui_Guard_Range(&call, {value}, ImGuiTableBgTarget_RowBg0, ImGuiTableBgTarget_CellBg + 1)",
    "ImGuiKey": "Imgui_Guard_Key(&call, {value})",
    "ImGuiKeyChord": "Imgui_Guard_Key_Chord(&call, {value})",
}

LIMITS = {
    "num_segments": "std::clamp({value}, 0, 512)",
    "a_min_of_12": "std::clamp({value}, -1200, 1200)",
    "a_max_of_12": "std::clamp({value}, -1200, 1200)",
}

REQUIRED_STRINGS = {
    "label", "name", "str_id", "text", "text_begin", "str_id_begin", "prefix", "type", "desc_id",
    "tab_or_docked_window_label", "hint", "items_separated_by_zeros",
}

NULLABLE = {
    ("ImGui::TableSetupColumn", "label"), ("ImGui::TableHeader", "label"), ("ImGui::CollapsingHeader", "p_visible"),
}

BASES = {
    "bool": "bool", "float": "f32", "double": "f64", "int": "i32", "signed int": "i32", "unsigned int": "i32",
    "short": "i32", "signed short": "i32", "unsigned short": "i32", "char": "i32", "signed char": "i32",
    "unsigned char": "i32", "long long": "i64", "signed long long": "i64", "unsigned long long": "i64",
    "size_t": "i64",
}

WIRE = {"bool": "i", "i32": "i", "i64": "I", "f32": "f", "f64": "F"}

SLOTS = {"vec4": 2, "style": 0, "callback": 0, "callback_data": 0, "varargs": 0, "valist": 0}

TYPEDEF = re.compile(r"^typedef\s+(?P<base>(?:\w+\s+)*?\w+)\s+(?P<name>\w+)\s*;$")
FUNCTION_POINTER = re.compile(r"^typedef\s+[^;]*\(\s*\*\s*(?P<name>\w+)\s*\)\s*\(")
ENUM = re.compile(r"^enum\s+(?P<name>\w+)\s*:\s*(?P<base>\w+)")
DECLARATION = re.compile(
    r"^IMGUI_API\s+(?P<return>.*?[\s*&])(?P<name>\w+)\s*\((?P<parameters>.*?)\)\s*(?P<const>const)?\s*"
    r"(?:IM_FMTARGS\(\d+\)|IM_FMTLIST\(\d+\))?\s*;$"
)

LINE_LIMIT = 160

def fail(message):
    sys.exit(f"generate_imgui_bindings.py: {message}")


def strip_comment(line):
    quoted = False
    for index in range(len(line) - 1):
        if line[index] == '"' and (index == 0 or line[index - 1] != "\\"):
            quoted = not quoted
        elif not quoted and line[index:index + 2] == "//":
            return line[:index]
    return line


def normalize(text):
    return re.sub(r"\s+", " ", text).replace(" *", "*").replace(" &", "&").strip()


def evaluate(expression):
    expression = strip_comment(expression)
    expression = re.sub(r"defined\s*\(\s*(\w+)\s*\)", lambda match: str(match.group(1) in DEFINED), expression)
    expression = expression.replace("&&", " and ").replace("||", " or ").replace("!", " not ")
    try:
        return bool(eval(expression, {"__builtins__": {}}, {}))
    except Exception:
        return False


def active_lines(text):
    stack = []
    for line in text.splitlines():
        directive = line.strip()
        if directive.startswith("#ifdef"):
            value = directive.split()[1] in DEFINED
            stack.append([value, value])
        elif directive.startswith("#ifndef"):
            value = directive.split()[1] not in DEFINED
            stack.append([value, value])
        elif directive.startswith("#if"):
            value = evaluate(directive[3:])
            stack.append([value, value])
        elif directive.startswith("#elif"):
            stack[-1][0] = not stack[-1][1] and evaluate(directive[5:])
            stack[-1][1] = stack[-1][1] or stack[-1][0]
        elif directive.startswith("#else"):
            stack[-1][0] = not stack[-1][1]
            stack[-1][1] = True
        elif directive.startswith("#endif"):
            stack.pop()
        elif all(entry[0] for entry in stack):
            yield line


def split_parameters(text):
    parameters = []
    depth = 0
    current = ""
    for character in text:
        if character in "([<":
            depth += 1
        elif character in ")]>":
            depth -= 1
        if character == "," and depth == 0:
            parameters.append(current.strip())
            current = ""
        else:
            current += character
    if current.strip():
        parameters.append(current.strip())
    return parameters


class Types:
    def __init__(self, lines):
        self.typedefs = {}
        self.callbacks = set()
        for line in lines:
            line = strip_comment(line).strip()
            if (match := FUNCTION_POINTER.match(line)) is not None:
                self.callbacks.add(match.group("name"))
            elif (match := TYPEDEF.match(line)) is not None:
                self.typedefs[match.group("name")] = normalize(match.group("base"))
            elif (match := ENUM.match(line)) is not None:
                self.typedefs[match.group("name")] = match.group("base")

    def scalar(self, name):
        while name in self.typedefs:
            name = self.typedefs[name]
        return BASES.get(name)


class Parameter:
    def __init__(self, text):
        self.default = None
        depth = 0
        for index, character in enumerate(text):
            if character in "([<":
                depth += 1
            elif character in ")]>":
                depth -= 1
            elif character == "=" and depth == 0:
                self.default = text[index + 1:].strip()
                text = text[:index].strip()
                break
        self.declaration = text
        self.array = None
        self.function_pointer = re.search(r"\(\s*\*\s*\w+\s*\)", text) is not None
        if text == "...":
            self.type = self.name = "..."
        elif self.function_pointer:
            self.type = text
            self.name = re.search(r"\(\s*\*\s*(\w+)\s*\)", text).group(1)
        else:
            match = re.match(r"^(?P<type>.*?)\s*\b(?P<name>\w+)\s*(?P<array>\[\w*\])?$", text)
            if match is None or not match.group("type"):
                fail(f"cannot read the parameter '{text}'")
            self.type = normalize(match.group("type"))
            self.name = match.group("name")
            self.array = match.group("array")

    def pointee(self, types):
        if self.array is not None:
            return self.type, int(self.array[1:-1])
        if not self.type.endswith(("*", "&")):
            return None
        element = self.type[:-1]
        base = element.replace("const ", "")
        if types.scalar(base) is None and base not in ("ImVec2", "ImVec4", "ImGuiWindowClass"):
            return None
        return element, 4 if self.name == "ref_col" else 1


class Function:
    def __init__(self, scope, match):
        text = match.group("parameters").strip()
        self.scope = scope
        self.name = match.group("name")
        self.qualified = f"{scope}::{self.name}"
        self.returns = normalize(match.group("return"))
        self.const = match.group("const") is not None
        self.parameters = [Parameter(item) for item in split_parameters(text)] if text not in ("", "void") else []
        self.valist = bool(self.parameters) and self.parameters[-1].type == "va_list"
        self.import_name = None
        self.kinds = None
        self.return_kind = None


def declarations(lines):
    scope = None
    for line in lines:
        code = strip_comment(line)
        if line.startswith("namespace ImGui"):
            scope = "ImGui"
        elif (match := re.match(r"^struct (\w+)\s*$", code)) is not None:
            scope = match.group(1)
        elif line.startswith("}"):
            scope = None
        elif scope in SCOPES and code.lstrip().startswith("IMGUI_API"):
            yield scope, code.strip()


def parameter_kind(function, index, types):
    parameters = function.parameters
    parameter = parameters[index]
    following = parameters[index + 1] if index + 1 < len(parameters) else None
    previous = parameters[index - 1] if index > 0 else None
    kind = parameter.type
    if parameter.name == "...":
        return "varargs"
    if kind == "va_list":
        return "valist"
    if following is not None and (following.name == "..." or following.type == "va_list"):
        return "format"
    if kind in types.callbacks:
        return "callback"
    if previous is not None and previous.type in types.callbacks and kind == "void*":
        return "callback_data"
    if kind == "const char*":
        if parameter.name.endswith("_end"):
            return "text_end"
        return "zeros" if parameter.name == "items_separated_by_zeros" else "string"
    if kind == "const char* const" and parameter.array == "[]":
        return "strings"
    if kind == "char*" and following is not None and following.name == "buf_size":
        return "buffer"
    if kind in ("void*", "const void*"):
        named = {"p_data": "data", "p_min": "bound", "p_max": "bound", "p_step": "bound", "p_step_fast": "bound", "ptr_id": "id", "data": "sized"}
        if parameter.name in named:
            return named[parameter.name]
    if kind == "const float*" and parameter.name == "values":
        return "values"
    if kind == "const ImVec2*" and parameter.name == "points":
        return "points"
    if kind in ("ImVec2", "const ImVec2&"):
        return "vec2"
    if kind in ("ImVec4", "const ImVec4&"):
        return "vec4"
    if kind == "ImTextureRef":
        return "texture"
    if kind == "ImFont*":
        return "font"
    if kind in ("ImGuiViewport*", "const ImGuiViewport*"):
        return "viewport"
    if kind == "ImGuiStyle*":
        return "style"
    if parameter.pointee(types) is not None:
        return "pointer"
    scalar = types.scalar(kind)
    if scalar is None:
        fail(f"{function.qualified}: no way across for '{parameter.declaration}'")
    return scalar


def return_kind(function, types):
    named = {"void": "void", "ImVec2": "vec2", "ImVec4": "vec4", "const char*": "string", "ImDrawList*": "draw_list", "ImFont*": "font", "ImGuiViewport*": "viewport"}
    if function.returns in named:
        return named[function.returns]
    scalar = types.scalar(function.returns)
    if scalar is None:
        fail(f"{function.qualified}: no way back for '{function.returns}'")
    return scalar


def nullable(function, parameter):
    return parameter.default == "NULL" or (function.qualified, parameter.name) in NULLABLE


# (wire type, import name, guest argument).
def import_parameters(function, types):
    result = []
    if function.scope == "ImDrawList":
        result.append(("u64", "list", "Imgui::Draw_List_Handle(this)"))
    for parameter, kind in zip(function.parameters, function.kinds):
        name = parameter.name
        if kind in WIRE:
            result.append((parameter.type, name, name))
        elif kind == "vec2":
            result.append(("u64", name, f"Imgui::Pack({name})"))
        elif kind == "vec4":
            result.append(("u64", f"{name}_xy", f"Imgui::Pack_Low({name})"))
            result.append(("u64", f"{name}_zw", f"Imgui::Pack_High({name})"))
        elif kind in ("string", "text_end", "zeros"):
            result.append(("const char*", name, name))
        elif kind == "format":
            source = "args" if function.valist else "arguments"
            result.append(("const char*", "text", f"Imgui::Format({name}, {source})"))
        elif kind == "strings":
            result.append(("const char* const*", name, name))
        elif kind == "buffer":
            result.append(("char*", name, name))
        elif kind == "data":
            result.append(("void*", name, name))
        elif kind in ("bound", "sized", "id"):
            result.append(("const void*", name, name))
        elif kind == "values":
            result.append(("const float*", name, name))
        elif kind == "points":
            result.append(("const ImVec2*", name, name))
        elif kind == "texture":
            result.append(("u64", name, f"{name}._TexID"))
        elif kind == "font":
            result.append(("u64", name, f"Imgui::Font_Handle({name})"))
        elif kind == "viewport":
            result.append(("ImGuiID", name, f"Imgui::Viewport_Id({name})"))
        elif kind == "pointer":
            element = parameter.pointee(types)[0]
            result.append((f"{element}*", name, f"&{name}" if parameter.type.endswith("&") else name))
    if function.return_kind == "vec4":
        result.append(("ImVec4*", "result", "&result"))
    elif function.return_kind == "string":
        result.append(("char*", "buffer", "buffer"))
        result.append(("u64", "capacity", "capacity"))
    return result


def import_return(function):
    return {"void": "void", "vec2": "u64", "vec4": "void", "string": "u64", "draw_list": "u64", "font": "u64", "viewport": "ImGuiID"}.get(function.return_kind, function.returns)


def wire(type_name, types):
    if type_name.endswith("*") or type_name == "u64":
        return "I"
    return WIRE[types.scalar(type_name)]


def module_definition(function, types):
    parameters = []
    for parameter, kind in zip(function.parameters, function.kinds):
        if kind in ("style", "callback", "callback_data"):
            parameters.append(parameter.type)
        else:
            parameters.append(parameter.declaration)
    arguments = ", ".join(expression for _, _, expression in import_parameters(function, types))
    call = f"{function.import_name}({arguments})"
    header = f"{function.returns} {function.qualified}({', '.join(parameters)}){' const' if function.const else ''} {{"
    declarations = []
    statements = []
    kind = function.return_kind
    varargs = "varargs" in function.kinds
    if varargs:
        declarations.append("va_list arguments;")
        statements.append(f"va_start(arguments, {function.parameters[-2].name});")
        if kind == "void":
            statements.append(f"{call};")
        else:
            declarations.append(f"{function.returns} result;")
            statements.append(f"result = {call};")
        statements.append("va_end(arguments);")
        if kind != "void":
            statements.append("return result;")
    elif kind == "void":
        statements.append(f"{call};")
    elif kind == "vec2":
        statements.append(f"return Imgui::Unpack({call});")
    elif kind == "vec4":
        declarations.append("ImVec4 result;")
        statements.append(f"{call};")
        statements.append("return result;")
    elif kind == "string":
        declarations.append("u64 capacity = 256;")
        declarations.append("char* buffer = Imgui::Scratch_Text(capacity);")
        declarations.append(f"u64 length = {call};")
        statements.append("if (length >= capacity) {")
        statements.append("    capacity = length + 1;")
        statements.append("    buffer = Imgui::Scratch_Text(capacity);")
        statements.append(f"    {call};")
        statements.append("}")
        statements.append("return buffer;")
    elif kind == "draw_list":
        statements.append(f"return Imgui::Draw_List({call});")
    elif kind == "font":
        statements.append(f"return Imgui::Font({call});")
    elif kind == "viewport":
        statements.append(f"return Imgui::Viewport({call});")
    else:
        statements.append(f"return {call};")
    body = [f"    {line}" for line in declarations]
    if declarations:
        body.append("")
    body += [f"    {line}" for line in statements]
    return "\n".join([header] + body + ["}"])


def host_native(function, types):
    by_name = {parameter.name: parameter for parameter in function.parameters}
    slots = {}
    slot = 1 if function.scope == "ImDrawList" else 0
    for parameter, kind in zip(function.parameters, function.kinds):
        slots[parameter.name] = slot
        slot += SLOTS.get(kind, 1)
    result_slot = slot

    def scalar(name):
        parameter = by_name[name]
        index = slots[name]
        kind = types.scalar(parameter.type)
        if kind == "bool":
            return f"static_cast<u32>(slots[{index}]) != 0"
        if kind == "f32":
            return f"Imgui_F32(slots[{index}])"
        if kind == "f64":
            return f"Imgui_F64(slots[{index}])"
        value = f"static_cast<{parameter.type}>(slots[{index}])"
        return LIMITS[name].format(value=value) if name in LIMITS else value

    guards = []
    for parameter, kind in zip(function.parameters, function.kinds):
        if kind in WIRE and parameter.type in TYPE_GUARDS and not (
                function.qualified == "ImGui::GetKeyName" and parameter.name == "key"):
            guards.append(TYPE_GUARDS[parameter.type].format(value=scalar(parameter.name)))
    if function.qualified in GUARDS:
        guards.append(GUARDS[function.qualified].format(**{name: scalar(name) for name in by_name if name in slots and function.kinds[list(by_name).index(name)] in WIRE}))

    locals_ = []
    arguments = []
    if function.scope == "ImDrawList":
        locals_.append("ImDrawList* list = Imgui_Draw_List(&call, slots[0]);")
    for index, (parameter, kind) in enumerate(zip(function.parameters, function.kinds)):
        name = parameter.name
        at = slots[name]
        if kind in WIRE:
            arguments.append(scalar(name))
        elif kind == "vec2":
            arguments.append(f"Imgui_Vec2(slots[{at}])")
        elif kind == "vec4":
            arguments.append(f"Imgui_Vec4(slots[{at}], slots[{at + 1}])")
        elif kind == "string":
            following = function.parameters[index + 1] if index + 1 < len(function.parameters) else None
            if following is not None and function.kinds[index + 1] == "text_end":
                locals_.append(f"const char* {name} = Imgui_Text(&call, slots[{at}], slots[{at + 1}]);")
            else:
                required = name in REQUIRED_STRINGS and not nullable(function, parameter)
                locals_.append(f"const char* {name} = Imgui_String(&call, slots[{at}], {str(required).lower()});")
            arguments.append(name)
        elif kind == "text_end":
            begin = function.parameters[index - 1].name
            locals_.append(f"const char* {name} = Imgui_Text_End({begin}, slots[{at - 1}], slots[{at}]);")
            arguments.append(name)
        elif kind == "zeros":
            locals_.append(f"const char* {name} = Imgui_Zero_Separated(&call, slots[{at}]);")
            arguments.append(name)
        elif kind == "format":
            locals_.append(f"const char* text = Imgui_String(&call, slots[{at}], true);")
            arguments += ['"%s"', "text"]
        elif kind == "strings":
            locals_.append(
                f"const char* const* {name} = Imgui_Strings(&call, slots[{at}], {scalar('items_count')});"
            )
            arguments.append(name)
        elif kind == "buffer":
            locals_.append(f"char* {name} = Imgui_Pointer<char>(&call, slots[{at}], {scalar('buf_size')}, false);")
            arguments.append(name)
        elif kind in ("data", "bound"):
            components = scalar("components") if kind == "data" and "components" in by_name else "1"
            pointer = "void*" if kind == "data" else "const void*"
            allowed = str(kind == "bound" and nullable(function, parameter)).lower()
            locals_.append(
                f"{pointer} {name} = Imgui_Data(&call, slots[{at}], {scalar('data_type')}, {components}, {allowed});"
            )
            arguments.append(name)
        elif kind == "sized":
            locals_.append(f"const void* {name} = Imgui_Pointer<const u8>(&call, slots[{at}], {scalar('sz')}, true);")
            arguments.append(name)
        elif kind == "values":
            locals_.append(
                f"const float* {name} = Imgui_Values(&call, slots[{at}], {scalar('values_count')}, {scalar('stride')});"
            )
            arguments.append(name)
        elif kind == "points":
            locals_.append(
                f"const ImVec2* {name} = Imgui_Pointer<const ImVec2>(&call, slots[{at}], {scalar('num_points')}, false);"
            )
            arguments.append(name)
        elif kind == "id":
            arguments.append(f"Imgui_Id(slots[{at}])")
        elif kind == "texture":
            locals_.append(f"ImTextureRef {name} = Imgui_Texture(&call, slots[{at}]);")
            arguments.append(name)
        elif kind == "font":
            locals_.append(f"ImFont* {name} = Imgui_Font(&call, slots[{at}]);")
            arguments.append(name)
        elif kind == "viewport":
            locals_.append(f"ImGuiViewport* {name} = Imgui_Viewport(&call, slots[{at}]);")
            arguments.append(name)
        elif kind in ("style", "callback", "callback_data"):
            arguments.append("nullptr")
        elif kind == "pointer":
            element, count = parameter.pointee(types)
            allowed = str(nullable(function, parameter)).lower()
            locals_.append(f"{element}* {name} = Imgui_Pointer<{element}>(&call, slots[{at}], {count}, {allowed});")
            arguments.append(f"*{name}" if parameter.type.endswith("&") else name)
    kind = function.return_kind
    if kind == "vec4":
        locals_.append(f"ImVec4* result = Imgui_Pointer<ImVec4>(&call, slots[{result_slot}], 1, false);")
    target = "list->" if function.scope == "ImDrawList" else "ImGui::"
    call = HOST_CALLS.get(function.qualified, f"{target}{function.name}({{arguments}})")
    call = call.format(arguments=", ".join(arguments))
    if kind == "void":
        statement = f"{call};"
    elif kind == "bool":
        statement = f"slots[0] = {call};"
    elif kind in ("i32", "i64"):
        statement = f"slots[0] = static_cast<u64>({call});"
    elif kind == "f32":
        statement = f"Imgui_Return_F32(slots, {call});"
    elif kind == "f64":
        statement = f"Imgui_Return_F64(slots, {call});"
    elif kind == "vec2":
        statement = f"slots[0] = Imgui_Pack({call});"
    elif kind == "vec4":
        statement = f"*result = {call};"
    elif kind == "string":
        statement = f"slots[0] = Imgui_String_Result(&call, {call}, slots[{result_slot}], slots[{result_slot + 1}]);"
    elif kind == "draw_list":
        statement = f"slots[0] = Imgui_Draw_List_Handle({call});"
    elif kind == "font":
        statement = f"slots[0] = Imgui_Font_Handle({call});"
    else:
        statement = f"slots[0] = Imgui_Viewport_Id({call});"
    returns_value = kind not in ("void", "vec4")
    uses_slots = returns_value or result_slot > 0 or kind == "vec4"
    lines = [
        f"void Native_{function.import_name}(wasm_exec_env_t exec_env, u64*{' slots' if uses_slots else ''}) {{",
        f'    Imgui_Call call = Imgui_Enter(exec_env, "{function.qualified}");',
    ]
    lines += [f"    {line}" for line in locals_]
    lines.append("")
    lines += [f"    {guard};" for guard in guards]
    lines.append("    if (call.failed) {")
    if returns_value:
        lines.append("        slots[0] = 0;")
    lines += ["        return;", "    }", f"    {statement}", "}"]
    return "\n".join(lines)


def wrap(line):
    if len(line) <= LINE_LIMIT:
        return [line]
    indentation = line[: len(line) - len(line.lstrip())]
    for start, character in enumerate(line):
        if character != "(":
            continue
        depth = 0
        parts = []
        begin = start + 1
        end = None
        quoted = False
        for index in range(start, len(line)):
            character = line[index]
            if character == '"':
                quoted = not quoted
            if quoted:
                continue
            if character in "([{":
                depth += 1
            elif character in ")]}":
                depth -= 1
                if depth == 0:
                    parts.append(line[begin:index].strip())
                    end = index
                    break
            elif character == "," and depth == 1:
                parts.append(line[begin:index].strip())
                begin = index + 1
        if end is None or len(parts) < 2:
            continue
        middle = [indentation + "    " + part + ("," if number < len(parts) - 1 else "") for number, part in enumerate(parts)]
        return [line[: start + 1]] + middle + [indentation + line[end:]]
    return [line]


def signature(function, types):
    parameters = "".join(wire(type_name, types) for type_name, _, _ in import_parameters(function, types))
    returns = import_return(function)
    return f"({parameters}){'' if returns == 'void' else wire(returns, types)}"


def main():
    lines = list(active_lines(HEADER.read_text(encoding="utf-8")))
    types = Types(lines)
    functions = []
    overloads = {}
    for scope, text in declarations(lines):
        match = DECLARATION.match(text)
        if match is None:
            continue # constructors and destructors
        function = Function(scope, match)
        if function.qualified in MANUAL or function.qualified in EXCLUDED or function.name.startswith("_"):
            continue
        if any(parameter.function_pointer for parameter in function.parameters):
            continue # getters: imgui_bindings.cpp fills an array and calls the overload taking one
        function.kinds = [parameter_kind(function, index, types) for index in range(len(function.parameters))]
        function.return_kind = return_kind(function, types)
        functions.append(function)

    # va_list overloads
    for function in functions:
        if function.valist:
            continue
        count = overloads.get(function.qualified, 0) + 1
        overloads[function.qualified] = count
        prefix = function.scope if function.scope == "ImDrawList" else "ImGui"
        function.import_name = f"{prefix}_{function.name}" + (f"_{count}" if count > 1 else "")
    for function in functions:
        if not function.valist:
            continue
        leading = [parameter.type for parameter in function.parameters[:-2]]
        for sibling in functions:
            if (not sibling.valist and sibling.scope == function.scope and sibling.name == function.name[:-1]
                    and [parameter.type for parameter in sibling.parameters[:-2]] == leading):
                function.import_name = sibling.import_name
        if function.import_name is None:
            fail(f"{function.qualified}: no formatting sibling")

    natives = [function for function in functions if not function.valist]
    host = [
        "// Generated by generate_imgui_bindings.py",
        '#include "ui/imgui_bindings.h"',
        "",
        "namespace {",
        "",
    ]
    for function in natives:
        host += [host_native(function, types), ""]
    host.append("NativeSymbol sNatives[] = {")
    for function in natives:
        host.append(f'    Native("{function.import_name}", "{signature(function, types)}", Native_{function.import_name}),')
    host += [
        "};",
        "",
        "}",
        "",
        "void Imgui_Register_Generated_Natives() {",
        '    Register_Natives(sNatives, "ImGui", "imgui");',
        "}",
        "",
    ]

    sdk = [
        "// Generated by client/src/ui/generate_imgui_bindings.py",
        '#include "imgui_bindings.h"',
        "",
        "namespace Imgui = ModLoader::Runtime::Imgui;",
        "",
        'extern "C" {',
        "",
    ]
    for function in natives:
        parameters = ", ".join(f"{type_name} {name}" for type_name, name, _ in import_parameters(function, types))
        sdk.append(
            f'IMGUI_IMPORT("{function.import_name}") {import_return(function)} {function.import_name}({parameters});'
        )
    sdk += ["", "}", ""]
    for function in functions:
        sdk += [module_definition(function, types), ""]

    host = [wrapped for line in "\n".join(host).split("\n") for wrapped in wrap(line)]
    sdk = [wrapped for line in "\n".join(sdk).split("\n") for wrapped in wrap(line)]
    CLIENT_OUTPUT.write_text("\n".join(host), encoding="utf-8", newline="\n")
    SDK_OUTPUT.write_text("\n".join(sdk), encoding="utf-8", newline="\n")
    print(f"{len(natives)} natives, {len(functions)} functions")


if __name__ == "__main__":
    main()
