#include "frontend/frontend.h"
#include "frontend/aot_preparation.h"
#include "frontend/images.h"
#include "frontend/library.h"
#include "frontend/window.h"
#include "client_settings.h"
#include "base/json.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <ctime>
#include <unordered_map>

namespace {

constexpr const char* gEvents[] = { "click", "mouseup", "change", "resize" };
constexpr usize gRecentGames = 12;
constexpr s32 gPrefetchCards = 32;
constexpr s32 gBoxartCardWidth = 180;
constexpr s32 gOverscanRows = 3;
constexpr s32 gPartialRows = 2;
constexpr s32 gRightMouseButton = 1;
constexpr u64 gSecondsPerMinute = 60;
constexpr u64 gMinutesPerHour = 60;
constexpr u64 gBytesPerMiB = 1024 * 1024;
constexpr const char* gCardBackground = "<div class=\"card-background\"><div class=\"card-gradient\"></div>"
    "<div class=\"logo-stripes\"><div class=\"logo-stripe-middle\"></div>"
    "<div class=\"logo-stripe-dark\"></div></div></div>";

Rml::Element* Attribute_Target(Rml::Element* element, const char* attribute) {
    while (element != nullptr && !element->HasAttribute(attribute)) {
        element = element->GetParentNode();
    }
    return element;
}

void Apply_Colors(Rml::Element* element, const std::string& path) {
    auto bytes = File_Read(path);
    Json_Document manifest(bytes ? yyjson_read(reinterpret_cast<const char*>(bytes->data()), bytes->size(), 0) : nullptr);
    yyjson_val* root = manifest ? yyjson_doc_get_root(manifest.get()) : nullptr;
    yyjson_obj_iter colors = yyjson_obj_iter_with(yyjson_obj_get(root, "colors"));
    while (yyjson_val* key = yyjson_obj_iter_next(&colors)) {
        yyjson_val* value = yyjson_obj_iter_get_val(key);
        if (yyjson_is_str(value)) {
            element->SetProperty(std::string("--") + yyjson_get_str(key), yyjson_get_str(value));
        }
    }
}

std::string Escape(const std::string& text) {
    return Rml::StringUtilities::EncodeRml(text);
}

std::string Date_Text(u64 time) {
    if (time == 0) {
        return "Not played yet";
    }
    time_t seconds = static_cast<time_t>(time);
    tm date {};
#if defined(_WIN32)
    localtime_s(&date, &seconds);
#else
    localtime_r(&seconds, &date);
#endif
    char text[32];
    strftime(text, sizeof(text), "%b %d, %Y", &date);
    return text;
}

std::string Playtime_Text(u64 seconds) {
    if (seconds == 0) {
        return "0m played";
    }
    if (seconds < gSecondsPerMinute) {
        return "<1m played";
    }
    u64 minutes = seconds / gSecondsPerMinute;
    if (minutes < gMinutesPerHour) {
        return std::to_string(minutes) + "m played";
    }
    return std::to_string(minutes / gMinutesPerHour) + "h " + std::to_string(minutes % gMinutesPerHour) + "m played";
}

bool Matches(const std::string& name, const std::string& search) {
    return std::search(name.begin(), name.end(), search.begin(), search.end(), [](u8 left, u8 right) {
        return tolower(left) == tolower(right);
    }) != name.end();
}

std::string File_Url(const std::string& path) {
    std::string normalized = Rml::StringUtilities::Replace(path, '\\', '/');
    std::string url = normalized.starts_with("//") ? "file:" : normalized.starts_with('/') ? "file://" : "file:///";
    for (u8 character : normalized) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || std::string_view("-._~:/").find(character) != std::string_view::npos) {
            url += static_cast<char>(character);
        }
        else {
            url += Text_Format("%%%02X", static_cast<u32>(character));
        }
    }
    return url;
}

std::string Image_Source(const std::string& path) {
    return Rml::StringUtilities::Replace(path, '\\', '/');
}

std::string Artwork(const char* classes, const std::string& source, bool placeholder = true) {
    std::string content = "<div class=\"" + std::string(classes) + "\"";
    if (!source.empty()) {
        content += " data-cover=\"" + Escape(source) + "\"";
    }
    return content + ">" + (placeholder ? "<svg src=\"controller.svg\"/>" : "") + "</div>";
}

std::string Game_Metadata(const Frontend_Game& game) {
    return "<div class=\"game-meta\"><span>" + Date_Text(game.lastPlayed) +
        "</span><span>" + Escape(Playtime_Text(game.playtime)) + "</span></div>";
}

