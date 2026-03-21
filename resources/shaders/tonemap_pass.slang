struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float2 uv;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
};

struct TonemapPushConstant {
    float4 params0;
};

[[vk::binding(0, 0)]] Sampler2D hdrColor;

[[vk::push_constant]]
ConstantBuffer<TonemapPushConstant> tonemapPush;

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput o;
    o.position = float4(input.position, 1.0);
    o.uv = input.uv;
    return o;
}

float3 ACESFilm(float3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 ReinhardWhitePoint(float3 x, float whitePoint) {
    float white2 = max(whitePoint * whitePoint, 1e-4);
    return (x * (1.0 + x / white2)) / (1.0 + x);
}

float3 FilmicCurve(float3 x) {
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((x * (A * x + C * B) + D * E) /
            (x * (A * x + B) + D * F)) -
           E / F;
}

float3 Uncharted2Filmic(float3 x, float whitePoint) {
    float3 curr = FilmicCurve(max(x, 0.0.xxx));
    float3 whiteScale = 1.0.xxx / max(FilmicCurve(whitePoint.xxx), 1e-4.xxx);
    return saturate(curr * whiteScale);
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
    float exposure = tonemapPush.params0.x;
    float whitePoint = tonemapPush.params0.y;
    float gamma = tonemapPush.params0.z;
    float operatorIndex = tonemapPush.params0.w;
    float4 hdrSample = hdrColor.Sample(input.uv);
    float3 hdr = max(hdrSample.rgb, 0.0.xxx) * exposure;

    float3 mapped = hdr;
    if (operatorIndex < 0.5) {
        mapped = hdr / max(whitePoint, 1e-4);
    } else if (operatorIndex < 1.5) {
        mapped = ReinhardWhitePoint(hdr, whitePoint);
    } else if (operatorIndex < 2.5) {
        mapped = ACESFilm(hdr / max(whitePoint, 1e-4));
    } else {
        mapped = Uncharted2Filmic(hdr, whitePoint);
    }

    float invGamma = 1.0 / max(gamma, 1e-4);
    float3 ldr = pow(saturate(mapped), invGamma.xxx);
    return float4(ldr, hdrSample.a);
}
