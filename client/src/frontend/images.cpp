#include "frontend/images.h"
#include "frontend/window.h"
#include "stb_image.h"

#include <limits.h>
#include <utility>

namespace {

constexpr usize gReadyLimit = 4;
constexpr usize gUploadsPerPoll = 2;

void Decode_Image(const std::string& source, std::vector<u8>& output, Rml::Vector2i& dimensions) {
    auto file = File_Read(source);
    if (!file || file->size() > INT_MAX) {
        return;
    }

    s32 width = 0;
    s32 height = 0;
    u8* pixels = stbi_load_from_memory(file->data(), static_cast<s32>(file->size()), &width, &height, nullptr, STBI_rgb_alpha);
    file.reset();
    if (pixels == nullptr) {
        return;
    }

    const usize byte_count = static_cast<usize>(width) * static_cast<usize>(height) * STBI_rgb_alpha;
    for (usize index = 0; index < byte_count; index += STBI_rgb_alpha) {
        for (usize channel = 0; channel < STBI_rgb; channel++) {
            pixels[index + channel] = static_cast<u8>((pixels[index + channel] * pixels[index + STBI_rgb] + UINT8_MAX / 2) / UINT8_MAX);
        }
    }

    output.assign(pixels, pixels + byte_count);
    stbi_image_free(pixels);
    dimensions = {width, height};
}

}

Frontend_Images::Frontend_Images(Rml::RenderInterface& renderer) : renderer(renderer) {
    if (!worker.Start([this]() {
        Run();
    })) {
        stopping = true;
        Log_Error("frontend", "Failed to start image worker");
    }
}

Frontend_Images::~Frontend_Images() {
    {
        std::lock_guard guard(lock);
        stopping = true;
    }
    changed.notify_all();
    worker.Join();
    Clear();
}

Frontend_Image_State Frontend_Images::Prepare(const std::string& source, bool priority, bool pinned) {
    auto [entry, inserted] = images.try_emplace(source);
    Image& image = entry->second;
    image.pinned |= pinned;
    if (inserted && stopping) {
        image.state = Request_State::Finished;
    }
    else if (inserted) {
        image.state = priority ? Request_State::Priority : Request_State::Pending;
        {
            std::lock_guard guard(lock);
            auto& queue = priority ? priorityPending : pending;
            queue.push_back({source, generation});
        }
        changed.notify_one();
    }
    else if (priority && image.state == Request_State::Pending) {
        image.state = Request_State::Priority;
        std::lock_guard guard(lock);
        for (auto request = pending.begin(); request != pending.end(); ++request) {
            if (request->source == source) {
                priorityPending.push_back(std::move(*request));
                pending.erase(request);
                break;
            }
        }
    }
    if (image.texture != 0) {
        return Frontend_Image_State::Ready;
    }
    return image.state == Request_State::Finished ? Frontend_Image_State::Failed : Frontend_Image_State::Pending;
}

bool Frontend_Images::Poll() {
    for (usize index = 0; index < gUploadsPerPoll; index++) {
        Result result;
        {
            std::lock_guard guard(lock);
            if (ready.empty()) {
                return false;
            }
            result = std::move(ready.front());
            ready.pop_front();
        }
        changed.notify_one();
        Image& image = images.at(result.request.source);
        image.state = Request_State::Finished;
        if (result.pixels.empty()) {
            continue;
        }
        image.dimensions = result.dimensions;
        image.texture = renderer.GenerateTexture({result.pixels.data(), result.pixels.size()}, result.dimensions);
        if (image.texture != 0) {
            ownedTextures.insert(image.texture);
        }
    }
    std::lock_guard guard(lock);
    return !ready.empty();
}

bool Frontend_Images::Owns_Texture(Rml::TextureHandle texture) const {
    return ownedTextures.contains(texture);
}

Rml::TextureHandle Frontend_Images::Load_Texture(Rml::Vector2i& dimensions, const Rml::String& source) {
    Prepare(source);
    const Image& image = images.at(source);
    dimensions = image.dimensions;
    return image.texture;
}

void Frontend_Images::Clear(bool keep_pinned) {
    {
        std::lock_guard guard(lock);
        generation++;
        pending.clear();
        priorityPending.clear();
        ready.clear();
    }
    changed.notify_all();
    for (auto image = images.begin(); image != images.end();) {
        if (keep_pinned && image->second.pinned && image->second.texture != 0) {
            ++image;
            continue;
        }
        const Rml::TextureHandle texture = image->second.texture;
        image = images.erase(image);
        if (texture != 0) {
            ownedTextures.erase(texture);
            renderer.ReleaseTexture(texture);
        }
    }
}

void Frontend_Images::Run() {
    for (;;) {
        Result result;
        {
            std::unique_lock guard(lock);
            changed.wait(guard, [this]() {
                return stopping || !priorityPending.empty() || !pending.empty();
            });
            if (stopping) {
                return;
            }
            auto& queue = priorityPending.empty() ? pending : priorityPending;
            result.request = std::move(queue.front());
            queue.pop_front();
        }

        Decode_Image(result.request.source, result.pixels, result.dimensions);

        std::unique_lock guard(lock);
        changed.wait(guard, [this, &result]() {
            return stopping || result.request.generation != generation || ready.size() < gReadyLimit;
        });
        if (stopping) {
            return;
        }
        if (result.request.generation == generation) {
            ready.push_back(std::move(result));
            Frontend_Wake();
        }
    }
}
