#pragma pack_matrix(row_major)

cbuffer TireMarkView : register(b0)
{
    matrix ViewProjection;
    matrix InverseViewProjection;
    float4 InvViewSize;
};

struct TireMark
{
    float4 CenterAndDepth;
    float4 AlongAndLength;
    float4 AcrossAndWidth;
    float4 NormalAndOpacity;
};

Texture2D<float> SceneDepth : register(t0);
Texture2D TireTexture : register(t1);
StructuredBuffer<TireMark> Marks : register(t2);
SamplerState TireSampler : register(s0);

struct VertexOut
{
    float4 Position : SV_Position;
    nointerpolation uint MarkIndex : TEXCOORD0;
};

VertexOut mainVS(uint VertexID : SV_VertexID)
{
    const uint MarkIndex = VertexID / 6;
    const TireMark Mark = Marks[MarkIndex];
    const float3 Along = Mark.AlongAndLength.xyz * Mark.AlongAndLength.w;
    const float3 Across = Mark.AcrossAndWidth.xyz * Mark.AcrossAndWidth.w;
    const float3 Normal = Mark.NormalAndOpacity.xyz * Mark.CenterAndDepth.w;
    float2 Min = float2(1, 1);
    float2 Max = float2(-1, -1);
    bool CrossesNearPlane = false;
    [unroll]
    for (uint Corner = 0; Corner < 8; ++Corner)
    {
        const float3 Point = Mark.CenterAndDepth.xyz
            + ((Corner & 1) ? Along : -Along)
            + ((Corner & 2) ? Across : -Across)
            + ((Corner & 4) ? Normal : -Normal);
        const float4 ClipPosition = mul(float4(Point, 1), ViewProjection);
        CrossesNearPlane = CrossesNearPlane || ClipPosition.z <= 0.0;
        const float2 Ndc = clamp(ClipPosition.xy / max(ClipPosition.w, 0.0001), -1.0, 1.0);
        Min = min(Min, Ndc);
        Max = max(Max, Ndc);
    }
    if (CrossesNearPlane)
    {
        Min = float2(-1, -1);
        Max = float2(1, 1);
    }

    static const float2 Quad[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1)
    };
    VertexOut Output;
    Output.Position = float4(lerp(Min, Max, Quad[VertexID % 6]), 0, 1);
    Output.MarkIndex = MarkIndex;
    return Output;
}

float4 mainPS(VertexOut Input) : SV_Target
{
    const float Depth = SceneDepth.Load(int3(int2(Input.Position.xy), 0));
    const float2 Ndc = Input.Position.xy * InvViewSize.xy * float2(2, -2) + float2(-1, 1);
    const float4 WorldH = mul(float4(Ndc, Depth, 1), InverseViewProjection);
    const float3 World = WorldH.xyz / max(WorldH.w, 0.000001);
    const float3 Surface = cross(ddx(World), ddy(World));

    const TireMark Mark = Marks[Input.MarkIndex];
    const float3 Offset = World - Mark.CenterAndDepth.xyz;
    const float2 Local = float2(dot(Offset, Mark.AcrossAndWidth.xyz), dot(Offset, Mark.AlongAndLength.xyz))
        / float2(Mark.AcrossAndWidth.w, Mark.AlongAndLength.w);
    const float2 UV = Local * 0.5 + 0.5;
    const float2 UVDx = ddx(UV);
    const float2 UVDy = ddy(UV);
    const float Facing = dot(Surface, Mark.NormalAndOpacity.xyz);
    if (Depth >= 1.0 || any(abs(Local) > 1.0)
        || abs(dot(Offset, Mark.NormalAndOpacity.xyz)) > Mark.CenterAndDepth.w
        || Facing * Facing < 0.5 * dot(Surface, Surface))
        discard;

    float4 Tire = TireTexture.SampleGrad(TireSampler, UV, UVDx, UVDy);
    Tire.a *= Mark.NormalAndOpacity.w;
    return Tire;
}