std::string Card_Content(const Frontend_Game& game, std::string_view view = {}) {
    bool boxart = view == "boxart";
    bool row = view == "details" || view == "list";
    std::string content = view == "list" ? "" : Artwork("cover no-art", Image_Source(game.art));
    if (row) {
        content += gCardBackground;
    }
    content += "<div class=\"card-caption\">";
    if (boxart) {
        content += gCardBackground;
    }
    content += "<span class=\"game-title\">" + Escape(game.name) + "</span>";
    if (boxart) {
        content += "<div class=\"card-footer\">" + Game_Metadata(game);
        if (!game.logo.empty()) {
            content += Artwork("logo-image", Image_Source(game.logo), false);
        }
        content += "</div>";
    }
    else if (view != "list") {
        content += Game_Metadata(game);
    }
    content += "</div>";
    if (row) {
        content += "<div class=\"game-logo\">" + Artwork("logo-image", Image_Source(game.logo), false) + "</div>";
    }
    return content;
}

struct Folder_Dialog {
    std::atomic<bool> ready = false;
    std::function<void(const std::vector<std::string>&)> apply;
    std::vector<std::string> paths;
    std::string error;
};

void SDLCALL Folder_Selected(void* userdata, const char* const* files, int) {
    Folder_Dialog& dialog = *static_cast<Folder_Dialog*>(userdata);
    if (files == nullptr) {
        dialog.error = SDL_GetError();
    }
    else {
        for (usize index = 0; files[index] != nullptr; index++) {
            dialog.paths.emplace_back(files[index]);
        }
    }
    dialog.ready.store(true);
    Frontend_Wake();
}

class Frontend final : public Rml::EventListener {
public:
    Frontend(Frontend_Window& window, Frontend_Library& library, Run_Options& options)
        : window(window), library(library), options(options) {
    }

    ~Frontend() {
        if (document != nullptr) {
            for (const char* event : gEvents) {
                document->RemoveEventListener(event, this);
            }
            document->RemoveEventListener("keydown", this, true);
            document->Close();
        }
    }

    bool Open(const std::string& assets, const std::string& status) {
        Apply_Palette(false);
        document = window.Context()->LoadDocument(Path_Join(assets, "frontend.rml"));
        if (document == nullptr) {
            Log_Error("frontend", "cannot load frontend.rml");
            return false;
        }
        document->SetClass("native-chrome", !window.Custom_Chrome());
        assetDirectory = assets;
        Build_Adapters();
        document->Show();
        window.Context()->Update();
        Set_Value("nickname", library.nickname);
        Set_Value("server", library.server);
        Set_Value("lobby", library.lobby);
        Set_Value("mod-directory", library.pluginDirectory);
        Set_Value("view", library.view);
        Set_Value("sort", library.sort);
        if (library.offline) {
            Element("offline")->SetAttribute("checked", "");
        }
        Update_Online();
        Build_Config();
        Refresh_Library();
        Status(status);
        for (const char* event : gEvents) {
            document->AddEventListener(event, this);
        }
        document->AddEventListener("keydown", this, true);
        return true;
    }

    bool Run() {
        std::string compilation_status;
        for (;;) {
            closing |= !window.Poll(!std::exchange(redraw, false));
            Update_Window_Controls();
            Finish_Dialog();
            if (preparation.Poll(compilation_status)) {
                Status(compilation_status.empty() ? "Ready" : compilation_status);
            }
            if ((launch || closing) && preparation.Stop() && dialog == nullptr) {
                break;
            }
            Update_Preview();
            Update_Games();
            window.Draw();
            Update_Covers();
        }
        Read_Form();
        library.Save();
        return launch && !closing;
    }

private:
    void Update_Window_Controls() {
        bool maximized = (SDL_GetWindowFlags(window.Window()) & SDL_WINDOW_MAXIMIZED) != 0;
        if (maximized == document->IsClassSet("maximized")) {
            return;
        }
        document->SetClass("maximized", maximized);
        Element("maximize-icon")->SetAttribute("src", maximized ? "window-restore.svg" : "window-maximize.svg");
        Element("window-maximize")->SetAttribute("title", maximized ? "Restore" : "Maximize");
    }

    Rml::Element* Element(const char* id) {
        auto [element, inserted] = elements.try_emplace(id);
        if (inserted) {
            element->second = document->GetElementById(id);
        }
        return element->second;
    }

    Rml::ElementFormControl* Control(const char* id) {
        return static_cast<Rml::ElementFormControl*>(Element(id));
    }

    std::string Value(const char* id) {
        return Control(id)->GetValue();
    }

    void Set_Value(const char* id, const std::string& value) {
        Control(id)->SetValue(value);
    }

    void Status(const std::string& text) {
        Element("status")->SetInnerRML(Escape(text));
    }

    void Set_Cards(const char* id, const std::string& content) {
        Element(id)->SetInnerRML(content);
        imagesDirty = true;
        redraw = true;
    }

    void Page(const std::string& page) {
        Rml::ElementList buttons;
        document->QuerySelectorAll(buttons, ".nav-item");
        for (Rml::Element* button : buttons) {
            std::string name = button->GetAttribute<Rml::String>("data-page", "");
            button->SetClass("active", name == page);
            Element((name + "-page").c_str())->SetClass("active", name == page);
        }
    }

