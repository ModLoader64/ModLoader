#include "frontend/library.h"
#include "base/json.h"
#include "module_manifest.h"
#include "module_package.h"
#include "network/names.h"
#include "wasm_object.h"
#include <modloader_adapter_api.h>

#include <algorithm>
#include <compare>
#include <ctime>
#include <filesystem>
#include <set>

namespace {

constexpr const char* gDefaultAdapter = "gopher64";
constexpr const char* gDefaultServer = "127.0.0.1";
constexpr const char* gArtworkExtensions[] = { ".png", ".jpg", ".jpeg" };
constexpr const char* gBoxartFolder = "Named_Boxarts";
constexpr const char* gLogoFolder = "Named_Logos";
constexpr u64 gArtworkLinkLimit = 1024;
constexpr usize gArtworkLinkDepth = 4;
constexpr const char* gRomExtensions[] = { ".z64", ".n64", ".v64" };
constexpr std::string_view gThumbnailUnsafeCharacters = "&*/:`<>?\\|\"";
constexpr std::pair<const char*, std::string Frontend_Library::*> gTextSettings[] = {
    { "pluginDirectory", &Frontend_Library::pluginDirectory },
    { "artworkDirectory", &Frontend_Library::artworkDirectory },
    { "adapterPath", &Frontend_Library::adapterPath },
    { "nickname", &Frontend_Library::nickname },
    { "server", &Frontend_Library::server },
    { "lobby", &Frontend_Library::lobby },
    { "selectedGame", &Frontend_Library::selectedGame },
    { "view", &Frontend_Library::view },
    { "sort", &Frontend_Library::sort },
};

std::filesystem::path Text_Path(const std::string& text) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string Path_Text(const std::filesystem::path& path) {
    auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string Canonical_Path(const std::string& path) {
    std::error_code error;
    auto canonical = std::filesystem::canonical(Text_Path(path), error);
    return error ? path : Path_Text(canonical);
}

u8 Lowercase(u8 character) {
    return character >= 'A' && character <= 'Z' ? character + ('a' - 'A') : character;
}

std::string Lowercase(const std::string& text) {
    std::string result = text;
    for (char& character : result) {
        character = static_cast<char>(Lowercase(static_cast<u8>(character)));
    }
    return result;
}

std::string Extension(const std::filesystem::path& path) {
    return Lowercase(Path_Text(path.extension()));
}

auto Compare_Names(const std::string& left, const std::string& right) {
    return std::lexicographical_compare_three_way(left.begin(), left.end(), right.begin(), right.end(), [](u8 left_character, u8 right_character) {
        return Lowercase(left_character) <=> Lowercase(right_character);
    });
}

void Read_Text(yyjson_val* root, const char* key, std::string& output) {
    yyjson_val* value = yyjson_obj_get(root, key);
    if (yyjson_is_str(value)) {
        std::string_view text(yyjson_get_str(value), yyjson_get_len(value));
        if (text.find('\0') == std::string_view::npos) {
            output = text;
        }
    }
}

void Read_Bool(yyjson_val* root, const char* key, bool& output) {
    yyjson_val* value = yyjson_obj_get(root, key);
    if (yyjson_is_bool(value)) {
        output = yyjson_get_bool(value);
    }
}

std::string Resolve_Artwork(const std::filesystem::directory_entry& entry) {
    std::filesystem::path path = entry.path();
    std::error_code error;
    u64 size = entry.file_size(error);
    for (usize depth = 0; !error && depth < gArtworkLinkDepth; depth++) {
        if (size > gArtworkLinkLimit) {
            return Path_Text(path);
        }
        auto bytes = File_Read(Path_Text(path));
        if (!bytes || std::ranges::find(*bytes, 0) != bytes->end()) {
            return bytes ? Path_Text(path) : "";
        }
        path = path.parent_path() / Text_Path(std::string(bytes->begin(), bytes->end()));
        size = std::filesystem::file_size(path, error);
    }
    return "";
}

std::string Find_Artwork(const std::unordered_map<std::string, std::string>& index, const std::string& lowercase_name) {
    auto found = index.find(lowercase_name);
    return found != index.end() ? found->second : "";
}

} // namespace

void Frontend_Library::Load(const std::string& data_directory) {
    configPath = Path_Join(Path_Join(data_directory, "config"), "frontend.json");
    pluginDirectory = Path_Join(Path_Directory(Path_Executable_Directory()), "plugins");

    auto bytes = File_Read(configPath);
    Json_Document document(bytes ? yyjson_read(reinterpret_cast<const char*>(bytes->data()), bytes->size(), 0) : nullptr);
    yyjson_val* root = document ? yyjson_doc_get_root(document.get()) : nullptr;
    if (bytes && !yyjson_is_obj(root)) {
        Log_Error("frontend", "could not read %s", configPath.c_str());
    }

    for (const auto& [key, member] : gTextSettings) {
        Read_Text(root, key, this->*member);
    }
    selectedGame = Canonical_Path(selectedGame);
    if (view != "boxart" && view != "details" && view != "list") {
        view = "boxart";
    }
    if (sort != "name" && sort != "recent") {
        sort = "name";
    }
    Read_Bool(root, "offline", offline);
    Read_Bool(root, "showGameDetails", showGameDetails);
    yyjson_obj_iter iterator = yyjson_obj_iter_with(yyjson_obj_get(root, "romDirectories"));
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator)) {
        auto& directories = romDirectories[yyjson_get_str(key)];
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        usize index;
        usize count;
        yyjson_val* directory_value;
        yyjson_arr_foreach(value, index, count, directory_value) {
            if (yyjson_is_str(directory_value)) {
                std::string directory(yyjson_get_str(directory_value), yyjson_get_len(directory_value));
                if (!directory.empty() && directory.find('\0') == std::string::npos) {
                    directories.push_back(std::move(directory));
                }
            }
        }
    }
    for (auto [name, history] : { std::pair{ "recent", &recent }, std::pair{ "playtime", &playtime } }) {
        iterator = yyjson_obj_iter_with(yyjson_obj_get(root, name));
        while (yyjson_val* key = yyjson_obj_iter_next(&iterator)) {
            yyjson_val* value = yyjson_obj_iter_get_val(key);
            if (yyjson_is_uint(value)) {
                u64& previous = (*history)[Canonical_Path(yyjson_get_str(key))];
                previous = std::max(previous, yyjson_get_uint(value));
            }
        }
    }
    iterator = yyjson_obj_iter_with(yyjson_obj_get(root, "mods"));
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator)) {
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        if (yyjson_is_bool(value)) {
            modSelections[yyjson_get_str(key)] = yyjson_get_bool(value);
        }
    }
    iterator = yyjson_obj_iter_with(yyjson_obj_get(root, "artworkDirectories"));
    while (yyjson_val* key = yyjson_obj_iter_next(&iterator)) {
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        if (yyjson_is_str(value)) {
            artworkDirectories[yyjson_get_str(key)] = yyjson_get_str(value);
        }
    }
    Set_Connection_Defaults();
    Scan_Adapters();
}

