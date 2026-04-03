struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(5)]] uint4 jointIndices;
    [[vk::location(6)]] float4 jointWeights;
};

struct ShadowPassData {
    float4x4 model;
    float4x4 lightViewProj;
};

struct ShadowObjectData {
    float4x4 model;
    int4 skinningData;
};

struct SkinPaletteData {
    float4x4 joints[128];
};

[[vk::binding(0, 0)]] ConstantBuffer<ShadowPassData> shadowPass;
[[vk::binding(0, 1)]] ConstantBuffer<SkinPaletteData> skinPalette;
[[vk::push_constant]]
ConstantBuffer<ShadowObjectData> shadowObject;

[shader("vertex")]
float4 vertMain(VSInput input) : SV_Position {
    float4 meshPosition = float4(input.position, 1.0);
    if (shadowObject.skinningData.x != 0) {
        meshPosition = 0.0;
        meshPosition += mul(skinPalette.joints[input.jointIndices.x], float4(input.position, 1.0)) * input.jointWeights.x;
        meshPosition += mul(skinPalette.joints[input.jointIndices.y], float4(input.position, 1.0)) * input.jointWeights.y;
        meshPosition += mul(skinPalette.joints[input.jointIndices.z], float4(input.position, 1.0)) * input.jointWeights.z;
        meshPosition += mul(skinPalette.joints[input.jointIndices.w], float4(input.position, 1.0)) * input.jointWeights.w;
    }
    float4 worldPos = mul(shadowObject.model, meshPosition);
    return mul(shadowPass.lightViewProj, worldPos);
}

[shader("fragment")]
void fragMain() {}
