#pragma once

#include "renderer.h"

// Must match shaders/image.hlsl
struct Image_Constants {
    f32 clip[4]; // clip-space left, top, right, bottom
    f32 sourceSize[2];
    f32 destinationSize[2];
    u32 filter;
    f32 sharpness;
    u32 padding[2];
};

Image_Constants Image_Constants_For(const Renderer_Image& image, u32 surface_width, u32 surface_height);
void Mesh_Transform(const Renderer_Mesh& mesh, u32 surface_width, u32 surface_height, f32 out_transform[16]);
