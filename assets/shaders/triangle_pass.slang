struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 color;
    [[vk::location(2)]] float2 uv;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color;
};

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target {
    return float4(input.color, 1.0);
}
