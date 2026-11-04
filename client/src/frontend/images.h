#pragma once

#include "base.h"
#include <RmlUi/Core/RenderInterface.h>

#include <condition_variable>
#include <deque>
#include <unordered_map>
#include <unordered_set>

enum class Frontend_Image_State {
    Pending,
    Ready,
    Failed,
};

class Frontend_Images {
public:
    explicit Frontend_Images(Rml::RenderInterface& renderer);
    ~Frontend_Images();

    Frontend_Image_State Prepare(const std::string& source, bool priority = false, bool pinned = false);
    bool Poll();
    bool Owns_Texture(Rml::TextureHandle texture) const;
    Rml::TextureHandle Load_Texture(Rml::Vector2i& dimensions, const Rml::String& source);
    void Clear(bool keep_pinned = false);

private:
    enum class Request_State {
        Pending,
        Priority,
        Finished,
    };

    struct Image {
        Rml::TextureHandle texture = 0;
        Rml::Vector2i dimensions = {};
        Request_State state = Request_State::Pending;
        bool pinned = false;
    };

    struct Job {
        std::string source;
        u64 generation = 0;
    };

    struct Result {
        Job request;
        std::vector<u8> pixels;
        Rml::Vector2i dimensions = {};
    };

    void Run();

    Rml::RenderInterface& renderer;
    std::unordered_map<std::string, Image> images;
    std::unordered_set<Rml::TextureHandle> ownedTextures;
    Thread worker;
    std::mutex lock;
    std::condition_variable changed;
    std::deque<Job> pending;
    std::deque<Job> priorityPending;
    std::deque<Result> ready;
    u64 generation = 0;
    bool stopping = false;
};
