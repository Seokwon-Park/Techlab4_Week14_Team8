#pragma pack_matrix(row_major)
#include "VertexFactory.hlsli"

cbuffer ViewConstants : register(b0)
{
    matrix VP;
};

cbuffer WorldConstants : register(b2)
{
    matrix World;
    matrix NormalMatrix;
    float4 ShadowFlags; // x: bReceiveShadow
};

cbuffer MaterialParams : register(b1)
{
    float4 BaseColor;
    float2 UVOffset;
    float2 TextureFlags; // x: 노멀맵 인코딩, y: 노멀맵 Y 반전
    float4 MaskParams;   // x: Masked 알파 기준값(0이면 끔), y: Unlit이면 1. FStaticMeshMaterialParams와 같은 순서
};

// 정점 입력은 VertexFactory.hlsli의 FVertexFactoryInput (정점 타입별 변형)

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float3 WorldNormal : NORMAL;
    float4 WorldTangent : TANGENT;
    float2 UV : TEXCOORD0;
    nointerpolation float ReceiveShadow : TEXCOORD1; // 오브젝트 상수(b2)는 VS에만 묶이므로 VS가 넘긴다
};

struct GBUFFER_OUTPUT
{
    float4 BaseColorMetallic : SV_Target0;
    float4 NormalRoughness : SV_Target1;
};

Texture2D ColorTexture : register(t0);
Texture2D NormalTexture : register(t1);
SamplerState ColorSampler : register(s0);

float3 ResolveWorldNormal(float3 WorldNormal, float4 WorldTangent, float3 MapNormal)
{
    float3 N = normalize(WorldNormal);
    float3 T = WorldTangent.xyz - N * dot(N, WorldTangent.xyz);
    T *= rsqrt(max(dot(T, T), 1.0e-8f));
    float3 B = cross(N, T) * WorldTangent.w;
    return normalize(T * MapNormal.x + B * MapNormal.y + N * MapNormal.z);
}

float3 DecodeMapNormal(float3 Sample, float Encoding)
{
    if (Encoding > 1.5f)
    {
        float2 XY = Encoding > 2.5f ? Sample.xy : Sample.xy * 2.0f - 1.0f;
        return float3(XY, sqrt(saturate(1.0f - dot(XY, XY))));
    }
    return Sample * 2.0f - 1.0f;
}

PS_INPUT mainVS(FVertexFactoryInput Input)
{
    PS_INPUT Output;
    const FVFIntermediates VF = VF_GetIntermediates(Input, World, NormalMatrix);

    Output.Position = mul(float4(VF.WorldPosition, 1.0f), VP);
    Output.WorldNormal = VF.WorldNormal;
    Output.WorldTangent = VF.WorldTangent;
    Output.UV = VF.UV;
    Output.ReceiveShadow = ShadowFlags.x;
    return Output;
}

GBUFFER_OUTPUT mainPS(PS_INPUT Input, bool bFrontFace : SV_IsFrontFace)
{
    GBUFFER_OUTPUT Output;
    // 뒷면 픽셀은 Two Sided 머티리얼에서만 들어온다
    if (!bFrontFace) Input.WorldNormal = -Input.WorldNormal;

    float4 TextureColor = ColorTexture.Sample(ColorSampler, Input.UV + UVOffset);
    // Masked 머티리얼: 알파가 기준값보다 작은 픽셀은 GBuffer에 쓰지 않는다(깊이도 남지 않는다)
    if (MaskParams.x > 0.0f)
        clip(TextureColor.a * BaseColor.a - MaskParams.x);
    // 색 텍스처는 UNORM으로 읽히므로 sRGB 값을 선형 조명 공간으로 변환한다.
    float3 LinearAlbedo = pow(max(TextureColor.rgb, 0.0f), 2.2f) * BaseColor.rgb;
    float3 Normal = normalize(Input.WorldNormal);
    if (TextureFlags.x > 0.5f)
    {
        float3 MapNormal = DecodeMapNormal(NormalTexture.Sample(ColorSampler, Input.UV + UVOffset).xyz, TextureFlags.x);
        if (TextureFlags.y > 0.5f) MapNormal.y = -MapNormal.y;
        Normal = ResolveWorldNormal(Input.WorldNormal, Input.WorldTangent, MapNormal);
    }

    // 현재 UMaterial에는 Metallic/Roughness 값이 없어 임시 상수를 쓴다.
    Output.BaseColorMetallic = float4(LinearAlbedo, 0.0f);
    // bReceiveShadow가 꺼진 물체는 w를 음수(-Roughness - 1)로 저장한다. Deferred 조명이 그림자 마스크를 무시한다.
    const float Roughness = 0.8f;
    // Unlit 머티리얼은 노멀을 0으로 남긴다. DeferredLightingShader가 이 픽셀은 조명 없이 알베도를 그대로 쓴다.
    if (MaskParams.y > 0.5f) Normal = float3(0.0f, 0.0f, 0.0f);
    Output.NormalRoughness = float4(Normal, Input.ReceiveShadow > 0.5f ? Roughness : -Roughness - 1.0f);
    return Output;
}
