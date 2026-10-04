// Main thread only.
#pragma once

#include "base.h"
#include "modloader_video.h"

#include <memory>

struct SDL_Window;

enum Renderer_Scaling : u32 {
    Renderer_Scaling_Fit,
    Renderer_Scaling_Fill,
    Renderer_Scaling_Stretch,
    Renderer_Scaling_Integer,
};

enum Renderer_Filter : u32 {
    Renderer_Filter_Nearest,
    Renderer_Filter_Bilinear,
    Renderer_Filter_Sharp_Bilinear,
    Renderer_Filter_Area,
    Renderer_Filter_Bicubic,
    Renderer_Filter_Lanczos,
    Renderer_Filter_Kaiser,
    Renderer_Filter_Count,
};

enum Renderer_Format : u32 {
    Renderer_Format_Rgba8 = 0,
    Renderer_Format_Rgba16 = 1,
};

class Renderer_Texture {
public:
    virtual ~Renderer_Texture() = default;

    u32 width = 0;
    u32 height = 0;
    Renderer_Format format = Renderer_Format_Rgba8;
};

class Renderer_Surface {
public:
    virtual ~Renderer_Surface() = default;
};

struct Renderer_Vertex {
    f32 x;
    f32 y;
    f32 u;
    f32 v;
    u32 color; // RGBA8, R in the low byte
};

struct Renderer_Draw {
    Renderer_Texture* texture;
    s32 scissor[4]; // x, y, width, height
    u32 indexOffset;
    u32 indexCount;
    u32 vertexOffset;
};

struct Renderer_Mesh {
    std::span<const Renderer_Vertex> vertices;
    std::span<const u32> indices;
    std::span<const Renderer_Draw> draws;
    f32 translation[2] = {}; // applied before transform
    const f32* transform = nullptr; // null is identity
    bool premultiplied = false; // RmlUi uses premultiplied colors and textures
};

struct Renderer_Image {
    Renderer_Texture* texture = nullptr;
    f32 destination[4] = {}; // x, y, width, height
    Renderer_Filter filter = Renderer_Filter_Bilinear;
    u64 readyValue = 0;
};

class Renderer {
public:
    static std::unique_ptr<Renderer> Create();
    virtual ~Renderer() = default;

    virtual void Gpu_Uuid(u8 out_uuid[16]) const = 0;
    virtual u32 Texture_Size_Limit() const = 0;
    virtual std::unique_ptr<Renderer_Surface> Create_Surface(SDL_Window* window) = 0;
    virtual std::unique_ptr<Renderer_Texture> Create_Texture(u32 width, u32 height, Renderer_Format format, const void* pixels) = 0;
    virtual void Update_Texture(Renderer_Texture& texture, u32 x, u32 y, u32 width, u32 height, const void* pixels, u32 pitch) = 0;
    virtual std::unique_ptr<Renderer_Texture> Import_Texture(const ModLoader_Gpu_Image& image, u32 width, u32 height) = 0;
    virtual void Release_Texture(Renderer_Texture& texture, u64 value) = 0;

    void Begin_Frame() {
        framePresented = false;
    }

    bool Frame_Presented() const {
        return framePresented;
    }

    virtual bool Begin(Renderer_Surface& surface, u32& out_width, u32& out_height) = 0;
    virtual void Draw_Image(const Renderer_Image& image) = 0;
    virtual void Draw_Mesh(const Renderer_Mesh& mesh) = 0;
    virtual void End() = 0;

protected:
    bool framePresented = false;
};

Renderer_Scaling Renderer_Scaling_From_Name(std::string_view name);
Renderer_Filter Renderer_Filter_From_Name(std::string_view name);

void Renderer_Place_Image(
    Renderer_Scaling scaling,
    u32 surface_width,
    u32 surface_height,
    u32 source_width,
    u32 source_height,
    f32 aspect,
    f32 out_destination[4]
);
