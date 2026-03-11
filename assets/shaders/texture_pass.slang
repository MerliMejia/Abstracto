struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float2 uv;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
};

[[vk::binding(0, 0)]]
Sampler2D uvMaskTexture;

[[vk::binding(1, 0)]]
Sampler2D albedoTexture;

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
    float4 uvData = uvMaskTexture.Sample(input.uv);

    if (uvData.a < 0.5) {
        discard;
    }

    float2 uv = uvData.rg;
    return albedoTexture.Sample(uv);
}
