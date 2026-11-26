Texture2D<float> SceneDepthTexture : register(t0);

cbuffer DepthOfFieldSetupConstants : register(b0)
{
    float ProjectionA;
    float ProjectionB;
    float FocusDistance;
    float InfinityCoCRadius;
};

float4 mainVS(uint VertexID : SV_VertexID) : SV_Position
{
    float2 uv = float2((VertexID << 1) & 2, VertexID & 2);
    return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

float mainPS(float4 Position : SV_Position) : SV_Target0
{
    int2 Pixel = int2(Position.xy);
    float DeviceZ = SceneDepthTexture.Load(int3(Pixel, 0));
    
    float CoC = InfinityCoCRadius;
    if (DeviceZ < 1.0f)
    {
        float ViewDepth = ProjectionB / (DeviceZ - ProjectionA);
        CoC *= 1.0f - FocusDistance / ViewDepth;
    }
    
    const float MaxCoCRadius = 16.0f;
    return clamp(CoC, -MaxCoCRadius, MaxCoCRadius);
}