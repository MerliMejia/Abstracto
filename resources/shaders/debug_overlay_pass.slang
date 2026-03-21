struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 color;
    [[vk::location(2)]] float2 uv;
};

struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color;
};

struct DebugOverlayFrameData {
    float4x4 view;
    float4x4 proj;
};

struct DebugOverlayPushConstant {
    float4x4 model;
    float4 color;
};

[[vk::binding(0, 0)]]
ConstantBuffer<DebugOverlayFrameData> frameData;

[[vk::push_constant]]
ConstantBuffer<DebugOverlayPushConstant> markerPush;

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    float4 worldPosition = mul(markerPush.model, float4(input.position, 1.0));
    float4 viewPosition = mul(frameData.view, worldPosition);
    output.position = mul(frameData.proj, viewPosition);
    output.color = float4(input.color, 1.0) * markerPush.color;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
    return input.color;
}
