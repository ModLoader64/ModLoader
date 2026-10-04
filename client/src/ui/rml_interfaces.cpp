#include "ui/windows_internal.h"

#include "RmlUi/Core.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#include "stb_image.h"

#include <stdio.h>
#include <string.h>
#include <limits.h>

namespace {

struct Cursor_Name {
    const char* name;
    SDL_SystemCursor cursor;
};

constexpr Cursor_Name gCursors[] = {
    { "move", SDL_SYSTEM_CURSOR_MOVE },
    { "pointer", SDL_SYSTEM_CURSOR_POINTER },
    { "hand", SDL_SYSTEM_CURSOR_POINTER },
    { "resize", SDL_SYSTEM_CURSOR_NWSE_RESIZE },
    { "cross", SDL_SYSTEM_CURSOR_CROSSHAIR },
    { "text", SDL_SYSTEM_CURSOR_TEXT },
    { "unavailable", SDL_SYSTEM_CURSOR_NOT_ALLOWED },
    { "rmlui-scroll-idle", SDL_SYSTEM_CURSOR_MOVE },
};

struct Geometry {
    std::vector<Renderer_Vertex> vertices;
    std::vector<u32> indices;
};

class System final : public Rml::SystemInterface {
public:
    void JoinPath(Rml::String& translated, const Rml::String& document, const Rml::String& path) override {
        usize folder_start = document.starts_with("@assets/") ? 8 : 0;
        usize folder_end = document.find('/', folder_start);
        if (folder_end == Rml::String::npos) {
            translated.clear();
            return;
        }

        Rml::String folder = document.substr(folder_start, folder_end - folder_start) + "/";
        Rml::String root = document.substr(0, folder_end + 1);
        Rml::String source = document;
        Rml::String relative = path;
        if (relative.starts_with("assets:/")) {
            root = "@assets/" + folder;
            source = root + "_inline.rml";
            relative.erase(0, 8);
        }
        else if (relative.starts_with('/')) {
            root = folder;
            source = root + "_inline.rml";
        }

        if (relative.starts_with('/')) {
            relative.erase(0, relative.find_first_not_of('/'));
        }

        Rml::SystemInterface::JoinPath(translated, source, relative);
        if (!translated.starts_with(root)) {
            translated.clear();
        }
    }

    double GetElapsedTime() override {
        return static_cast<double>(SDL_GetTicksNS()) / 1e9;
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        Log_Level level = Log_Level::Debug;

        if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT) {
            level = Log_Level::Error;
        }
        else if (type == Rml::Log::LT_WARNING) {
            level = Log_Level::Warning;
        }
        else if (type == Rml::Log::LT_INFO) {
            level = Log_Level::Info;
        }

        Log_Write(level, gWindows.worker != nullptr ? gWindows.worker->name.c_str() : "rmlui", "%s", message.c_str());
        return true;
    }

    void SetMouseCursor(const Rml::String& name) override {
        SDL_SystemCursor cursor = SDL_SYSTEM_CURSOR_DEFAULT;

        if (gWindows.current == nullptr || SDL_GetMouseFocus() != Shown_In(*gWindows.current)) {
            return;
        }

        for (const Cursor_Name& pair : gCursors) {
            cursor = name == pair.name ? pair.cursor : cursor;
        }

        if (gWindows.cursors[cursor] == nullptr) {
            gWindows.cursors[cursor] = SDL_CreateSystemCursor(cursor);
        }

        SDL_SetCursor(gWindows.cursors[cursor]);
    }

    void SetClipboardText(const Rml::String& text) override {
        SDL_SetClipboardText(text.c_str());
    }

    void GetClipboardText(Rml::String& text) override {
        char* clipboard = SDL_GetClipboardText();
        text = clipboard != nullptr ? clipboard : "";
        SDL_free(clipboard);
    }

    void ActivateKeyboard(Rml::Vector2f, float) override {
        if (gWindows.current != nullptr && gWindows.current->overlay) {
            SDL_StartTextInput(gWindows.main);
        }
    }
};

class Files final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override {
        std::optional<std::string> full = gWindows.worker != nullptr ? Module_File_Path(*gWindows.worker, path) : std::nullopt;
        return reinterpret_cast<Rml::FileHandle>(full && !Directory_Exists(*full) ? fopen(full->c_str(), "rb") : nullptr);
    }

    void Close(Rml::FileHandle file) override {
        fclose(reinterpret_cast<FILE*>(file));
    }

    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
        return fread(buffer, 1, size, reinterpret_cast<FILE*>(file));
    }

    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        return fseek(reinterpret_cast<FILE*>(file), offset, origin) == 0;
    }

    size_t Tell(Rml::FileHandle file) override {
        return static_cast<size_t>(ftell(reinterpret_cast<FILE*>(file)));
    }
};

class Render final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override {
        Geometry* geometry = new Geometry();
        geometry->vertices.resize(vertices.size());
        for (usize index = 0; index < vertices.size(); index++) {
            geometry->vertices[index] = { vertices[index].position.x, vertices[index].position.y, vertices[index].tex_coord.x, vertices[index].tex_coord.y, 0 };
            memcpy(&geometry->vertices[index].color, &vertices[index].colour, sizeof(u32));
        }

