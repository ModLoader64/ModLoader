#pragma once

#include "base.h"
#include <map>
#include <unordered_map>

struct Frontend_Game {
    std::string path;
    std::string name;
    std::string art;
    std::string logo;
    u64 size = 0;
    u64 lastPlayed = 0;
    u64 playtime = 0;
};

struct Frontend_Mod {
    std::string path;
    std::string name;
    std::string version;
    std::string description;
    std::vector<std::string> dependencies;
    bool enabled = false;
};

struct Frontend_Adapter {
    std::string path;
    std::string name;
    std::string platform;
};

class Frontend_Library {
public:
    void Load(const std::string& data_directory);
    void Save();
    void Set_Connection_Defaults();
    void Scan(bool reindex_artwork = false);
    void Sort_Games();
    void Record_Play(const std::string& path);
    void Add_Playtime(const std::string& path, u64 seconds);

    std::vector<Frontend_Game> games;
    std::vector<Frontend_Mod> mods;
    std::vector<Frontend_Adapter> adapters;
    std::map<std::string, std::vector<std::string>> romDirectories;
    std::map<std::string, std::string> artworkDirectories;
    std::string artworkDirectory;
    std::string pluginDirectory;
    std::string adapterPath;
    std::string platform;
    std::string nickname;
    std::string server;
    std::string lobby;
    bool offline = true;
    std::string selectedGame;
    std::string view = "boxart";
    std::string sort = "name";
    bool showGameDetails = true;

private:
    void Scan_Adapters();
    void Store_Mod_Selections();
    void Index_Artwork(const std::string& root);

    std::string configPath;
    std::string artworkRoot;
    std::unordered_map<std::string, std::string> boxarts;
    std::unordered_map<std::string, std::string> logos;
    std::map<std::string, u64> recent;
    std::map<std::string, u64> playtime;
    std::map<std::string, bool> modSelections;
};
