#include "module_manifest.h"
#include "base/json.h"

#include <modloader/detail/metadata.h>
#include <algorithm>
#include <unordered_set>

bool Module_Name_Is_Valid(std::string_view name) {
    return !name.empty() && name != "." && name != ".." && std::ranges::all_of(name, [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.';
    });
}

bool Module_Relative_Path_Is_Valid(std::string_view path) {
    do {
        usize end = path.find('/');
        std::string_view part = path.substr(0, end);
        if (part.empty() || part == "." || part == ".." || std::ranges::any_of(part, [](char character) {
                return static_cast<u8>(character) < 32 || character == '\\' || character == ':';
            })) {
            return false;
        }

        if (end == std::string_view::npos) {
            return true;
        }

        path.remove_prefix(end + 1);
    } while (true);
}

namespace {

Json_Document Read_Json(std::span<const u8> bytes, const std::string& source) {
    Json_Document document(yyjson_read(reinterpret_cast<const char*>(bytes.data()), bytes.size(), 0));
    if (!document || !yyjson_is_obj(yyjson_doc_get_root(document.get()))) {
        Log_Error("manifest", "expected a JSON object in %s", source.c_str());
        return nullptr;
    }

    std::unordered_set<std::string_view> keys;
    yyjson_obj_iter iterator = yyjson_obj_iter_with(yyjson_doc_get_root(document.get()));
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator)) {
        std::string_view name(yyjson_get_str(key), yyjson_get_len(key));
        if (name.find('\0') != std::string_view::npos || !keys.insert(name).second) {
            Log_Error("manifest", "invalid or duplicate JSON field in %s", source.c_str());
            return nullptr;
        }
    }

    return document;
}

bool Text(yyjson_val* root, const char* key, std::string& output, bool required = true) {
    yyjson_val* value = yyjson_obj_get(root, key);
    if (value == nullptr && !required) {
        return true;
    }

    if (!yyjson_is_str(value)) {
        return false;
    }

    output.assign(yyjson_get_str(value), yyjson_get_len(value));
    return output.find('\0') == std::string::npos;
}

bool List(yyjson_val* root, const char* key, std::vector<std::string>& output, bool identifiers) {
    yyjson_val* values = yyjson_obj_get(root, key);
    if (!yyjson_is_arr(values)) {
        return false;
    }

    std::unordered_set<std::string> seen;
    for (usize index = 0; index < yyjson_arr_size(values); index++) {
        yyjson_val* value = yyjson_arr_get(values, index);
        if (!yyjson_is_str(value)) {
            return false;
        }

        std::string text(yyjson_get_str(value), yyjson_get_len(value));
        if (text.empty() || text.find('\0') != std::string::npos || (identifiers && !Module_Name_Is_Valid(text)) || !seen.insert(text).second) {
            return false;
        }

        output.push_back(std::move(text));
    }
    return true;
}

std::optional<Module_Manifest> Parse(yyjson_val* root, const char* version_key, const std::string& source) {
    Module_Manifest manifest;
    if (!Text(root, "name", manifest.name) || !Module_Name_Is_Valid(manifest.name) ||
        !Text(root, version_key, manifest.version) || manifest.version.empty() ||
        !Text(root, "description", manifest.description) || !List(root, "authors", manifest.authors, false) ||
        !List(root, "dependencies", manifest.dependencies, true)) {
        Log_Error("manifest", "invalid fields in %s", source.c_str());
        return std::nullopt;
    }

    manifest.folder = manifest.name;
    if (!Text(root, "folder", manifest.folder, false) || !Module_Name_Is_Valid(manifest.folder) ||
        !Text(root, "icon", manifest.icon, false) || (!manifest.icon.empty() && !Module_Relative_Path_Is_Valid(manifest.icon)) ||
        !Text(root, "banner", manifest.banner, false) || (!manifest.banner.empty() && !Module_Relative_Path_Is_Valid(manifest.banner))) {
        Log_Error("manifest", "invalid folder, icon or banner in %s", source.c_str());
        return std::nullopt;
    }

    if (std::ranges::contains(manifest.dependencies, manifest.name)) {
        Log_Error("manifest", "%s depends on itself", manifest.name.c_str());
        return std::nullopt;
    }

    std::ranges::sort(manifest.dependencies);
    manifest.sourcePath = source;
    return manifest;
}

} // namespace

std::optional<Module_Manifest> Parse_Module_Manifest(std::span<const u8> bytes, const std::string& source) {
    auto document = Read_Json(bytes, source);
    return document ? Parse(yyjson_doc_get_root(document.get()), "version", source) : std::nullopt;
}

std::optional<Module_Metadata> Read_Module_Metadata(std::span<const u8> bytes, const std::string& source) {
    auto document = Read_Json(bytes, source);
    if (!document) {
        return std::nullopt;
    }

    yyjson_val* root = yyjson_doc_get_root(document.get());
    yyjson_val* version = yyjson_obj_get(root, "version");
    if (!yyjson_is_uint(version) || yyjson_get_uint(version) != MODLOADER_METADATA_VERSION) {
        Log_Error("manifest", "%s: expected metadata version %d", source.c_str(), MODLOADER_METADATA_VERSION);
        return std::nullopt;
    }

    auto manifest = Parse(root, "pluginVersion", source);
    if (!manifest) {
        return std::nullopt;
    }

    Module_Metadata metadata;
    if (!Text(root, "runtime", metadata.runtime) || metadata.runtime.empty() ||
        !Text(root, "runtimeHash", metadata.runtimeHash) || metadata.runtimeHash.empty()) {
        Log_Error("manifest", "invalid SDK runtime reference in %s", source.c_str());
        return std::nullopt;
    }
    
    metadata.manifest = std::move(*manifest);
    return metadata;
}

bool Module_Manifest_Matches(const Module_Manifest& left, const Module_Manifest& right) {
    return left.name == right.name && left.version == right.version && left.folder == right.folder && left.authors == right.authors &&
        left.description == right.description && left.dependencies == right.dependencies && left.icon == right.icon && left.banner == right.banner;
}