        geometry->indices.assign(indices.begin(), indices.end());
        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
    }

    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override {
        const Geometry* geometry = reinterpret_cast<const Geometry*>(handle);
        Module_Window* window = gWindows.current;
        Window_Frame* frame = window != nullptr ? &window->frame : nullptr;
        Frame_Batch* batch;

        if (frame == nullptr || geometry == nullptr) {
            return;
        }

        batch = !frame->batches.empty() ? &frame->batches.back() : nullptr;
        if (batch == nullptr || batch->transformed != window->transformed ||
            (window->transformed && memcmp(batch->transform, window->transform, sizeof(window->transform)) != 0)) {
            batch = &frame->batches.emplace_back();
            batch->transformed = window->transformed;
            memcpy(batch->transform, window->transform, sizeof(window->transform));
            batch->firstDraw = static_cast<u32>(frame->draws.size());
            batch->drawCount = 0;
        }

        frame->draws.push_back(
            {
                reinterpret_cast<Renderer_Texture*>(texture),
                { window->scissored ? window->scissor[0] : 0,
                  window->scissored ? window->scissor[1] : 0,
                  window->scissored ? window->scissor[2] : INT32_MAX,
                  window->scissored ? window->scissor[3] : INT32_MAX },
                static_cast<u32>(frame->indices.size()),
                static_cast<u32>(geometry->indices.size()),
                static_cast<u32>(frame->vertices.size()),
            }
        );

        batch->drawCount++;
        for (Renderer_Vertex vertex : geometry->vertices) {
            vertex.x += translation.x;
            vertex.y += translation.y;
            frame->vertices.push_back(vertex);
        }

        frame->indices.insert(frame->indices.end(), geometry->indices.begin(), geometry->indices.end());
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override {
        delete reinterpret_cast<Geometry*>(handle);
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
        std::optional<std::string> full = gWindows.worker != nullptr ? Module_File_Path(*gWindows.worker, source) : std::nullopt;
        std::optional<std::vector<u8>> file = full ? File_Read(*full) : std::nullopt;
        s32 width = 0;
        s32 height = 0;
        s32 channels;
        u32 size_limit = gWindows.renderer->Texture_Size_Limit();

        if (!file || file->size() > INT_MAX ||
            !stbi_info_from_memory(file->data(), static_cast<int>(file->size()), &width, &height, &channels) ||
            width <= 0 || height <= 0 || static_cast<u32>(width) > size_limit || static_cast<u32>(height) > size_limit) {
            return 0;
        }

        std::unique_ptr<u8, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(file->data(), static_cast<int>(file->size()), &width, &height, &channels, 4), stbi_image_free
        );

        if (pixels == nullptr) {
            return 0;
        }

        for (u8* pixel = pixels.get(); pixel < pixels.get() + static_cast<s64>(width) * height * 4; pixel += 4) {
            pixel[0] = static_cast<u8>((pixel[0] * pixel[3] + 127) / 255);
            pixel[1] = static_cast<u8>((pixel[1] * pixel[3] + 127) / 255);
            pixel[2] = static_cast<u8>((pixel[2] * pixel[3] + 127) / 255);
        }

        std::unique_ptr<Renderer_Texture> texture = gWindows.renderer->Create_Texture(static_cast<u32>(width), static_cast<u32>(height), Renderer_Format_Rgba8, pixels.get());
        dimensions = Rml::Vector2i(width, height);
        return reinterpret_cast<Rml::TextureHandle>(texture.release());
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override {
        if (dimensions.x <= 0 || dimensions.y <= 0 || static_cast<u64>(dimensions.x) * static_cast<u64>(dimensions.y) > source.size() / 4) {
            return 0;
        }
        
        return reinterpret_cast<Rml::TextureHandle>(
            gWindows.renderer->Create_Texture(static_cast<u32>(dimensions.x), static_cast<u32>(dimensions.y), Renderer_Format_Rgba8, source.data()).release()
        );
    }

    void ReleaseTexture(Rml::TextureHandle texture) override {
        delete reinterpret_cast<Renderer_Texture*>(texture);
    }

    void EnableScissorRegion(bool enable) override {
        if (gWindows.current != nullptr) {
            gWindows.current->scissored = enable;
        }
    }

    void SetScissorRegion(Rml::Rectanglei region) override {
        if (gWindows.current != nullptr) {
            gWindows.current->scissor[0] = region.Left();
            gWindows.current->scissor[1] = region.Top();
            gWindows.current->scissor[2] = region.Width();
            gWindows.current->scissor[3] = region.Height();
        }
    }

    void SetTransform(const Rml::Matrix4f* transform) override {
        if (gWindows.current == nullptr) {
            return;
        }
        gWindows.current->transformed = transform != nullptr;
        if (transform != nullptr) {
            memcpy(gWindows.current->transform, transform->data(), sizeof(gWindows.current->transform));
        }
    }
};

System sSystem;
Files sFiles;
Render sRender;

} // namespace

void Window_Frame::Clear() {
    vertices.clear();
    indices.clear();
    draws.clear();
    batches.clear();
}

void Window_Frame::Draw(Renderer& renderer) const {
    Renderer_Mesh mesh;

    mesh.vertices = vertices;
    mesh.indices = indices;
    mesh.premultiplied = true;
    for (const Frame_Batch& batch : batches) {
        mesh.draws = std::span(draws).subspan(batch.firstDraw, batch.drawCount);
        mesh.transform = batch.transformed ? batch.transform : nullptr;
        renderer.Draw_Mesh(mesh);
    }
}

void Rml_Interfaces_Install() {
    Rml::SetSystemInterface(&sSystem);
    Rml::SetFileInterface(&sFiles);
    Rml::SetRenderInterface(&sRender);
}
