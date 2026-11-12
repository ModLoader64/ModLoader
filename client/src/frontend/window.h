#pragma once

#include <memory>
#include <string>

struct SDL_Window;
enum class Frontend_Image_State;
namespace Rml {
class Context;
}

void Frontend_Wake();

class Frontend_Window {
public:
    Frontend_Window();
    ~Frontend_Window();

    bool Open(const std::string& asset_directory);
    bool Poll(bool wait);
    bool Custom_Chrome() const;
    void Draw();
    Frontend_Image_State Prepare_Image(const std::string& source, bool priority = false, bool pinned = false);
    void Refresh_Images();
    SDL_Window* Window();
    Rml::Context* Context();

private:
    struct Backend;
    std::unique_ptr<Backend> backend;
};
