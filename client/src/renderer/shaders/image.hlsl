#define FILTER_NEAREST 0
#define FILTER_BILINEAR 1
#define FILTER_SHARP_BILINEAR 2
#define FILTER_AREA 3
#define FILTER_BICUBIC 4
#define FILTER_LANCZOS 5
#define FILTER_KAISER 6

#define MAX_TAPS 12
#define PI 3.14159265

struct Constants_Data {
    float4 clip;            // the destination in clip space: left, top, right, bottom
    float2 sourceSize;      // texels
    float2 destinationSize; // pixels
    uint filter;
    uint3 padding;
};

[[vk::push_constant]] Constants_Data gConstants;
[[vk::binding(0, 0)]] Texture2D gSource;
[[vk::binding(1, 0)]] SamplerState gLinear;

struct Pixel_Input {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

// Triangle strip: TL, TR, BL, BR
Pixel_Input Vertex_Main(uint vertex : SV_VertexID) {
    float2 corner = float2(vertex & 1, vertex >> 1);
    Pixel_Input output;

    output.position = float4(lerp(gConstants.clip.x, gConstants.clip.z, corner.x), lerp(gConstants.clip.y, gConstants.clip.w, corner.y), 0.0, 1.0);
    output.uv = corner;
    return output;
}

float4 Texel(int2 position) {
    int2 last = int2(gConstants.sourceSize) - 1;

    return gSource.Load(int3(clamp(position, int2(0, 0), last), 0));
}

float Sinc(float x) {
    x *= PI;
    return abs(x) < 1e-4 ? 1.0 : sin(x) / x;
}

float Bessel_I0(float x) {
    float sum = 1.0;
    float term = 1.0;
    float half_x = x * 0.5;

    [unroll]
    for (int k = 1; k < 12; k++) {
        term *= (half_x / k) * (half_x / k);
        sum += term;
    }
    return sum;
}

float Window(float t, bool kaiser) {
    const float beta = 4.0;

    if (abs(t) >= 1.0) {
        return 0.0;
    }
    return kaiser ? Bessel_I0(beta * sqrt(1.0 - t * t)) / Bessel_I0(beta) : Sinc(t);
}

float4 Windowed_Sinc(float2 position, float radius, bool kaiser) {
    float2 stretch = max(gConstants.sourceSize / gConstants.destinationSize, 1.0);
    float2 support = radius * stretch;
    int2 first = int2(floor(position - support + 0.5));
    int2 last = min(int2(floor(position + support - 0.5)), first + MAX_TAPS - 1);
    float4 sum = 0.0;
    float total = 0.0;
    float weight_y;
    float weight;
    float offset;

    [loop]
    for (int y = 0; y < MAX_TAPS; y++) {
        if (first.y + y > last.y) {
            break;
        }
        offset = (first.y + y + 0.5 - position.y) / stretch.y;
        weight_y = Sinc(offset) * Window(offset / radius, kaiser);
        [loop]
        for (int x = 0; x < MAX_TAPS; x++) {
            if (first.x + x > last.x) {
                break;
            }
            offset = (first.x + x + 0.5 - position.x) / stretch.x;
            weight = weight_y * Sinc(offset) * Window(offset / radius, kaiser);
            sum += Texel(first + int2(x, y)) * weight;
            total += weight;
        }
    }
    return total != 0.0 ? sum / total : Texel(int2(floor(position)));
}

float4 Area(float2 position) {
    float2 footprint = gConstants.sourceSize / gConstants.destinationSize;
    float2 low = position - footprint * 0.5;
    float2 high = position + footprint * 0.5;
    int2 first = int2(floor(low));
    int2 last = min(int2(ceil(high)) - 1, first + MAX_TAPS - 1);
    float4 sum = 0.0;
    float total = 0.0;
    float cover_y;
    float cover;

    [loop]
    for (int y = 0; y < MAX_TAPS; y++) {
        if (first.y + y > last.y) {
            break;
        }
        cover_y = min(high.y, first.y + y + 1.0) - max(low.y, first.y + y);
        [loop]
        for (int x = 0; x < MAX_TAPS; x++) {
            if (first.x + x > last.x) {
                break;
            }
            cover = cover_y * (min(high.x, first.x + x + 1.0) - max(low.x, first.x + x));
            sum += Texel(first + int2(x, y)) * cover;
            total += cover;
        }
    }
    return total != 0.0 ? sum / total : Texel(int2(floor(position)));
}

float Catmull_Rom(float x) {
    x = abs(x);
    if (x < 1.0) {
        return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    }
    if (x < 2.0) {
        return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    }
    return 0.0;
}

float4 Bicubic(float2 position) {
    float2 center = position - 0.5;
    int2 base = int2(floor(center));
    float2 fraction = center - base;
    float4 sum = 0.0;
    float weight_y;

    [unroll]
    for (int y = -1; y <= 2; y++) {
        weight_y = Catmull_Rom(y - fraction.y);
        [unroll]
        for (int x = -1; x <= 2; x++) {
            sum += Texel(base + int2(x, y)) * weight_y * Catmull_Rom(x - fraction.x);
        }
    }
    return sum;
}

float4 Sharp_Bilinear(float2 position) {
    float2 scale = max(floor(gConstants.destinationSize / gConstants.sourceSize), 1.0);
    float2 texel = floor(position);
    float2 center = position - texel - 0.5;
    float2 region = 0.5 - 0.5 / scale;
    float2 fraction = (center - clamp(center, -region, region)) * scale + 0.5;

    return gSource.SampleLevel(gLinear, (texel + fraction) / gConstants.sourceSize, 0.0);
}

float4 Pixel_Main(Pixel_Input input) : SV_Target {
    float2 position = input.uv * gConstants.sourceSize;
    float4 color;

    switch (gConstants.filter) {
    case FILTER_BILINEAR:
        color = gSource.SampleLevel(gLinear, input.uv, 0.0);
        break;
    case FILTER_SHARP_BILINEAR:
        color = Sharp_Bilinear(position);
        break;
    case FILTER_AREA:
        color = Area(position);
        break;
    case FILTER_BICUBIC:
        color = Bicubic(position);
        break;
    case FILTER_LANCZOS:
        color = Windowed_Sinc(position, 3.0, false);
        break;
    case FILTER_KAISER:
        color = Windowed_Sinc(position, 3.0, true);
        break;
    default:
        color = Texel(int2(floor(position)));
        break;
    }
    return float4(color.rgb, 1.0);
}
