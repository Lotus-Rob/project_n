// The composite (design doc 0002.4.1 §3): the scene target drawn into the swapchain, encoded to sRGB.

struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
