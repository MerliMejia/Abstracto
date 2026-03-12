struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 normal;
    [[vk::location(2)]] float2 uv;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 normalVS;
    [[vk::location(1)]] float2 uv;
};

struct GeometryData {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};

struct FSOutput {
    [[vk::location(0)]] float4 gbuffer0;
    [[vk::location(1)]] float4 gbuffer1;
    [[vk::location(2)]] float4 gbuffer2;
};

[[vk::binding(0, 0)]] ConstantBuffer<GeometryData> ubo;
[[vk::binding(1, 0)]] Sampler2D albedoTexture;

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput o;
    float4 worldPos = mul(ubo.model, float4(input.position, 1.0));
    float4 viewPos = mul(ubo.view, worldPos);
    o.position = mul(ubo.proj, viewPos);
    float3 normalWS = normalize(mul((float3x3)ubo.model, input.normal));
    o.normalVS = normalize(mul((float3x3)ubo.view, normalWS));
    o.uv = input.uv;
    return o;
}

[shader("fragment")]
FSOutput fragMain(VSOutput input) {
    FSOutput o;
    float3 albedo = albedoTexture.Sample(input.uv).rgb;
    float3 packedNormal = normalize(input.normalVS) * 0.5 + 0.5;

    o.gbuffer0 = float4(albedo, 1.0);
    o.gbuffer1 = float4(packedNormal, 1.0);
    o.gbuffer2 = float4(0.0, 0.5, 0.0, 1.0); // R=metallic, G=specular
    return o;
}
