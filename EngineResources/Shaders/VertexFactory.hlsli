// EngineResources/Shaders/VertexFactory.hlsli
// 메시를 그리는 패스 셰이더가 정점을 받는 입구. UE의 VertexFactory.ush와 같은 자리다.
// 정점 타입마다 FVertexFactoryInput과 VF_GetIntermediates가 다르고, 패스는 월드 공간 결과만 쓴다.
//
// 변형 매크로는 C++ EVertexFactoryType의 VariantMacro와 같다(VertexFactory.cpp).
//   (없음)                     -> VF_Local.hlsli        FVertex
//   GPU_SKIN=1                 -> VF_GpuSkin.hlsli      FGpuSkinVertex + 본 행렬(t14)
//   MESH_PARTICLE_INSTANCING=1 -> VF_MeshParticle.hlsli FVertex + 인스턴스 스트림(slot 1)
// 이 파일을 include한 셰이더는 FRenderResourceManager::ScanShaders가 정점 타입별 변형을 함께 컴파일한다.
// 입력 레이아웃은 VS 입력 리플렉션으로 만들므로 FVertexFactoryInput의 시맨틱과 순서는 C++ 정점 구조체와 같아야 한다.
#ifndef VERTEX_FACTORY_HLSLI
#define VERTEX_FACTORY_HLSLI

#ifndef GPU_SKIN
#define GPU_SKIN 0
#endif
#ifndef MESH_PARTICLE_INSTANCING
#define MESH_PARTICLE_INSTANCING 0
#endif

#if GPU_SKIN && MESH_PARTICLE_INSTANCING
#error GPU_SKIN and MESH_PARTICLE_INSTANCING cannot be combined.
#endif

struct FVFIntermediates
{
    float3 WorldPosition;
    float3 WorldNormal;   // 정규화하지 않은 값
    float4 WorldTangent;  // xyz 정규화하지 않은 값, w = 종법선 부호
    float2 UV;
    float4 Color;
};

// 로컬 정점을 World/NormalMatrix로 옮긴다. 스킨 결과도 이 함수로 넘긴다
FVFIntermediates VF_LocalToWorld(float3 Position, float3 Normal, float4 Tangent, float2 UV, float4 Color,
    float4x4 WorldMatrix, float4x4 NormalMatrixIn)
{
    FVFIntermediates Result;
    Result.WorldPosition = mul(float4(Position, 1.0f), WorldMatrix).xyz;
    Result.WorldNormal = mul(float4(Normal, 0.0f), NormalMatrixIn).xyz;
    // CPU가 구한 로컬 공간 부호에 월드 변환의 거울 반전만 반영한다
    const float Handedness = Tangent.w * (determinant((float3x3)WorldMatrix) < 0.0f ? -1.0f : 1.0f);
    Result.WorldTangent = float4(mul(float4(Tangent.xyz, 0.0f), WorldMatrix).xyz, Handedness);
    Result.UV = UV;
    Result.Color = Color;
    return Result;
}

#if GPU_SKIN
    #include "VF_GpuSkin.hlsli"
#elif MESH_PARTICLE_INSTANCING
    #include "VF_MeshParticle.hlsli"
#else
    #include "VF_Local.hlsli"
#endif

#endif