void Frontend_Library::Set_Connection_Defaults() {
    if (nickname.empty() || lobby.empty()) {
        Online_Names names = Random_Names();
        if (nickname.empty()) {
            nickname = names.nickname;
        }
        if (lobby.empty()) {
            lobby = names.lobby;
        }
    }
    if (server.empty()) {
        server = gDefaultServer;
    }
}

void Frontend_Library::Save() {
    Store_Mod_Selections();
    Mutable_Json_Document document(yyjson_mut_doc_new(nullptr));
    if (!document) {
        return;
    }
    yyjson_mut_val* root = yyjson_mut_obj(document.get());
    yyjson_mut_doc_set_root(document.get(), root);
    for (const auto& [key, member] : gTextSettings) {
        yyjson_mut_obj_add_strcpy(document.get(), root, key, (this->*member).c_str());
    }
    yyjson_mut_obj_add_bool(document.get(), root, "offline", offline);
    yyjson_mut_obj_add_bool(document.get(), root, "showGameDetails", showGameDetails);
    yyjson_mut_val* directories = yyjson_mut_obj_add_obj(document.get(), root, "romDirectories");
    for (const auto& entry : romDirectories) {
        yyjson_mut_val* paths = yyjson_mut_arr(document.get());
        yyjson_mut_obj_add_val(document.get(), directories, entry.first.c_str(), paths);
        for (const std::string& path : entry.second) {
            yyjson_mut_arr_add_strcpy(document.get(), paths, path.c_str());
        }
    }
    for (auto [name, history] : { std::pair{ "recent", &recent }, std::pair{ "playtime", &playtime } }) {
        yyjson_mut_val* values = yyjson_mut_obj_add_obj(document.get(), root, name);
        for (const auto& entry : *history) {
            yyjson_mut_obj_add_uint(document.get(), values, entry.first.c_str(), entry.second);
        }
    }
    yyjson_mut_val* selections = yyjson_mut_obj_add_obj(document.get(), root, "mods");
    for (const auto& entry : modSelections) {
        yyjson_mut_obj_add_bool(document.get(), selections, entry.first.c_str(), entry.second);
    }
    yyjson_mut_val* artwork = yyjson_mut_obj_add_obj(document.get(), root, "artworkDirectories");
    for (const auto& entry : artworkDirectories) {
        yyjson_mut_obj_add_strcpy(document.get(), artwork, entry.first.c_str(), entry.second.c_str());
    }
    if (!Json_Write_File(configPath, document.get())) {
        Log_Error("frontend", "cannot save %s", configPath.c_str());
    }
}