    void Apply_Palette(bool gamecube) {
        Rml::Element* root = window.Context()->GetRootElement();
        std::string manifests = Path_Join(Path_Executable_Directory(), "adapters");
        Apply_Colors(root, Path_Join(manifests, "default.json"));
        auto adapter = std::ranges::find(library.adapters, library.adapterPath, &Frontend_Adapter::path);
        if (gamecube) {
            Apply_Colors(root, Path_Join(manifests, "dolphin.json"));
        }
        else if (adapter != library.adapters.end()) {
            Apply_Colors(root, Path_Join(Path_Directory(adapter->path), adapter->name + ".json"));
        }
    }

    void Update_Adapter() {
        Apply_Palette(Element("debug-gamecube-palette")->HasAttribute("checked"));
        const std::string& platform = library.platform;
        const char* suffix = platform == "n64" ? "64" : platform == "gc" || platform == "gamecube" ? "GC" :
            platform == "ps2" || platform == "playstation2" ? "PS2" : "";
        static_cast<Rml::ElementText*>(Element("brand-suffix")->GetChild(0))->SetText(suffix);
    }

    void Build_Adapters() {
        std::string entries;
        for (const Frontend_Adapter& adapter : library.adapters) {
            std::string icon = "<svg src=\"controller.svg\"/>";
            std::string attributes;
            std::string svg = adapter.name + ".svg";
            if (File_Exists(Path_Join(assetDirectory, svg))) {
                icon = "<svg src=\"" + Escape(svg) + "\"/>";
            }
            else {
                std::string path = Image_Source(Path_Join(assetDirectory, adapter.name + ".png"));
                if (File_Exists(path)) {
                    window.Prepare_Image(path, true, true);
                    attributes = " data-cover=\"" + Escape(path) + "\"";
                }
            }
            entries += "<option value=\"" + Escape(adapter.path) + "\"" +
                (adapter.path == library.adapterPath ? " selected" : "") + "><span class=\"adapter-icon\"" + attributes + ">" +
                icon + "</span><span>" + Escape(adapter.name) + "</span></option>";
        }
        if (entries.empty()) {
            entries = "<option value=\"\">No emulator installed</option>";
        }
        Element("adapter")->SetInnerRML(entries);
        adapterImagesPending = true;
        Update_Adapter();
    }

    std::string Card(const Frontend_Game& game, usize index, std::string_view view = {}) {
        std::string card = "<button class=\"game-card";
        if (game.path == library.selectedGame) {
            card += " selected";
        }
        card += "\" data-action=\"select-game\" data-index=\"" + std::to_string(index) + "\">";
        return card + Card_Content(game, view) + "</button>";
    }

    void Build_Preview() {
        Element("games-content")->SetClass("details-visible", library.showGameDetails);
        Element("toggle-game-details")->SetClass("active", library.showGameDetails);
        Element("toggle-game-details")->SetAttribute("title", library.showGameDetails ? "Hide game details" : "Show game details");
        if (!library.showGameDetails) {
            return;
        }
        auto game = std::ranges::find(library.games, previewGame, &Frontend_Game::path);
        if (game == library.games.end()) {
            game = std::ranges::find(library.games, library.selectedGame, &Frontend_Game::path);
        }
        bool empty = game == library.games.end();
        Element("preview-empty")->SetProperty("display", empty ? "block" : "none");
        Element("preview-content")->SetProperty("display", empty ? "none" : "block");
        if (empty) {
            previewGame.clear();
            return;
        }
        previewGame = game->path;
        Set_Cards("preview-card", Card_Content(*game));
        Element("preview-name")->SetInnerRML(Escape(game->name));
        Element("preview-location")->SetInnerRML(Escape(Path_Directory(game->path)));
        Element("preview-size")->SetInnerRML(Text_Format("%.1f MiB", static_cast<double>(game->size) / gBytesPerMiB));
    }

    void Update_Preview() {
        if (!library.showGameDetails || !Element("games-page")->IsVisible()) {
            return;
        }
        Rml::Element* hovered = Attribute_Target(window.Context()->GetHoverElement(), "data-index");
        const std::string& path = hovered != nullptr && hovered->GetParentNode() == Element("games") ?
            library.games[hovered->GetAttribute<usize>("data-index", 0)].path : library.selectedGame;
        if (previewGame != path) {
            previewGame = path;
            Build_Preview();
        }
    }

    void Update_Covers() {
        if (imagesDirty) {
            pendingCovers.clear();
            document->QuerySelectorAll(pendingCovers, "[data-cover]");
            std::erase_if(pendingCovers, [](Rml::Element* image) { return image->IsClassSet("adapter-icon"); });
            imagesDirty = false;
        }
        auto load = [&](Rml::Element* cover) {
            bool important = cover->IsClassSet("adapter-icon");
            if (!important && !cover->IsVisible(true)) {
                return false;
            }
            std::string path = cover->GetAttribute<Rml::String>("data-cover", "");
            Frontend_Image_State state = window.Prepare_Image(path, true);
            if (state == Frontend_Image_State::Ready) {
                path = Rml::StringUtilities::Replace(path, "\"", "\\\"");
                cover->SetProperty("decorator", "image(\"" + path + "\" contain)");
                cover->SetInnerRML("");
                cover->SetClass("no-art", false);
                cover->SetClass("loaded", true);
                redraw = true;
            }
            if (state != Frontend_Image_State::Pending) {
                cover->RemoveAttribute("data-cover");
                return true;
            }
            return false;
        };
        std::erase_if(pendingCovers, load);
        if (adapterImagesPending) {
            Rml::ElementList icons;
            Rml::Element* adapter = Element("adapter");
            for (s32 index = 0; index < adapter->GetNumChildren(true); index++) {
                adapter->GetChild(index)->QuerySelectorAll(icons, "[data-cover]");
            }

            adapterImagesPending = false;
            for (Rml::Element* icon : icons) {
                adapterImagesPending |= !load(icon);
            }
        }
    }

