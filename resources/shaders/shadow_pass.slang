struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(5)]] uint4 jointIndices;
    [[vk::location(6)]] float4 jointWeights;
    [[vk::location(7)]] float4 instanceModel0;
    [[vk::location(8)]] float4 instanceModel1;
    [[vk::location(9)]] float4 instanceModel2;
    [[vk::location(10)]] float4 instanceModel3;
};

struct ShadowPassData {
    float4x4 model;
    float4x4 lightViewProj;
};

struct ShadowObjectData {
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
    float4x4 model = transpose(float4x4(
        input.instanceModel0,
        input.instanceModel1,
        input.instanceModel2,
        input.instanceModel3));
    float4 meshPosition = float4(input.position, 1.0);
    if (shadowObject.skinningData.x != 0) {
        meshPosition = 0.0;
        meshPosition += mul(skinPalette.joints[input.jointIndices.x], float4(input.position, 1.0)) * input.jointWeights.x;
        meshPosition += mul(skinPalette.joints[input.jointIndices.y], float4(input.position, 1.0)) * input.jointWeights.y;
        meshPosition += mul(skinPalette.joints[input.jointIndices.z], float4(input.position, 1.0)) * input.jointWeights.z;
        meshPosition += mul(skinPalette.joints[input.jointIndices.w], float4(input.position, 1.0)) * input.jointWeights.w;
    }
    float4 worldPos = mul(model, meshPosition);
    return mul(shadowPass.lightViewProj, worldPos);
}

[shader("fragment")]
void fragMain() {}
