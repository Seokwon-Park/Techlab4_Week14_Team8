Texture2D<float4> LightingHDR : register(t0);
Texture2D<float> CoCTexture : register(t1);

static const float MaxCoCRadius = 16.0f;
static const int TapCount = 37;
static const float2 DiskTaps[TapCount] =
{
    // Center (1)
    float2(0.0f, 0.0f),
    // Ring 1 (6, r = 1/3)
    float2( 0.333333f,  0.000000f), float2( 0.166667f,  0.288675f), float2(-0.166667f,  0.288675f),
    float2(-0.333333f,  0.000000f), float2(-0.166667f, -0.288675f), float2( 0.166667f, -0.288675f),
    // Ring 2 (12, r = 2/3)
    float2( 0.666667f,  0.000000f), float2( 0.577350f,  0.333333f), float2( 0.333333f,  0.577350f),
    float2( 0.000000f,  0.666667f), float2(-0.333333f,  0.577350f), float2(-0.577350f,  0.333333f),
    float2(-0.666667f,  0.000000f), float2(-0.577350f, -0.333333f), float2(-0.333333f, -0.577350f),
    float2( 0.000000f, -0.666667f), float2( 0.333333f, -0.577350f), float2( 0.577350f, -0.333333f),
    // Ring 3 (18, r = 1)
    float2( 1.000000f,  0.000000f), float2( 0.939693f,  0.342020f), float2( 0.766044f,  0.642788f),
    float2( 0.500000f,  0.866025f), float2( 0.173648f,  0.984808f), float2(-0.173648f,  0.984808f),
    float2(-0.500000f,  0.866025f), float2(-0.766044f,  0.642788f), float2(-0.939693f,  0.342020f),
    float2(-1.000000f,  0.000000f), float2(-0.939693f, -0.342020f), float2(-0.766044f, -0.642788f),
    float2(-0.500000f, -0.866025f), float2(-0.173648f, -0.984808f), float2( 0.173648f, -0.984808f),
    float2( 0.500000f, -0.866025f), float2( 0.766044f, -0.642788f), float2( 0.939693f, -0.342020f)
};

struct FGatherOutput
{
    float4 NearLayer : SV_Target0;
    float4 FarLayer : SV_Target1;
};

float4 mainVS(uint VertexID : SV_VertexID) : SV_Position
{
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    return float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

int2 GetSamplePixel(int2 Center, float2 Offset, int2 MaxPixel)
{
    return clamp(Center + int2(round(Offset)), int2(0, 0), MaxPixel);
}

FGatherOutput mainPS(float4 Position : SV_Position)
{
    uint Width, Height;
    CoCTexture.GetDimensions(Width, Height);
    int2 MaxPixel = int2(Width, Height) - 1;
    int2 Center = min(int2(Position.xy) * 2 + 1, MaxPixel);
    float FarRadius = max(CoCTexture.Load(int3(Center, 0)), 0.0f);

    float4 NearSum = 0.0f;
    float4 FarSum = 0.0f;
    [loop]
    for (int I = 0; I < TapCount; ++I)
    {
        float2 Tap = DiskTaps[I];

        int2 NearPixel = GetSamplePixel(Center, Tap * MaxCoCRadius, MaxPixel);
        float NearRadius = -CoCTexture.Load(int3(NearPixel, 0));
        float NearDistance = length(float2(NearPixel - Center));
        float NearWeight = saturate(NearRadius - NearDistance + 0.5f);
        if (NearRadius > 0.5f && NearWeight > 0.0f)
        {
            NearSum += float4(LightingHDR.Load(int3(NearPixel, 0)).rgb, 1.0f) * NearWeight;
        }

        if (FarRadius > 0.5f)
        {
            int2 FarPixel = GetSamplePixel(Center, Tap * FarRadius, MaxPixel);
            if (CoCTexture.Load(int3(FarPixel, 0)) > 0.5f)
            {
                FarSum += float4(LightingHDR.Load(int3(FarPixel, 0)).rgb, 1.0f);
            }
        }
    }

    FGatherOutput Output;
    Output.NearLayer = NearSum / TapCount;
    Output.FarLayer = FarSum / TapCount;
    return Output;
}