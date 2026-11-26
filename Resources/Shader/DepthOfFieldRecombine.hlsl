Texture2D<float4> LightingHDR : register(t0);
Texture2D<float> CoCTexture : register(t1);
Texture2D<float4> NearTexture : register(t2);
Texture2D<float4> FarTexture : register(t3);
SamplerState LinearClampSampler : register(s0);

float4 mainVS(uint VertexID : SV_VertexID) : SV_Position
{
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    return float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

float4 mainPS(float4 Position : SV_Position) : SV_Target0
{
    int2 Pixel = int2(Position.xy);
    float4 Original = LightingHDR.Load(int3(Pixel, 0));
    float CoC = CoCTexture.Load(int3(Pixel, 0));

    uint Width, Height;
    LightingHDR.GetDimensions(Width, Height);
    float2 UV = Position.xy / float2(Width, Height);
    float4 Far = FarTexture.SampleLevel(LinearClampSampler, UV, 0);
    float4 Near = NearTexture.SampleLevel(LinearClampSampler, UV, 0);

    float3 Color = Original.rgb;
    if (Far.a > 0.0001f)
    {
        float FarBlend = smoothstep(0.5f, 2.0f, CoC) * saturate(Far.a);
        Color = lerp(Color, Far.rgb / Far.a, FarBlend);
    }

    if (Near.a > 0.0001f)
    {
        float NearBlend = max(smoothstep(0.5f, 2.0f, -CoC), saturate(Near.a));
        Color = lerp(Color, Near.rgb / Near.a, NearBlend);
    }

    return float4(Color, Original.a);
}
