struct Constants_Data {
    float4x4 transform; // pixels to clip space
};

[[vk::push_constant]] Constants_Data gConstants;
[[vk::binding(0, 0)]] Texture2D gTexture;
[[vk::binding(1, 0)]] SamplerState gSampler;

struct Vertex_Input {
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
};

struct Pixel_Input {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR0;
};

Pixel_Input Vertex_Main(Vertex_Input input) {
    Pixel_Input output;

    output.position = mul(gConstants.transform, float4(input.position, 0.0, 1.0));
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 Pixel_Main(Pixel_Input input) : SV_Target {
    return input.color * gTexture.Sample(gSampler, input.uv);
}