    void Prefetch_Games(s32 first, s32 last) {
        s32 begin = std::max(0, first - gPrefetchCards / 2);
        s32 end = std::min(static_cast<s32>(filteredGames.size()), last + gPrefetchCards - (first - begin));
        if (begin == prefetchBegin && end == prefetchEnd) {
            return;
        }
        prefetchBegin = begin;
        prefetchEnd = end;
        for (s32 index = begin; index < end; index++) {
            const Frontend_Game& game = library.games[filteredGames[index]];
            if (!game.art.empty()) {
                window.Prepare_Image(Image_Source(game.art));
            }
            if (!game.logo.empty()) {
                window.Prepare_Image(Image_Source(game.logo));
            }
        }
    }

    void Update_Games() {
        Rml::Element* games = Element("games");
        if (filteredGames.empty() || !games->IsVisible(true)) {
            return;
        }
        Rml::Element* scroll = Element("games-scroll");
        Rml::Element* card = games->GetChild(0);
        float gap = games->ResolveLength(games->GetProperty(Rml::PropertyId::RowGap)->GetNumericValue());
        float row_height = card->ResolveLength(card->GetProperty(Rml::PropertyId::Height)->GetNumericValue()) + gap;
        float width = games->GetClientWidth();
        float scale = window.Context()->GetDensityIndependentPixelRatio();
        if (row_height <= 0 || width <= 0) {
            return;
        }
        s32 columns = library.view == "boxart" ? std::max(1, static_cast<s32>((width + gap) / (gBoxartCardWidth * scale + gap))) : 1;
        s32 card_width = static_cast<s32>((width - (columns - 1) * gap) / columns);
        bool resized = columns != gameColumns || card_width != cardWidth || row_height != rowHeight;
        if (resized) {
            gameColumns = columns;
            cardWidth = card_width;
            rowHeight = row_height;
            games->SetProperty("--game-card-width", std::to_string(card_width) + "px");
        }
        s32 count = static_cast<s32>(filteredGames.size());
        s32 rows = (count + columns - 1) / columns;
        float padding = scroll->GetBox().GetEdge(Rml::BoxArea::Padding, Rml::BoxEdge::Top);
        if (focusGame >= 0) {
            float top = padding + (focusGame / columns) * row_height;
            float bottom = top + row_height - gap;
            float scroll_top = scroll->GetScrollTop();
            if (top < scroll_top) {
                scroll->SetScrollTop(top);
            }
            else if (bottom > scroll_top + scroll->GetClientHeight()) {
                scroll->SetScrollTop(bottom - scroll->GetClientHeight());
            }
        }
        s32 first_visible = std::clamp(static_cast<s32>((scroll->GetScrollTop() - padding) / row_height), 0, rows - 1);
        s32 last_visible = std::min(rows, first_visible + static_cast<s32>(scroll->GetClientHeight() / row_height) + gPartialRows);
        Prefetch_Games(first_visible * columns, std::min(count, last_visible * columns));
        if (resized || firstRow < 0 || first_visible < firstRow + (firstRow > 0) ||
            last_visible > lastRow - (lastRow < rows)) {
            Rml::Element* focused = window.Context()->GetFocusElement();
            if (focusGame < 0 && focused != nullptr && focused->GetParentNode() == games) {
                auto found = std::ranges::find(filteredGames, focused->GetAttribute<usize>("data-index", 0));
                focusGame = static_cast<s32>(found - filteredGames.begin());
            }
            firstRow = std::max(0, first_visible - gOverscanRows);
            lastRow = std::min(rows, last_visible + gOverscanRows);
            std::string cards;
            for (s32 index = firstRow * columns; index < std::min(count, lastRow * columns); index++) {
                usize game = filteredGames[index];
                cards += Card(library.games[game], game, library.view);
            }
            Element("games-before")->SetProperty(Rml::PropertyId::Height, {firstRow * row_height, Rml::Unit::PX});
            Element("games-after")->SetProperty(Rml::PropertyId::Height, {(rows - lastRow) * row_height, Rml::Unit::PX});
            Set_Cards("games", cards);
        }
        if (focusGame >= firstRow * columns && focusGame < std::min(count, lastRow * columns)) {
            games->GetChild(focusGame - firstRow * columns)->Focus(true);
        }
        focusGame = -1;
    }

