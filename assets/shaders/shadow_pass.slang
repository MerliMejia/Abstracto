struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 normal;
    [[vk::location(2)]] float2 uv;
};

struct ShadowPassData {
    float4x4 model;
    float4x4 lightViewProj;
};

[[vk::binding(0, 0)]] ConstantBuffer<ShadowPassData> shadowPass;

[shader("vertex")]
float4 vertMain(VSInput input) : SV_Position {
    float4 worldPos = mul(shadowPass.model, float4(input.position, 1.0));
    return mul(shadowPass.lightViewProj, worldPos);
}

[shader("fragment")]
void fragMain() {}