void Frontend_Library::Sort_Games() {
    const bool by_recent = sort == "recent";
    std::sort(games.begin(), games.end(), [&](const Frontend_Game& left, const Frontend_Game& right) {
        if (by_recent && left.lastPlayed != right.lastPlayed) {
            return left.lastPlayed > right.lastPlayed;
        }
        const auto comparison = Compare_Names(left.name, right.name);
        return comparison == 0 ? left.path < right.path : comparison < 0;
    });
}

void Frontend_Library::Scan_Adapters() {
    adapters.clear();
    std::vector<std::string> paths;
    std::error_code error;
    std::filesystem::directory_iterator iterator(Text_Path(Path_Join(Path_Executable_Directory(), "adapters")), error);
    for (std::filesystem::directory_iterator end; !error && iterator != end; iterator.increment(error)) {
        const auto& entry = *iterator;
        std::error_code entry_error;
        if (entry.is_regular_file(entry_error) && Extension(entry.path()) == Library_Extension()) {
            paths.push_back(Canonical_Path(Path_Text(entry.path())));
        }
    }
    std::string selected_path;
    if (!adapterPath.empty()) {
        selected_path = Canonical_Path(adapterPath);
        if (std::ranges::find(paths, selected_path) == paths.end()) {
            paths.push_back(selected_path);
        }
    }
    for (const std::string& path : paths) {
        Library library;
        std::string load_error;
        if (!library.Open(path, load_error)) {
            continue;
        }
        auto get_adapter = reinterpret_cast<ModLoader_Get_Platform_Adapter_Function>(library.Symbol("ModLoader_Get_Platform_Adapter"));
        const ModLoader_Platform_Adapter* identity = get_adapter != nullptr ? get_adapter() : nullptr;
        constexpr usize required_size = offsetof(ModLoader_Platform_Adapter, processorDescribe) + sizeof(identity->processorDescribe);
        if (identity == nullptr || identity->abiVersion != MODLOADER_ADAPTER_ABI_VERSION || identity->structSize < required_size ||
            identity->platformIdentifier == nullptr || identity->platformIdentifier[0] == 0 ||
            identity->adapterIdentifier == nullptr || identity->adapterIdentifier[0] == 0) {
            continue;
        }
        adapters.push_back({ path, identity->adapterIdentifier, identity->platformIdentifier });
    }
    std::sort(adapters.begin(), adapters.end(), [](const Frontend_Adapter& left, const Frontend_Adapter& right) {
        return left.name == right.name ? left.path < right.path : left.name < right.name;
    });
    auto selected = std::ranges::find(adapters, selected_path, &Frontend_Adapter::path);
    if (selected == adapters.end()) {
        selected = std::ranges::find(adapters, gDefaultAdapter, &Frontend_Adapter::name);
    }
    if (selected == adapters.end()) {
        selected = adapters.begin();
    }
    platform = selected != adapters.end() ? selected->platform : "";
    if (selected != adapters.end()) {
        adapterPath = selected->path;
    }
}

void Frontend_Library::Store_Mod_Selections() {
    for (const Frontend_Mod& mod : mods) {
        modSelections[mod.path] = mod.enabled;
    }
}

void Frontend_Library::Index_Artwork(const std::string& root) {
    artworkRoot = root;
    boxarts.clear();
    logos.clear();
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(Text_Path(root), std::filesystem::directory_options::skip_permission_denied, error);
    for (std::filesystem::recursive_directory_iterator end; !error && iterator != end; iterator.increment(error)) {
        const std::filesystem::path& path = iterator->path();
        std::string folder = Path_Text(path.parent_path().filename());
        if ((folder != gBoxartFolder && folder != gLogoFolder) || std::ranges::find(gArtworkExtensions, Extension(path)) == std::end(gArtworkExtensions)) {
            continue;
        }
        std::string image = Resolve_Artwork(*iterator);
        if (!image.empty()) {
            (folder == gBoxartFolder ? boxarts : logos).try_emplace(Lowercase(Path_Text(path.stem())), std::move(image));
        }
    }
}