    void Build_Games() {
        filteredGames.clear();
        std::string search = Value("search");
        for (usize index = 0; index < library.games.size(); index++) {
            if (Matches(library.games[index].name, search)) {
                filteredGames.push_back(index);
            }
        }
        firstRow = -1;
        focusGame = -1;
        prefetchBegin = -1;
        Element("games-scroll")->SetScrollTop(0);
        Element("games-before")->SetProperty(Rml::PropertyId::Height, {0.f, Rml::Unit::PX});
        Element("games-after")->SetProperty(Rml::PropertyId::Height, {0.f, Rml::Unit::PX});
        Element("games")->SetClassNames("game-grid " + library.view);
        std::string cards;
        if (filteredGames.empty()) {
            cards = "<div class=\"empty-state\"><div class=\"empty-title\">" +
                std::string(library.games.empty() ? "Add your games" : "No matching games") +
                "</div><div class=\"muted\">" +
                (library.games.empty() ? "Choose a folder containing ROMs." : "Try another search.") +
                "</div><button class=\"secondary\" data-action=\"browse-roms\">Choose ROM folder</button></div>";
        }
        else {
            usize first = filteredGames.front();
            cards = Card(library.games[first], first, library.view);
        }
        Set_Cards("games", cards);
        Prefetch_Games(0, 0);
    }

    void Build_Selected_Game() {
        auto selected = std::ranges::find(library.games, library.selectedGame, &Frontend_Game::path);
        if (selected == library.games.end() && !library.games.empty()) {
            selected = library.games.begin();
            library.selectedGame = selected->path;
        }
        if (selected != library.games.end()) {
            Set_Cards("selected-game", Card(*selected, static_cast<usize>(selected - library.games.begin())));
        }
        else {
            Set_Cards("selected-game", "<div class=\"empty-state\"><div class=\"empty-title\">Choose a game</div>"
                "<div class=\"muted\">Add your ROM folder to get started.</div>"
                "<button class=\"secondary\" data-action=\"browse-roms\">Browse ROMs</button></div>");
        }
        document->QuerySelector(".launch-button")->SetClass("disabled", selected == library.games.end() || library.platform.empty());
    }

    void Build_General() {
        Build_Selected_Game();
        std::vector<usize> recent;
        for (usize index = 0; index < library.games.size(); index++) {
            if (library.games[index].lastPlayed != 0) {
                recent.push_back(index);
            }
        }
        usize count = std::min(recent.size(), gRecentGames);
        std::partial_sort(recent.begin(), recent.begin() + count, recent.end(), [&](usize left, usize right) {
            return library.games[left].lastPlayed > library.games[right].lastPlayed;
        });
        std::string cards;
        for (usize index : std::span(recent).first(count)) {
            cards += Card(library.games[index], index);
        }
        Set_Cards("recent-games", cards.empty() ?
            "<div class=\"empty-state compact\"><div class=\"muted\">Games you launch will appear here.</div></div>" : cards);
    }

    void Build_Mods() {
        std::string rows;
        for (usize index = 0; index < library.mods.size(); index++) {
            const Frontend_Mod& mod = library.mods[index];
            std::string description = Escape(mod.description);
            if (!mod.dependencies.empty()) {
                description += "<br/>Requires: ";
                for (usize dependency = 0; dependency < mod.dependencies.size(); dependency++) {
                    description += (dependency != 0 ? ", " : "") + Escape(mod.dependencies[dependency]);
                }
            }
            rows += "<label class=\"setting-row\"><div class=\"setting-info\"><div class=\"setting-title\">" +
                Escape(mod.name) + " " + Escape(mod.version) + "</div><div class=\"setting-description\">" + description +
                "</div></div><input class=\"checkbox\" type=\"checkbox\" data-mod=\"" + std::to_string(index) + "\"" +
                (mod.enabled ? " checked" : "") + "/></label>";
        }
        Element("mods")->SetInnerRML(rows.empty() ?
            "<div class=\"empty-state\"><div class=\"empty-title\">No mods found</div>"
            "<div class=\"muted\">Choose a folder containing your .pak or .wasm plugins and cores.</div></div>" : rows);
    }

    void Refresh_Library(bool reindex_artwork = false) {
        library.Scan(reindex_artwork);
        Build_General();
        Build_Games();
        Build_Preview();
        Build_Mods();
        Prepare_Mods();
    }

    std::vector<std::string> Enabled_Mods() {
        std::vector<std::string> paths;
        for (const Frontend_Mod& mod : library.mods) {
            if (mod.enabled) {
                paths.push_back(mod.path);
            }
        }
        return paths;
    }

    void Prepare_Mods() {
        preparation.Request(Enabled_Mods(), options.dataDirectory, clientSettings.Execution_Mode());
    }

