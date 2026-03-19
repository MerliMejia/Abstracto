struct VSOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color;
};

struct TriangleData {
    float4 positions[3];
    float4 colors[3];
    float4 translation;
    float4 rotation;
    float4 scale;
};

[[vk::binding(0, 0)]]
ConstantBuffer<TriangleData> triangleData;

float3 rotateByQuat(float3 v, float4 q) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

[shader("vertex")]
VSOutput vertMain(uint vertexId : SV_VertexID) {
    VSOutput output;

    float3 localPosition = triangleData.positions[vertexId].xyz;
    localPosition *= triangleData.scale.xyz;
    localPosition = rotateByQuat(localPosition, triangleData.rotation);
    float3 finalPosition = localPosition + triangleData.translation.xyz;

    output.position = float4(finalPosition, 1.0);
    output.color = triangleData.colors[vertexId];
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
    return input.color;
}
