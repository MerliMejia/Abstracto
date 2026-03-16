struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float2 uv;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
};

struct DebugPushConstant {
    uint selectedOutput;
    float nearPlane;
    float farPlane;
    float padding;
};

float LinearizeDepth(float depth, float nearPlane, float farPlane) {
    float z = depth * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) /
           (farPlane + nearPlane - z * (farPlane - nearPlane));
}

[[vk::binding(0, 0)]] Sampler2D gbufferAlbedo;
[[vk::binding(1, 0)]] Sampler2D gbufferNormal;
[[vk::binding(2, 0)]] Sampler2D gbufferMaterial;
[[vk::binding(3, 0)]] Sampler2D gbufferEmissive;
[[vk::binding(4, 0)]] Sampler2D gbufferDepth;
[[vk::binding(5, 0)]] Sampler2D lightPassColor;
[[vk::binding(6, 0)]] Sampler2D tonemapPassColor;
[[vk::binding(7, 0)]] Sampler2D directionalShadowMap;
[[vk::binding(8, 0)]] Sampler2D spotShadowMap0;
[[vk::binding(9, 0)]] Sampler2D spotShadowMap1;
[[vk::binding(10, 0)]] Sampler2D spotShadowMap2;

[[vk::push_constant]]
ConstantBuffer<DebugPushConstant> debugPush;

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput o;
    o.position = float4(input.position, 1.0);
    o.uv = input.uv;
    return o;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
    if (debugPush.selectedOutput == 0) {
        return gbufferAlbedo.Sample(input.uv);
    }

    if (debugPush.selectedOutput == 1) {
        return gbufferNormal.Sample(input.uv);
    }

    if (debugPush.selectedOutput == 2) {
        return gbufferMaterial.Sample(input.uv);
    }

    if (debugPush.selectedOutput == 3) {
        return gbufferEmissive.Sample(input.uv);
    }

    if (debugPush.selectedOutput == 4) {
        float depth = gbufferDepth.Sample(input.uv).r;
        float linearDepth = LinearizeDepth(depth, debugPush.nearPlane,
                                           debugPush.farPlane);
        float normalized = saturate(linearDepth / debugPush.farPlane);
        return float4(normalized.xxx, 1.0);
    }

    if (debugPush.selectedOutput == 5) {
        float2 tiledUv = frac(input.uv * 2.0);

        if (input.uv.x < 0.5 && input.uv.y < 0.5) {
            return gbufferAlbedo.Sample(tiledUv);
        }
        if (input.uv.x >= 0.5 && input.uv.y < 0.5) {
            return float4(gbufferNormal.Sample(tiledUv).rgb, 1.0);
        }
        if (input.uv.x < 0.5 && input.uv.y >= 0.5) {
            return gbufferMaterial.Sample(tiledUv);
        }
        return gbufferEmissive.Sample(tiledUv);
    }

    if (debugPush.selectedOutput == 6) {
        float3 hdr = lightPassColor.Sample(input.uv).rgb;
        return float4(hdr / (1.0 + hdr), 1.0);
    }

    if (debugPush.selectedOutput == 7) {
        return tonemapPassColor.Sample(input.uv);
    }

    if (debugPush.selectedOutput == 8) {
        float depth = directionalShadowMap.Sample(input.uv).r;
        return float4((1.0 - depth).xxx, 1.0);
    }

    if (debugPush.selectedOutput == 9) {
        float depth = spotShadowMap0.Sample(input.uv).r;
        return float4((1.0 - depth).xxx, 1.0);
    }

    if (debugPush.selectedOutput == 10) {
        float depth = spotShadowMap1.Sample(input.uv).r;
        return float4((1.0 - depth).xxx, 1.0);
    }

    if (debugPush.selectedOutput == 11) {
        float depth = spotShadowMap2.Sample(input.uv).r;
        return float4((1.0 - depth).xxx, 1.0);
    }

    return float4(1.0, 0.0, 1.0, 1.0);
}