    void Build_Directories() {
        for (const Frontend_Adapter& adapter : library.adapters) {
            library.romDirectories.try_emplace(adapter.platform);
        }
        std::string rows;
        for (const auto& [platform, directories] : library.romDirectories) {
            std::string attributes = " data-platform=\"" + Escape(platform) + "\"";
            rows += "<div><div class=\"rom-folders-header\"><span>" + Escape(platform) +
                " ROM folders</span><button class=\"secondary\" data-action=\"browse-roms\"" + attributes + ">Add folder</button></div>";
            for (usize index = 0; index < directories.size(); index++) {
                std::string directory_attributes = attributes + " data-directory=\"" + std::to_string(index) + "\"";
                rows += "<div class=\"rom-folder-row\"><input class=\"text-input\" value=\"" + Escape(directories[index]) + "\"" +
                    directory_attributes + "/><button class=\"secondary\" data-action=\"remove-rom-folder\"" +
                    directory_attributes + ">Remove</button></div>";
            }
            rows += "</div>";
        }
        Element("rom-directories")->SetInnerRML(rows);

        std::string artwork = Artwork_Row("", "Default artwork", "libretro-thumbnails folder for emulators without their own");
        for (usize index = 0; index < library.adapters.size(); index++) {
            const std::string& name = library.adapters[index].name;
            if (index == 0 || name != library.adapters[index - 1].name) {
                artwork += Artwork_Row(name, name + " artwork", "Folder with Named_Boxarts and Named_Logos; blank uses the default");
            }
        }
        Element("artwork-directories")->SetInnerRML(artwork);
    }

    std::string& Artwork_Directory(const std::string& adapter) {
        return adapter.empty() ? library.artworkDirectory : library.artworkDirectories[adapter];
    }

    std::string Artwork_Row(const std::string& adapter, const std::string& title, const char* description) {
        std::string attributes = " data-artwork=\"" + Escape(adapter) + "\"";
        return "<div class=\"setting-row\"><div class=\"setting-info\"><div class=\"setting-title\">" + Escape(title) +
            "</div><div class=\"setting-description\">" + description + "</div></div><div class=\"row-actions\"><input class=\"text-input\" value=\"" +
            Escape(Artwork_Directory(adapter)) + "\"" + attributes + "/><button class=\"secondary\" data-action=\"browse-artwork\"" + attributes +
            ">Browse</button></div></div>";
    }

    void Build_Config() {
        std::string rows;
        auto lock = Settings_Lock();
        for (const Setting& setting : group->Entries()) {
            if (!setting.Is_Visible()) {
                continue;
            }
            std::string attributes = " data-setting=\"" + Escape(setting.key) + "\"";
            std::string control;
            if (setting.type == MODLOADER_SETTING_BOOL) {
                control = "<input class=\"checkbox\" type=\"checkbox\"" + attributes +
                    (setting.value == "true" ? " checked" : "") + "/>";
            }
            else if (setting.type == MODLOADER_SETTING_CHOICE) {
                control = "<select class=\"compact-select\"" + attributes + ">";
                for (const Setting_Choice& choice : Setting_Choices(setting.choices)) {
                    control += "<option value=\"" + Escape(std::string(choice.value)) + "\"" +
                        (choice.value == setting.value ? " selected" : "") + ">" + Escape(std::string(choice.label)) + "</option>";
                }
                control += "</select>";
            }
            else {
                control = "<input class=\"text-input\" value=\"" + Escape(setting.value) + "\"" + attributes + "/>";
            }
            rows += "<div class=\"setting-row\"><div class=\"setting-info\"><div class=\"setting-title\">" +
                Escape(setting.label) + "</div><div class=\"setting-description\">" + Escape(setting.help) +
                "</div></div><div class=\"row-actions\">" + control + "</div></div>";
        }
        Element("client-settings")->SetInnerRML(rows);
        Build_Directories();
    }

    void Update_Online() {
        bool offline = Element("offline")->HasAttribute("checked");
        for (const char* field : { "server", "lobby", "password" }) {
            Control(field)->SetDisabled(offline);
        }
    }

    void Read_Form() {
        library.nickname = Value("nickname");
        library.server = Value("server");
        library.lobby = Value("lobby");
        library.offline = Element("offline")->HasAttribute("checked");
        library.pluginDirectory = Value("mod-directory");
        Rml::ElementList inputs;
        document->QuerySelectorAll(inputs, "input[data-directory], input[data-artwork]");
        for (Rml::Element* input : inputs) {
            std::string value = static_cast<Rml::ElementFormControl*>(input)->GetValue();
            if (input->HasAttribute("data-artwork")) {
                Artwork_Directory(input->GetAttribute<Rml::String>("data-artwork", "")) = value;
            }
            else {
                library.romDirectories[input->GetAttribute<Rml::String>("data-platform", "")][input->GetAttribute<usize>("data-directory", 0)] = value;
            }
        }
    }

    void Browse(const std::string& location, bool many, std::function<void(const std::vector<std::string>&)> apply) {
        if (dialog != nullptr) {
            return;
        }
        dialog = std::make_unique<Folder_Dialog>();
        dialog->apply = std::move(apply);
        SDL_ShowOpenFolderDialog(Folder_Selected, dialog.get(), window.Window(), location.c_str(), many);
    }

