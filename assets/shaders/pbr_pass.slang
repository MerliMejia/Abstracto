static const float PI = 3.14159265359;

struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float2 uv;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
};

struct PbrPushConstant {
    float4 projParams;
    float4 viewRightAndBackground;
    float4 viewUpAndDiffuse;
    float4 viewForwardAndSpecular;
    float4 lightDirectionAndRotation;
    float4 lightColorAndPrefilterMip;
    float4 specularTuning;
    uint4 settings;
};

[[vk::binding(0, 0)]] Sampler2D gbufferAlbedo;
[[vk::binding(1, 0)]] Sampler2D gbufferNormal;
[[vk::binding(2, 0)]] Sampler2D gbufferMaterial;
[[vk::binding(3, 0)]] Sampler2D gbufferEmissive;
[[vk::binding(4, 0)]] Sampler2D gbufferDepth;
[[vk::binding(5, 0)]] SamplerCube environmentMap;
[[vk::binding(6, 0)]] SamplerCube irradianceMap;
[[vk::binding(7, 0)]] SamplerCube prefilteredEnvironmentMap;
[[vk::binding(8, 0)]] Sampler2D brdfLut;

[[vk::push_constant]]
ConstantBuffer<PbrPushConstant> pbrPush;

static const uint PBR_FLAG_ENABLE_IBL = 1;
static const uint PBR_FLAG_SHOW_BACKGROUND = 2;

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
    float viewZ = pbrPush.projParams.w / (-depth - pbrPush.projParams.z);
    float viewX = ndcXY.x * (-viewZ) / pbrPush.projParams.x;
    float viewY = ndcXY.y * (-viewZ) / pbrPush.projParams.y;
    return float3(viewX, viewY, viewZ);
}

float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-5);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / lerp(NdotV, 1.0, k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0 - roughness), F0) - F0) *
                    pow(1.0 - cosTheta, 5.0);
}

float3 ViewToWorld(float3 viewDirection) {
    float3 right = pbrPush.viewRightAndBackground.xyz;
    float3 up = pbrPush.viewUpAndDiffuse.xyz;
    float3 forward = pbrPush.viewForwardAndSpecular.xyz;
    return right * viewDirection.x + up * viewDirection.y + forward * (-viewDirection.z);
}

float3 RotateEnvironment(float3 worldDirection) {
    float angle = pbrPush.lightDirectionAndRotation.w;
    float s = sin(angle);
    float c = cos(angle);
    return normalize(float3(
        c * worldDirection.x - s * worldDirection.y,
        s * worldDirection.x + c * worldDirection.y,
        worldDirection.z));
}

float3 SampleEnvironment(float3 worldDirection) {
    return environmentMap.Sample(RotateEnvironment(worldDirection)).rgb;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
    float depth = gbufferDepth.Sample(input.uv).r;
    float3 backgroundColor = 0.0.xxx;
    bool showBackground = (pbrPush.settings.x & PBR_FLAG_SHOW_BACKGROUND) != 0;
    bool enableIbl = (pbrPush.settings.x & PBR_FLAG_ENABLE_IBL) != 0;

    float3 fallbackView = normalize(float3(
        (input.uv.x * 2.0 - 1.0) / pbrPush.projParams.x,
        (input.uv.y * 2.0 - 1.0) / pbrPush.projParams.y,
        -1.0));
    float3 fallbackWorld = normalize(ViewToWorld(fallbackView));
    backgroundColor =
        SampleEnvironment(fallbackWorld) * pbrPush.viewRightAndBackground.w;

    if (depth >= 0.9999) {
        if (pbrPush.settings.y == 6) {
            return float4(backgroundColor, 1.0);
        }
        return showBackground ? float4(backgroundColor, 1.0)
                              : float4(0.0, 0.0, 0.0, 1.0);
    }

    float4 albedoSample = gbufferAlbedo.Sample(input.uv);
    float3 albedo = albedoSample.rgb;
    float3 normal = DecodeNormal(gbufferNormal.Sample(input.uv).rgb);
    float3 material = gbufferMaterial.Sample(input.uv).rgb;
    float3 emissive = gbufferEmissive.Sample(input.uv).rgb;

    float metallic = saturate(material.r);
    float roughness = clamp(material.g, 0.045, 1.0);
    float ao = saturate(material.b);

    float3 viewPos = ReconstructViewPosition(input.uv, depth);
    float3 V = normalize(-viewPos);
    float3 L = normalize(-pbrPush.lightDirectionAndRotation.xyz);
    float3 H = normalize(V + L);
    float3 radiance = pbrPush.lightColorAndPrefilterMip.rgb;

    float dielectricF0 = 0.04 * pbrPush.specularTuning.x;
    float3 F0 = lerp(float3(dielectricF0, dielectricF0, dielectricF0), albedo, metallic);
    float3 directF = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float NDF = DistributionGGX(normal, H, roughness);
    float G = GeometrySmith(normal, V, L, roughness);

    float NdotV = max(dot(normal, V), 0.0);
    float NdotL = max(dot(normal, L), 0.0);
    float3 numerator = NDF * G * directF;
    float denominator = max(4.0 * NdotV * NdotL, 1e-4);
    float3 specular = numerator / denominator;

    float3 directKs = directF;
    float3 directKd = (1.0 - directKs) * (1.0 - metallic);
    float3 diffuse = directKd * albedo / PI;

    float3 directLighting = (diffuse + specular) * radiance * NdotL;
    float3 ambientDiffuse = 0.0.xxx;
    float3 ambientSpecular = 0.0.xxx;
    float3 reflections = 0.0.xxx;

    if (enableIbl) {
        float3 worldNormal = normalize(ViewToWorld(normal));
        float3 worldView = normalize(ViewToWorld(V));
        float3 worldReflection = normalize(reflect(-worldView, worldNormal));
        float3 rotatedNormal = RotateEnvironment(worldNormal);
        float3 rotatedReflection = RotateEnvironment(worldReflection);

        float3 irradiance = irradianceMap.Sample(rotatedNormal).rgb;
        float3 diffuseIbl = irradiance * albedo;

        float maxMip = max(pbrPush.lightColorAndPrefilterMip.w, 0.0);
        float3 prefilteredColor =
            prefilteredEnvironmentMap.SampleLevel(rotatedReflection,
                                                  roughness * maxMip)
                .rgb;
        float2 envBrdf = brdfLut.Sample(float2(max(NdotV, 0.0), roughness)).rg;
        float3 iblF = FresnelSchlickRoughness(NdotV, F0, roughness);
        float3 iblKd = (1.0 - iblF) * (1.0 - metallic);
        float3 specularIbl =
            prefilteredColor * (iblF * envBrdf.x + envBrdf.y);

        ambientDiffuse =
            iblKd * diffuseIbl * ao * pbrPush.viewUpAndDiffuse.w;
        ambientSpecular =
            specularIbl * ao * pbrPush.viewForwardAndSpecular.w;
        reflections = prefilteredColor;
    }

    float3 ambient = ambientDiffuse + ambientSpecular;
    float3 color = ambient + directLighting + emissive;

    if (pbrPush.settings.y == 1) {
        color = directLighting;
    } else if (pbrPush.settings.y == 2) {
        color = ambientDiffuse;
    } else if (pbrPush.settings.y == 3) {
        color = ambientSpecular;
    } else if (pbrPush.settings.y == 4) {
        color = ambient;
    } else if (pbrPush.settings.y == 5) {
        color = reflections;
    } else if (pbrPush.settings.y == 6) {
        color = backgroundColor;
    }

    return float4(color, albedoSample.a);
}
