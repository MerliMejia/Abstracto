struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float2 uv;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
};

[[vk::binding(0, 0)]]
Sampler2D sourceColor;

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target {
    float3 source = sourceColor.Sample(input.uv).rgb;

    float luminance = dot(source, float3(0.299, 0.587, 0.114));
    float3 grayscale = luminance.xxx;

    float3 gradient = float3(input.uv.x, input.uv.y, 1.0 - input.uv.x);
    float3 finalColor = lerp(grayscale, gradient, 0.25);

    return float4(finalColor, 1.0);
}
