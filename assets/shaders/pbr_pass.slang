static const float PI = 3.14159265359;
static const uint PBR_MAX_LIGHTS = 8;
static const uint PBR_LIGHT_TYPE_DIRECTIONAL = 0;
static const uint PBR_LIGHT_TYPE_POINT = 1;
static const uint PBR_LIGHT_TYPE_SPOT = 2;

struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float2 uv;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
};

struct PbrLightData {
    float4 positionAndType;
    float4 directionAndRange;
    float4 colorAndRadiance;
    float4 spotAngles;
    float4x4 shadowMatrix;
    float4 shadowParams;
    uint4 shadowInfo;
};

struct PbrFrameData {
    float4 projParams;
    float4 cameraWorldAndBackground;
    float4 viewRightAndDiffuse;
    float4 viewUpAndSpecular;
    float4 viewForwardAndUnused;
    float4 environmentParams;
    float4 specularTuning;
    uint4 settings;
    PbrLightData lights[PBR_MAX_LIGHTS];
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
[[vk::binding(9, 0)]] Sampler2D shadowMap0;
[[vk::binding(10, 0)]] Sampler2D shadowMap1;
[[vk::binding(11, 0)]] Sampler2D shadowMap2;
[[vk::binding(12, 0)]] Sampler2D shadowMap3;

[[vk::binding(0, 1)]]
ConstantBuffer<PbrFrameData> pbrFrame;

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
    float viewZ = pbrFrame.projParams.w / (-depth - pbrFrame.projParams.z);
    float viewX = ndcXY.x * (-viewZ) / pbrFrame.projParams.x;
    float viewY = ndcXY.y * (-viewZ) / pbrFrame.projParams.y;
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
    float3 right = pbrFrame.viewRightAndDiffuse.xyz;
    float3 up = pbrFrame.viewUpAndSpecular.xyz;
    float3 forward = pbrFrame.viewForwardAndUnused.xyz;
    return right * viewDirection.x + up * viewDirection.y + forward * (-viewDirection.z);
}

float3 ReconstructWorldPosition(float2 uv, float depth) {
    float3 viewPos = ReconstructViewPosition(uv, depth);
    return pbrFrame.cameraWorldAndBackground.xyz + ViewToWorld(viewPos);
}

float3 RotateEnvironment(float3 worldDirection) {
    float angle = pbrFrame.environmentParams.x;
    float s = sin(angle);
    float c = cos(angle);
    return normalize(float3(
        c * worldDirection.x + s * worldDirection.z,
        worldDirection.y,
        -s * worldDirection.x + c * worldDirection.z));
}

float3 SampleEnvironment(float3 worldDirection) {
    return environmentMap.Sample(RotateEnvironment(worldDirection)).rgb;
}

float DistanceAttenuation(float distanceToLight, float range) {
    float safeRange = max(range, 1e-4);
    float distanceRatio = saturate(distanceToLight / safeRange);
    float smoothRange = 1.0 - distanceRatio * distanceRatio;
    smoothRange *= smoothRange;
    return smoothRange / max(distanceToLight * distanceToLight, 1e-3);
}

float PointLightAttenuation(float distanceToLight, float radius, float range) {
    float effectiveDistance = max(distanceToLight, max(radius, 1e-3));
    return DistanceAttenuation(effectiveDistance, range);
}

float SpotAttenuation(float3 lightToSurfaceDirection, float3 lightDirection,
                      float2 spotAngles) {
    float cosInner = spotAngles.x;
    float cosOuter = spotAngles.y;
    float coneAmount = dot(normalize(lightDirection), lightToSurfaceDirection);
    return saturate((coneAmount - cosOuter) / max(cosInner - cosOuter, 1e-4));
}

