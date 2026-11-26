Texture2D<float4> LightingHDR : register(t0);
Texture2D<float> CoCTexture : register(t1);

static const float MaxCoCRadius = 16.0f;

float4 mainVS(uint VertexID : SV_VertexID) : SV_Position
{
    float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
    return float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

float4 mainPS(float4 Position : SV_Position) : SV_Target0
{
    int2 Pixel = int2(Position.xy);
    float3 HDR = max(LightingHDR.Load(int3(Pixel, 0)).rgb, 0.0f);
    float CoC = CoCTexture.Load(int3(Pixel, 0));

    // 배경 지오메트리가 식별되도록 ACES 근사 톤매핑 후 휘도(sRGB) 계산
    float3 Mapped = saturate((HDR * (2.51f * HDR + 0.03f)) / (HDR * (2.43f * HDR + 0.59f) + 0.14f));
    float3 DisplayScene = pow(Mapped, 1.0f / 2.2f);
    float Luma = dot(DisplayScene, float3(0.2126f, 0.7152f, 0.0722f));
    float3 BaseGray = Luma.xxx;

    float AbsCoC = abs(CoC);
    float LayerMask = smoothstep(0.5f, 2.0f, AbsCoC);
    float Normalized = saturate((AbsCoC - 0.5f) / (MaxCoCRadius - 0.5f));
    float Strength = LayerMask * lerp(0.35f, 1.0f, pow(Normalized, 0.6f));

    // Near(음수) = 초록, Far(양수) = 파랑, In-Focus(|CoC| <= 0.5) = 무채색
    float3 LayerTint = (CoC < 0.0f) ? float3(0.05f, 0.95f, 0.25f) : float3(0.10f, 0.45f, 1.00f);
    float3 ShadedLayer = LayerTint * (0.35f + 0.65f * Luma);
    float3 OutputColor = lerp(BaseGray, ShadedLayer, Strength);

    return float4(OutputColor, 1.0f);
}