    void Finish_Dialog() {
        if (dialog == nullptr || !dialog->ready.load()) {
            return;
        }
        if (!dialog->error.empty()) {
            Status(dialog->error);
        }
        else if (!dialog->paths.empty()) {
            Read_Form();
            dialog->apply(dialog->paths);
            Refresh_Library();
            Status(std::to_string(library.games.size()) + " games / " + std::to_string(library.mods.size()) + " mods");
        }
        dialog.reset();
    }

    void Launch() {
        Read_Form();
        Refresh_Library();
        if (library.platform.empty()) {
            Status("Install an emulator in the adapters folder first.");
            return;
        }
        if (!File_Exists(library.selectedGame)) {
            Status("Choose an available game first.");
            return;
        }
        library.Set_Connection_Defaults();
        Set_Value("nickname", library.nickname);
        Set_Value("lobby", library.lobby);
        Set_Value("server", library.server);
        options.romPath = library.selectedGame;
        options.adapterPath = library.adapterPath;
        options.nickname = library.nickname;
        options.lobby = library.lobby;
        options.password = Value("password");
        options.connect = library.offline ? "" : library.server;
        options.offline = library.offline;
        options.modulePaths = Enabled_Mods();
        launch = true;
    }

    void Select_Game(usize index, bool open_general) {
        library.selectedGame = library.games[index].path;
        previewGame = library.selectedGame;
        Rml::ElementList cards;
        document->QuerySelectorAll(cards, ".game-card[data-index]");
        for (Rml::Element* card : cards) {
            card->SetClass("selected", card->GetAttribute<usize>("data-index", 0) == index);
        }
        Build_Selected_Game();
        Build_Preview();
        if (open_general) {
            Page("general");
        }
    }

    void ProcessEvent(Rml::Event& event) override {
        redraw = true;
        if (event == Rml::EventId::Resize) {
            document->UpdateDocument();
            Update_Games();
            return;
        }
        Rml::Element* target = event.GetTargetElement();
        if (event == Rml::EventId::Keydown) {
            if (target->GetParentNode() != Element("games")) {
                return;
            }
            s32 step = 0;
            switch (event.GetParameter<s32>("key_identifier", Rml::Input::KI_UNKNOWN)) {
            case Rml::Input::KI_TAB:
                step = event.GetParameter<bool>("shift_key", false) ? -1 : 1;
                break;
            case Rml::Input::KI_LEFT:
                step = -1;
                break;
            case Rml::Input::KI_RIGHT:
                step = 1;
                break;
            case Rml::Input::KI_UP:
                step = -gameColumns;
                break;
            case Rml::Input::KI_DOWN:
                step = gameColumns;
                break;
            default:
                return;
            }
            auto found = std::ranges::find(filteredGames, target->GetAttribute<usize>("data-index", 0));
            s32 next = (focusGame >= 0 ? focusGame : static_cast<s32>(found - filteredGames.begin())) + step;
            if (next >= 0 && next < static_cast<s32>(filteredGames.size())) {
                focusGame = next;
                event.StopPropagation();
            }
            return;
        }
        if (event == Rml::EventId::Mouseup) {
            if (!Element("games")->Contains(target)) {
                return;
            }
            target = Attribute_Target(target, "data-index");
            if (target == nullptr) {
                return;
            }
            if (event.GetParameter<s32>("button", -1) == gRightMouseButton) {
                Select_Game(target->GetAttribute<usize>("data-index", 0), false);
            }
            return;
        }
        if (event == Rml::EventId::Change) {
            std::string id = target->GetId();
            if (id == "adapter") {
                adapterImagesPending = true;
                Read_Form();
                library.adapterPath = Value("adapter");
                auto adapter = std::ranges::find(library.adapters, library.adapterPath, &Frontend_Adapter::path);
                library.platform = adapter != library.adapters.end() ? adapter->platform : "";
                Update_Adapter();
                Refresh_Library();
            }
            else if (target->HasAttribute("data-mod")) {
                usize index = target->GetAttribute<usize>("data-mod", 0);
                library.mods[index].enabled = target->HasAttribute("checked");
                Prepare_Mods();
            }
            else if (target->HasAttribute("data-setting")) {
                auto* control = static_cast<Rml::ElementFormControl*>(target);
                std::string key = target->GetAttribute<Rml::String>("data-setting", "");
                std::string value = target->GetAttribute<Rml::String>("type", "") == "checkbox" ?
                    (target->HasAttribute("checked") ? "true" : "false") : control->GetValue();
                if (!group->Set(key, value)) {
                    control->SetValue(group->Get(key).value_or(""));
                    Status("Invalid value for " + key);
                }
                else if (key == "wasm_mode") {
                    Prepare_Mods();
                }
            }
            else if (id == "view" || id == "sort" || id == "search") {
                library.view = Value("view");
                std::string sort = Value("sort");
                if (library.sort != sort) {
                    library.sort = std::move(sort);
                    library.Sort_Games();
                    Build_General();
                }
                Build_Games();
            }
            else if (id == "debug-gamecube-palette") {
                Update_Adapter();
            }
            else if (id == "offline") {
                Update_Online();
            }
            return;
        }
        target = Attribute_Target(target, "data-action");
        if (target == nullptr) {
            return;
        }
        std::string action = target->GetAttribute<Rml::String>("data-action", "");
        if (action == "window-minimize") {
            SDL_MinimizeWindow(window.Window());
        }
        else if (action == "window-maximize") {
            if (SDL_GetWindowFlags(window.Window()) & SDL_WINDOW_MAXIMIZED) {
                SDL_RestoreWindow(window.Window());
            }
            else {
                SDL_MaximizeWindow(window.Window());
            }
        }
        else if (action == "window-close") {
            closing = true;
        }
        else if (action == "page") {
            Page(target->GetAttribute<Rml::String>("data-page", "general"));
        }
        else if (action == "select-game") {
            Select_Game(target->GetAttribute<usize>("data-index", 0), true);
        }
        else if (action == "toggle-game-details") {
            library.showGameDetails = !library.showGameDetails;
            Build_Preview();
        }
        else if (action == "open-rom-folder") {
            if (!previewGame.empty() && !SDL_OpenURL(File_Url(Path_Directory(previewGame)).c_str())) {
                Status("Could not open the ROM folder");
            }
        }
        else if (action == "browse-roms") {
            std::string platform = target->GetAttribute<Rml::String>("data-platform", library.platform);
            if (!platform.empty()) {
                const auto& directories = library.romDirectories[platform];
                Browse(directories.empty() ? "" : directories.back(), true, [this, platform](const std::vector<std::string>& paths) {
                    auto& directories = library.romDirectories[platform];
                    for (const std::string& path : paths) {
                        if (std::ranges::find(directories, path) == directories.end()) {
                            directories.push_back(path);
                        }
                    }
                    Build_Directories();
                });
            }
        }
        else if (action == "remove-rom-folder") {
            Read_Form();
            std::string platform = target->GetAttribute<Rml::String>("data-platform", "");
            usize index = target->GetAttribute<usize>("data-directory", 0);
            auto& directories = library.romDirectories[platform];
            directories.erase(directories.begin() + index);
            Build_Directories();
            Refresh_Library();
        }
        else if (action == "browse-artwork") {
            std::string adapter = target->GetAttribute<Rml::String>("data-artwork", "");
            Browse(Artwork_Directory(adapter), false, [this, adapter](const std::vector<std::string>& paths) {
                Artwork_Directory(adapter) = paths.front();
                Build_Directories();
            });
        }
        else if (action == "browse-mods") {
            Browse(Value("mod-directory"), false, [this](const std::vector<std::string>& paths) {
                library.pluginDirectory = paths.front();
                Set_Value("mod-directory", library.pluginDirectory);
            });
        }
        else if (action == "rescan") {
            Read_Form();
            window.Refresh_Images();
            Build_Adapters();
            Refresh_Library(true);
            Status("Library refreshed");
        }
        else if (action == "launch") {
            Launch();
        }
    }