float SampleShadowDepth(uint shadowMapIndex, float2 uv) {
    if (shadowMapIndex == 0) {
        return shadowMap0.Sample(uv).r;
    }
    if (shadowMapIndex == 1) {
        return shadowMap1.Sample(uv).r;
    }
    if (shadowMapIndex == 2) {
        return shadowMap2.Sample(uv).r;
    }
    return shadowMap3.Sample(uv).r;
}

float ComputeShadowVisibility(PbrLightData light, float3 worldPos, float3 worldNormal) {
    if (light.shadowInfo.x == 0) {
        return 1.0;
    }

    float3 biasedWorldPos = worldPos + worldNormal * light.shadowParams.y;
    float4 shadowClip = mul(light.shadowMatrix, float4(biasedWorldPos, 1.0));
    if (abs(shadowClip.w) <= 1e-5) {
        return 1.0;
    }

    float3 shadowNdc = shadowClip.xyz / shadowClip.w;
    float2 shadowUv = shadowNdc.xy * 0.5 + 0.5;
    if (shadowUv.x <= 0.0 || shadowUv.x >= 1.0 ||
        shadowUv.y <= 0.0 || shadowUv.y >= 1.0 ||
        shadowNdc.z <= 0.0 || shadowNdc.z >= 1.0) {
        return 1.0;
    }

    float currentDepth = shadowNdc.z - light.shadowParams.x;
    float texelSize = max(light.shadowParams.z, 1e-5);
    float visibility = 0.0;

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            float2 offset = float2((float)x, (float)y) * texelSize;
            float sampledDepth =
                SampleShadowDepth(light.shadowInfo.y, shadowUv + offset);
            visibility += currentDepth <= sampledDepth ? 1.0 : 0.0;
        }
    }

    return visibility / 9.0;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
    float depth = gbufferDepth.Sample(input.uv).r;
    float3 backgroundColor = 0.0.xxx;
    bool showBackground = (pbrFrame.settings.x & PBR_FLAG_SHOW_BACKGROUND) != 0;
    bool enableIbl = (pbrFrame.settings.x & PBR_FLAG_ENABLE_IBL) != 0;

    float3 fallbackView = normalize(float3(
        (input.uv.x * 2.0 - 1.0) / pbrFrame.projParams.x,
        (input.uv.y * 2.0 - 1.0) / pbrFrame.projParams.y,
        -1.0));
    float3 fallbackWorld = normalize(ViewToWorld(fallbackView));
    backgroundColor =
        SampleEnvironment(fallbackWorld) * pbrFrame.cameraWorldAndBackground.w;

    if (depth >= 0.9999) {
        if (pbrFrame.settings.y == 6) {
            return float4(backgroundColor, 1.0);
        }
        return showBackground ? float4(backgroundColor, 1.0)
                              : float4(0.0, 0.0, 0.0, 1.0);
    }

    float4 albedoSample = gbufferAlbedo.Sample(input.uv);
    float3 albedo = albedoSample.rgb;
    float3 worldNormal = DecodeNormal(gbufferNormal.Sample(input.uv).rgb);
    float3 material = gbufferMaterial.Sample(input.uv).rgb;
    float3 emissive = gbufferEmissive.Sample(input.uv).rgb;

    float metallic = saturate(material.r);
    float roughness = clamp(material.g, 0.045, 1.0);
    float ao = saturate(material.b);

    float3 worldPos = ReconstructWorldPosition(input.uv, depth);
    float3 V = normalize(pbrFrame.cameraWorldAndBackground.xyz - worldPos);

    float dielectricF0 = 0.04 * pbrFrame.specularTuning.x;
    float3 F0 = lerp(float3(dielectricF0, dielectricF0, dielectricF0), albedo, metallic);
    float NdotV = max(dot(worldNormal, V), 0.0);
    float3 directLighting = 0.0.xxx;
    float3 ambientDiffuse = 0.0.xxx;
    float3 ambientSpecular = 0.0.xxx;
    float3 reflections = 0.0.xxx;

    uint lightCount = min(pbrFrame.settings.z, PBR_MAX_LIGHTS);
    for (uint lightIndex = 0; lightIndex < lightCount; ++lightIndex) {
        PbrLightData light = pbrFrame.lights[lightIndex];
        uint lightType = (uint)(light.positionAndType.w + 0.5);
        float3 radiance = light.colorAndRadiance.rgb * light.colorAndRadiance.w;
        float3 L = 0.0.xxx;
        float attenuation = 1.0;

        if (lightType == PBR_LIGHT_TYPE_DIRECTIONAL) {
            L = normalize(-light.directionAndRange.xyz);
        } else {
            float3 toLight = light.positionAndType.xyz - worldPos;
            float distanceToLight = length(toLight);
            L = toLight / max(distanceToLight, 1e-4);
            if (lightType == PBR_LIGHT_TYPE_POINT) {
                attenuation = PointLightAttenuation(distanceToLight,
                                                    light.spotAngles.z,
                                                    light.directionAndRange.w);
            } else {
                if (distanceToLight >= light.directionAndRange.w) {
                    continue;
                }

                attenuation = DistanceAttenuation(distanceToLight,
                                                  light.directionAndRange.w);
                float3 lightToSurfaceDirection = -L;
                attenuation *= SpotAttenuation(lightToSurfaceDirection,
                                               light.directionAndRange.xyz,
                                               light.spotAngles.xy);
                if (attenuation <= 1e-4) {
                    continue;
                }
            }
        }

        float3 H = normalize(V + L);
        float3 directF = FresnelSchlick(max(dot(H, V), 0.0), F0);
        float NDF = DistributionGGX(worldNormal, H, roughness);
        float G = GeometrySmith(worldNormal, V, L, roughness);
        float NdotL = max(dot(worldNormal, L), 0.0);
        if (NdotL <= 1e-4) {
            continue;
        }

        float3 numerator = NDF * G * directF;
        float denominator = max(4.0 * NdotV * NdotL, 1e-4);
        float3 specular = numerator / denominator;
        float3 directKs = directF;
        float3 directKd = (1.0 - directKs) * (1.0 - metallic);
        float3 diffuse = directKd * albedo / PI;
        float shadowVisibility = 1.0;
        if (lightType == PBR_LIGHT_TYPE_DIRECTIONAL ||
            lightType == PBR_LIGHT_TYPE_SPOT) {
            shadowVisibility =
                ComputeShadowVisibility(light, worldPos, worldNormal);
        }
        directLighting +=
            (diffuse + specular) * radiance * attenuation * NdotL * shadowVisibility;
    }

    if (enableIbl) {
        float3 worldView = V;
        float3 worldReflection = normalize(reflect(-worldView, worldNormal));
        float3 rotatedNormal = RotateEnvironment(worldNormal);
        float3 rotatedReflection = RotateEnvironment(worldReflection);

        float3 irradiance = irradianceMap.Sample(rotatedNormal).rgb;
        float3 diffuseIbl = irradiance * albedo;

        float maxMip = max(pbrFrame.environmentParams.y, 0.0);
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
            iblKd * diffuseIbl * ao * pbrFrame.viewRightAndDiffuse.w;
        ambientSpecular =
            specularIbl * ao * pbrFrame.viewUpAndSpecular.w;
        reflections = prefilteredColor;
    }

    float3 ambient = ambientDiffuse + ambientSpecular;
    float3 color = ambient + directLighting + emissive;

    if (pbrFrame.settings.y == 1) {
        color = directLighting;
    } else if (pbrFrame.settings.y == 2) {
        color = ambientDiffuse;
    } else if (pbrFrame.settings.y == 3) {
        color = ambientSpecular;
    } else if (pbrFrame.settings.y == 4) {
        color = ambient;
    } else if (pbrFrame.settings.y == 5) {
        color = reflections;
    } else if (pbrFrame.settings.y == 6) {
        color = backgroundColor;
    }

    return float4(color, albedoSample.a);
}