void Frontend_Library::Scan(bool reindex_artwork) {
    Store_Mod_Selections();
    games.clear();
    mods.clear();
    auto adapter = std::ranges::find(adapters, adapterPath, &Frontend_Adapter::path);
    auto custom = adapter != adapters.end() ? artworkDirectories.find(adapter->name) : artworkDirectories.end();
    std::string artwork = custom != artworkDirectories.end() && !custom->second.empty() ? custom->second : artworkDirectory;
    if (reindex_artwork || artwork != artworkRoot) {
        Index_Artwork(artwork);
    }
    auto directory = romDirectories.find(platform);
    if (platform == "n64" && directory != romDirectories.end()) {
        std::set<std::string> game_paths;
        for (const std::string& path : directory->second) {
            std::error_code error;
            std::filesystem::recursive_directory_iterator iterator(Text_Path(path), std::filesystem::directory_options::skip_permission_denied, error);
            for (std::filesystem::recursive_directory_iterator end; !error && iterator != end; iterator.increment(error)) {
                const auto& entry = *iterator;
                std::error_code entry_error;
                if (!entry.is_regular_file(entry_error) || std::ranges::find(gRomExtensions, Extension(entry.path())) == std::end(gRomExtensions)) {
                    continue;
                }
                Frontend_Game game;
                game.path = Canonical_Path(Path_Text(entry.path()));
                if (!game_paths.insert(game.path).second) {
                    continue;
                }
                game.name = Path_Text(entry.path().filename());
                std::error_code size_error;
                auto file_size = entry.file_size(size_error);
                if (!size_error) {
                    game.size = static_cast<u64>(file_size);
                }
                auto played = recent.find(game.path);
                if (played != recent.end()) {
                    game.lastPlayed = played->second;
                }
                auto duration = playtime.find(game.path);
                if (duration != playtime.end()) {
                    game.playtime = duration->second;
                }
                std::string stem = Lowercase(Path_Text(entry.path().stem()));
                for (char& character : stem) {
                    if (gThumbnailUnsafeCharacters.find(character) != std::string_view::npos) {
                        character = '_';
                    }
                }
                game.art = Find_Artwork(boxarts, stem);
                game.logo = Find_Artwork(logos, stem);
                games.push_back(std::move(game));
            }
        }
    }
    Sort_Games();

    std::error_code plugin_error;
    std::filesystem::directory_iterator iterator(Text_Path(pluginDirectory), plugin_error);
    std::vector<std::filesystem::path> plugin_paths;
    std::set<std::string> package_stems;
    for (std::filesystem::directory_iterator end; !plugin_error && iterator != end; iterator.increment(plugin_error)) {
        const auto& entry = *iterator;
        std::string extension = Extension(entry.path());
        std::error_code entry_error;
        if (!entry.is_regular_file(entry_error) || (extension != ".pak" && extension != ".wasm")) {
            continue;
        }
        plugin_paths.push_back(entry.path());
        if (extension == ".pak") {
            package_stems.insert(Lowercase(Path_Text(entry.path().stem())));
        }
    }
    for (const auto& plugin_path : plugin_paths) {
        std::string path = Path_Text(plugin_path);
        std::optional<Module_Manifest> manifest;
        if (Extension(plugin_path) == ".pak") {
            auto bytes = Read_Module_Package_Manifest(path);
            if (bytes) {
                manifest = Parse_Module_Manifest(*bytes, path);
            }
        }
        else if (!package_stems.contains(Lowercase(Path_Text(plugin_path.stem())))) {
            auto bytes = File_Read(path);
            Wasm_Metadata metadata;
            if (bytes && Wasm_Read_Metadata(*bytes, metadata) && !metadata.module.empty()) {
                auto description = Read_Module_Metadata(metadata.module, path);
                if (description) {
                    manifest = std::move(description->manifest);
                }
            }
        }
        if (!manifest) {
            continue;
        }
        Frontend_Mod mod;
        mod.path = Canonical_Path(path);
        auto selection = modSelections.find(mod.path);
        if (selection != modSelections.end()) {
            mod.enabled = selection->second;
        }
        mod.name = std::move(manifest->name);
        mod.version = std::move(manifest->version);
        mod.description = std::move(manifest->description);
        mod.dependencies = std::move(manifest->dependencies);
        mods.push_back(std::move(mod));
    }
    std::sort(mods.begin(), mods.end(), [](const Frontend_Mod& left, const Frontend_Mod& right) {
        return Compare_Names(left.name, right.name) < 0;
    });
}

void Frontend_Library::Record_Play(const std::string& path) {
    std::string canonical = Canonical_Path(path);
    recent[canonical] = static_cast<u64>(std::time(nullptr));
    selectedGame = canonical;
    Save();
}

void Frontend_Library::Add_Playtime(const std::string& path, u64 seconds) {
    playtime[Canonical_Path(path)] += seconds;
    Save();
}