    Frontend_Window& window;
    Frontend_Library& library;
    Run_Options& options;
    Rml::ElementDocument* document = nullptr;
    std::unordered_map<std::string, Rml::Element*> elements;
    Client_Settings clientSettings;
    Settings_Group* group = Settings_Group_Find_Or_Create("client_settings", nullptr);
    std::unique_ptr<Folder_Dialog> dialog;
    Frontend_Aot_Preparation preparation;
    std::string assetDirectory;
    std::string previewGame;
    bool launch = false;
    bool closing = false;
    bool redraw = true;
    bool imagesDirty = true;
    bool adapterImagesPending = true;
    Rml::ElementList pendingCovers;
    std::vector<usize> filteredGames;
    s32 cardWidth = 0;
    s32 gameColumns = 0;
    float rowHeight = 0;
    s32 firstRow = -1;
    s32 lastRow = 0;
    s32 focusGame = -1;
    s32 prefetchBegin = -1;
    s32 prefetchEnd = -1;
};

} // namespace

s32 Frontend_Run(Run_Options options) {
    Frontend_Library library;
    library.Load(options.dataDirectory);
    std::string assets = Path_Join(Path_Executable_Directory(), "assets/frontend");
    std::string status = "Ready";
    for (;;) {
        bool opened = false;
        bool launch = false;
        Settings_Start(options.dataDirectory);
        {
            Frontend_Window window;
            Frontend frontend(window, library, options);
            opened = window.Open(assets) && frontend.Open(assets, status);
            launch = opened && frontend.Run();
        }
        Settings_Stop();
        if (!launch) {
            return opened ? 0 : 1;
        }
        library.Record_Play(options.romPath);
        u64 started = Time_Monotonic_Milliseconds();
        status = Session_Run(options) == 0 ? "Ready" : "The game could not start. Check the console for details.";
        library.Add_Playtime(options.romPath, (Time_Monotonic_Milliseconds() - started) / SDL_MS_PER_SECOND);
    }
}
