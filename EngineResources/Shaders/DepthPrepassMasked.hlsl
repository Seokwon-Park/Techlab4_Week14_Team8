#pragma pack_matrix(row_major)
// Forward 깊이 프리패스에서 Masked 머티리얼에 쓰는 PS.
// VS는 메시 패스의 UberLit VS를 그대로 쓰므로(SceneRenderer::RenderPrePass) 입력은 UberLit VS_OUT의 앞부분과 같아야 한다.
// 알파 판정은 UberLit.hlsl Uber_PS의 Masked clip과 같다.

cbuffer MaterialParams : register(b2)
{
    float4 BaseColor;
    float4 Emissive;
    float4 SurfaceParams; // x: 노멀맵 Y 반전, y: Masked 알파 기준값(0이면 끔), z: 컬러 텍스처 사용, w: 노멀맵 인코딩
    float4 UVTransform;
    float4 SpecularColorShininess;
};

Texture2D ColorTexture : register(t0);
SamplerState ColorSampler : register(s0);

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR;
    float2 UV : TEXCOORD0;
};

// 스캐너가 mainVS/mainPS가 둘 다 있는 파일만 등록하므로 VS를 둔다. 실제로는 메시 패스 VS를 쓴다
float4 mainVS(float3 Position : POSITION) : SV_POSITION
{
    return float4(Position, 1.0f);
}

void mainPS(PS_INPUT Input)
{
    if (SurfaceParams.y <= 0.0f)
        return;
    const float Alpha = (SurfaceParams.z > 0.5f ? ColorTexture.Sample(ColorSampler, Input.UV).a : Input.Color.a) * BaseColor.a;
    clip(Alpha - SurfaceParams.y);
}
