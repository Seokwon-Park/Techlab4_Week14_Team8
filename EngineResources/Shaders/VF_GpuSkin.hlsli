// FGpuSkinVertex. 본 행렬로 스키닝한 결과를 로컬 정점처럼 월드로 옮긴다. VertexFactory.hlsli에서만 include한다
// Cloth는 CPU가 정점 버퍼에 직접 써 넣으므로 여기서는 본 스키닝만 한다.
#include "GpuSkinCommon.hlsli"

struct FVertexFactoryInput
{
    float3 Position     : POSITION;
    float3 Normal       : NORMAL;
    float4 Tangent      : TANGENT;
    float2 UV           : TEXCOORD;
    float4 Color        : COLOR;
    uint4  BlendIndices : BLENDINDICES;
    float4 BlendWeights : BLENDWEIGHT;
};

FVFIntermediates VF_GetIntermediates(FVertexFactoryInput Input, float4x4 WorldMatrix, float4x4 NormalMatrixIn)
{
    float3 Position = Input.Position;
    float3 Normal = Input.Normal;
    float4 Tangent = Input.Tangent;
    GpuSkinVertex(Input.BlendIndices, Input.BlendWeights, Position, Normal, Tangent);
    return VF_LocalToWorld(Position, Normal, Tangent, Input.UV, Input.Color, WorldMatrix, NormalMatrixIn);
}

// 스켈레탈 메시 에디터의 웨이트 히트맵. Bone 번호의 가중치를 색으로 돌려준다
bool VF_GetDebugWeightColor(FVertexFactoryInput Input, uint Bone, out float4 Color)
{
    Color = float4(GpuSkinWeightColor(GpuSkinWeightForBone(Input.BlendIndices, Input.BlendWeights, Bone)), 1.0f);
    return true;
}
