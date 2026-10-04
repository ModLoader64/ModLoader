#include "renderer_internal.h"

#include <math.h>

namespace {

struct Name {
    const char* text;
    u32 value;
};

constexpr Name gScalingNames[] = {
    { "fit", Renderer_Scaling_Fit },
    { "fill", Renderer_Scaling_Fill },
    { "stretch", Renderer_Scaling_Stretch },
    { "integer", Renderer_Scaling_Integer },
};

constexpr Name gFilterNames[] = {
    { "nearest", Renderer_Filter_Nearest },
    { "bilinear", Renderer_Filter_Bilinear },
    { "sharp_bilinear", Renderer_Filter_Sharp_Bilinear },
    { "area", Renderer_Filter_Area },
    { "bicubic", Renderer_Filter_Bicubic },
    { "lanczos", Renderer_Filter_Lanczos },
    { "kaiser", Renderer_Filter_Kaiser },
};

u32 From_Name(std::span<const Name> names, std::string_view text) {
    for (const Name& name : names) {
        if (text == name.text) {
            return name.value;
        }
    }
    return names[0].value;
}

// Column-major product: out = left * right
void Multiply(const f32 left[16], const f32 right[16], f32 out_matrix[16]) {
    f32 sum;

    for (u32 column = 0; column < 4; column++) {
        for (u32 row = 0; row < 4; row++) {
            sum = 0.0f;
            for (u32 index = 0; index < 4; index++) {
                sum += left[index * 4 + row] * right[column * 4 + index];
            }
            out_matrix[column * 4 + row] = sum;
        }
    }
}

} // namespace

Renderer_Scaling Renderer_Scaling_From_Name(std::string_view name) {
    return static_cast<Renderer_Scaling>(From_Name(gScalingNames, name));
}

Renderer_Filter Renderer_Filter_From_Name(std::string_view name) {
    return static_cast<Renderer_Filter>(From_Name(gFilterNames, name));
}

Image_Constants Image_Constants_For(const Renderer_Image& image, u32 surface_width, u32 surface_height) {
    const f32* rect = image.destination;
    Image_Constants constants = {};

    constants.clip[0] = 2.0f * rect[0] / surface_width - 1.0f;
    constants.clip[1] = 2.0f * rect[1] / surface_height - 1.0f;
    constants.clip[2] = 2.0f * (rect[0] + rect[2]) / surface_width - 1.0f;
    constants.clip[3] = 2.0f * (rect[1] + rect[3]) / surface_height - 1.0f;
    constants.sourceSize[0] = static_cast<f32>(image.texture->width);
    constants.sourceSize[1] = static_cast<f32>(image.texture->height);
    constants.destinationSize[0] = rect[2];
    constants.destinationSize[1] = rect[3];
    constants.filter = image.filter < Renderer_Filter_Count ? image.filter : Renderer_Filter_Bilinear;
    return constants;
}

void Mesh_Transform(const Renderer_Mesh& mesh, u32 surface_width, u32 surface_height, f32 out_transform[16]) {
    f32 projection[16] = { 2.0f / surface_width, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f / surface_height, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 1.0f };
    f32 translation[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, mesh.translation[0], mesh.translation[1], 0.0f, 1.0f };
    f32 placed[16];

    if (mesh.transform != nullptr) {
        Multiply(mesh.transform, translation, placed);
        Multiply(projection, placed, out_transform);
    }
    else {
        Multiply(projection, translation, out_transform);
    }
}

void Renderer_Place_Image(
    Renderer_Scaling scaling,
    u32 surface_width,
    u32 surface_height,
    u32 source_width,
    u32 source_height,
    f32 aspect,
    f32 out_destination[4]
) {
    f32 width = static_cast<f32>(surface_width);
    f32 height = static_cast<f32>(surface_height);
    f32 multiple;

    if (aspect <= 0.0f) {
        aspect = source_height != 0 ? static_cast<f32>(source_width) / source_height : 4.0f / 3.0f;
    }

    if (scaling == Renderer_Scaling_Integer && source_height != 0) {
        multiple = floorf(fminf(height / source_height, width / (source_height * aspect)));
        if (multiple >= 1.0f) {
            height = multiple * source_height;
            width = roundf(height * aspect);
        }
        else {
            scaling = Renderer_Scaling_Fit;
        }
    }

    if (scaling == Renderer_Scaling_Fit || scaling == Renderer_Scaling_Fill) {
        if ((width / height > aspect) == (scaling == Renderer_Scaling_Fit)) {
            width = roundf(height * aspect);
        }
        else {
            height = roundf(width / aspect);
        }
    }
    
    out_destination[0] = floorf((surface_width - width) * 0.5f);
    out_destination[1] = floorf((surface_height - height) * 0.5f);
    out_destination[2] = width;
    out_destination[3] = height;
}
