Texture2D<float4> NearTexture : register(t0);
Texture2D<float4> FarTexture : register(t1);
SamplerState LinearClampSampler : register(s0);

struct FPostfilterOutput
{
    float4 NearLayer : SV_Target0;
    float4 FarLayer : SV_Target1;
};

float4 mainVS(uint VertexID : SV_VertexID) : SV_Position
{
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    return float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

float4 SampleTent3x3(Texture2D<float4> Tex, float2 UV, float2 HalfTexel)
{
    return 0.25f * (
        Tex.SampleLevel(LinearClampSampler, UV + float2(-HalfTexel.x, -HalfTexel.y), 0) +
        Tex.SampleLevel(LinearClampSampler, UV + float2( HalfTexel.x, -HalfTexel.y), 0) +
        Tex.SampleLevel(LinearClampSampler, UV + float2(-HalfTexel.x,  HalfTexel.y), 0) +
        Tex.SampleLevel(LinearClampSampler, UV + float2( HalfTexel.x,  HalfTexel.y), 0)
    );
}

FPostfilterOutput mainPS(float4 Position : SV_Position)
{
    uint Width, Height;
    NearTexture.GetDimensions(Width, Height);
    float2 InvSize = 1.0f / float2(Width, Height);
    float2 UV = Position.xy * InvSize;
    float2 HalfTexel = 0.5f * InvSize;

    FPostfilterOutput Output;
    Output.NearLayer = SampleTent3x3(NearTexture, UV, HalfTexel);
    Output.FarLayer = SampleTent3x3(FarTexture, UV, HalfTexel);
    return Output;
}
