struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float2 uv;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
};

struct LightPushConstant {
    float4x4 invProj;
    float4 lightDirection;
    float4 lightColor;
};

[[vk::binding(0, 0)]] Sampler2D gbufferAlbedo;
[[vk::binding(1, 0)]] Sampler2D gbufferNormal;
[[vk::binding(2, 0)]] Sampler2D gbufferMaterial;
[[vk::binding(3, 0)]] Sampler2D gbufferDepth;

[[vk::push_constant]]
ConstantBuffer<LightPushConstant> lightPush;

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput o;
    o.position = float4(input.position, 1.0);
    o.uv = input.uv;
    return o;
}

float3 DecodeNormal(float3 encodedNormal) {
    return normalize(encodedNormal * 2.0 - 1.0);
}

float3 ReconstructViewPosition(float2 uv, float depth) {
    float2 ndcXY = uv * 2.0 - 1.0;
    float ndcZ = depth * 2.0 - 1.0;

    float4 clipPos = float4(ndcXY, ndcZ, 1.0);
    float4 viewPos = mul(lightPush.invProj, clipPos);
    return viewPos.xyz / viewPos.w;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
    float depth = gbufferDepth.Sample(input.uv).r;
    if (depth >= 0.9999) {
        return float4(0.02, 0.02, 0.02, 1.0);
    }

    float3 albedo = gbufferAlbedo.Sample(input.uv).rgb;
    float3 normal = DecodeNormal(gbufferNormal.Sample(input.uv).rgb);
    float2 material = gbufferMaterial.Sample(input.uv).rg;
    float metallic = material.r;
    float specularStrength = material.g;

    float3 viewPos = ReconstructViewPosition(input.uv, depth);
    float3 L = normalize(-lightPush.lightDirection.xyz);
    float3 V = normalize(-viewPos);
    float3 H = normalize(L + V);

    float NdotL = max(dot(normal, L), 0.0);
    float NdotH = max(dot(normal, H), 0.0);

    float shininess = lerp(16.0, 64.0, metallic);
    float3 ambient = albedo * 0.05;
    float3 diffuse = albedo * NdotL;
    float3 specular = lightPush.lightColor.rgb *
                      (pow(NdotH, shininess) * specularStrength);

    float3 color = ambient + (diffuse + specular) * lightPush.lightColor.rgb;
    return float4(color, 1.0);
}
