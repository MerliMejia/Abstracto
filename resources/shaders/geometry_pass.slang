struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 normal;
    [[vk::location(2)]] float2 uv;
    [[vk::location(3)]] float4 tangent;
    [[vk::location(4)]] uint4 jointIndices;
    [[vk::location(5)]] float4 jointWeights;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 normalWS;
    [[vk::location(1)]] float2 uv;
    [[vk::location(2)]] float selectedJointWeight;
    [[vk::location(3)]] float boneWeightDebugEnabled;
};

struct GeometryData {
    float4x4 model;
    float4x4 modelNormal;
    float4x4 view;
    float4x4 proj;
};

struct GeometryObjectData {
    float4x4 model;
    float4x4 modelNormal;
    int boneWeightJointIndex;
    int boneWeightDebugEnabled;
    int skinningEnabled;
    int debugPadding;
};

struct SkinPaletteData {
    float4x4 joints[128];
};

struct MaterialData {
    float4 baseColorFactor;
    float4 emissiveFactor;
    float4 surfaceParams;
};

struct FSOutput {
    [[vk::location(0)]] float4 gbuffer0;
    [[vk::location(1)]] float4 gbuffer1;
    [[vk::location(2)]] float4 gbuffer2;
    [[vk::location(3)]] float4 gbuffer3;
};

[[vk::binding(0, 0)]] ConstantBuffer<GeometryData> ubo;
[[vk::binding(1, 0)]] Sampler2D baseColorTexture;
[[vk::binding(2, 0)]] Sampler2D metallicRoughnessTexture;
[[vk::binding(3, 0)]] Sampler2D emissiveTexture;
[[vk::binding(4, 0)]] Sampler2D occlusionTexture;
[[vk::binding(5, 0)]] ConstantBuffer<MaterialData> materialParams;
[[vk::binding(0, 1)]] ConstantBuffer<SkinPaletteData> skinPalette;
[[vk::push_constant]]
ConstantBuffer<GeometryObjectData> objectData;

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput o;
    float4 meshPosition = float4(input.position, 1.0);
    float3 meshNormal = input.normal;
    if (objectData.skinningEnabled != 0) {
        meshPosition = 0.0;
        meshNormal = 0.0;

        meshPosition += mul(skinPalette.joints[input.jointIndices.x], float4(input.position, 1.0)) * input.jointWeights.x;
        meshPosition += mul(skinPalette.joints[input.jointIndices.y], float4(input.position, 1.0)) * input.jointWeights.y;
        meshPosition += mul(skinPalette.joints[input.jointIndices.z], float4(input.position, 1.0)) * input.jointWeights.z;
        meshPosition += mul(skinPalette.joints[input.jointIndices.w], float4(input.position, 1.0)) * input.jointWeights.w;

        meshNormal += mul((float3x3)skinPalette.joints[input.jointIndices.x], input.normal) * input.jointWeights.x;
        meshNormal += mul((float3x3)skinPalette.joints[input.jointIndices.y], input.normal) * input.jointWeights.y;
        meshNormal += mul((float3x3)skinPalette.joints[input.jointIndices.z], input.normal) * input.jointWeights.z;
        meshNormal += mul((float3x3)skinPalette.joints[input.jointIndices.w], input.normal) * input.jointWeights.w;
    }

    float4 worldPos = mul(objectData.model, meshPosition);
    float4 viewPos = mul(ubo.view, worldPos);
    o.position = mul(ubo.proj, viewPos);

    o.normalWS = normalize(mul((float3x3)objectData.modelNormal, meshNormal));
    o.uv = input.uv;
    o.selectedJointWeight = 0.0;
    o.boneWeightDebugEnabled = objectData.boneWeightDebugEnabled;
    if (objectData.boneWeightDebugEnabled != 0 &&
        objectData.boneWeightJointIndex >= 0) {
        uint selectedJointIndex = (uint)objectData.boneWeightJointIndex;
        if (input.jointIndices.x == selectedJointIndex) o.selectedJointWeight += input.jointWeights.x;
        if (input.jointIndices.y == selectedJointIndex) o.selectedJointWeight += input.jointWeights.y;
        if (input.jointIndices.z == selectedJointIndex) o.selectedJointWeight += input.jointWeights.z;
        if (input.jointIndices.w == selectedJointIndex) o.selectedJointWeight += input.jointWeights.w;
    }
    return o;
}

float3 BoneWeightHeatmap(float weight) {
    float t = saturate(weight);
    const float3 low = float3(0.03, 0.12, 0.55);
    const float3 mid = float3(0.10, 0.92, 0.82);
    const float3 high = float3(1.00, 0.22, 0.08);
    return t < 0.5
        ? lerp(low, mid, t * 2.0)
        : lerp(mid, high, (t - 0.5) * 2.0);
}

[shader("fragment")]
FSOutput fragMain(VSOutput input) {
    FSOutput o;

    if (input.boneWeightDebugEnabled > 0.5) {
        float3 heatmap = BoneWeightHeatmap(input.selectedJointWeight);
        float3 packedNormal = normalize(input.normalWS) * 0.5 + 0.5;
        o.gbuffer0 = float4(heatmap, 1.0);
        o.gbuffer1 = float4(packedNormal, 1.0);
        o.gbuffer2 = float4(0.0, 1.0, 1.0, 1.0);
        o.gbuffer3 = float4(heatmap, 1.0);
        return o;
    }

    float4 baseColorSample = baseColorTexture.Sample(input.uv);
    float4 metallicRoughnessSample = metallicRoughnessTexture.Sample(input.uv);
    float3 emissiveSample = emissiveTexture.Sample(input.uv).rgb;
    float occlusionSample = occlusionTexture.Sample(input.uv).r;

    float3 albedo = baseColorSample.rgb * materialParams.baseColorFactor.rgb;
    float metallic =
        saturate(metallicRoughnessSample.b * materialParams.surfaceParams.x);
    float roughness =
        saturate(metallicRoughnessSample.g * materialParams.surfaceParams.y);
    float occlusion = lerp(1.0, occlusionSample, materialParams.surfaceParams.w);
    float3 emissive = emissiveSample * materialParams.emissiveFactor.rgb;

    float3 packedNormal = normalize(input.normalWS) * 0.5 + 0.5;

    o.gbuffer0 = float4(albedo,
                        baseColorSample.a * materialParams.baseColorFactor.a);
    o.gbuffer1 = float4(packedNormal, 1.0);
    o.gbuffer2 = float4(metallic, roughness, occlusion, 1.0);
    o.gbuffer3 = float4(emissive, 1.0);
    return o;
}
