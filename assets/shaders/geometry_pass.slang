struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 normal;
    [[vk::location(2)]] float2 uv;
    [[vk::location(3)]] float4 tangent;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 normalVS;
    [[vk::location(1)]] float4 tangentVS;
    [[vk::location(2)]] float2 uv;
};

struct GeometryData {
    float4x4 model;
    float4x4 modelNormal;
    float4x4 view;
    float4x4 proj;
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
[[vk::binding(3, 0)]] Sampler2D normalTexture;
[[vk::binding(4, 0)]] Sampler2D emissiveTexture;
[[vk::binding(5, 0)]] Sampler2D occlusionTexture;
[[vk::binding(6, 0)]] ConstantBuffer<MaterialData> materialParams;

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput o;
    float4 worldPos = mul(ubo.model, float4(input.position, 1.0));
    float4 viewPos = mul(ubo.view, worldPos);
    o.position = mul(ubo.proj, viewPos);

    float3 normalWS = normalize(mul((float3x3)ubo.modelNormal, input.normal));
    float3 tangentWS = normalize(mul((float3x3)ubo.model, input.tangent.xyz));
    o.normalVS = normalize(mul((float3x3)ubo.view, normalWS));
    o.tangentVS = float4(normalize(mul((float3x3)ubo.view, tangentWS)),
                         input.tangent.w);
    o.uv = input.uv;
    return o;
}

[shader("fragment")]
FSOutput fragMain(VSOutput input) {
    FSOutput o;

    float4 baseColorSample = baseColorTexture.Sample(input.uv);
    float4 metallicRoughnessSample = metallicRoughnessTexture.Sample(input.uv);
    float3 normalSample = normalTexture.Sample(input.uv).xyz * 2.0 - 1.0;
    float3 emissiveSample = emissiveTexture.Sample(input.uv).rgb;
    float occlusionSample = occlusionTexture.Sample(input.uv).r;

    float3 albedo = baseColorSample.rgb * materialParams.baseColorFactor.rgb;
    float metallic =
        saturate(metallicRoughnessSample.b * materialParams.surfaceParams.x);
    float roughness =
        saturate(metallicRoughnessSample.g * materialParams.surfaceParams.y);
    float occlusion = lerp(1.0, occlusionSample, materialParams.surfaceParams.w);
    float3 emissive = emissiveSample * materialParams.emissiveFactor.rgb;

    float3 N = normalize(input.normalVS);
    float3 normalVS = N;
    if (materialParams.surfaceParams.z > 0.0) {
        float3 T = normalize(input.tangentVS.xyz - N * dot(N, input.tangentVS.xyz));
        float3 B = normalize(cross(N, T)) * input.tangentVS.w;
        float3x3 tbn = float3x3(T, B, N);
        normalSample.xy *= materialParams.surfaceParams.z;
        normalVS = normalize(mul(tbn, normalSample));
    }
    float3 packedNormal = normalVS * 0.5 + 0.5;

    o.gbuffer0 = float4(albedo,
                        baseColorSample.a * materialParams.baseColorFactor.a);
    o.gbuffer1 = float4(packedNormal, 1.0);
    o.gbuffer2 = float4(metallic, roughness, occlusion, 1.0);
    o.gbuffer3 = float4(emissive, 1.0);
    return o;
}
