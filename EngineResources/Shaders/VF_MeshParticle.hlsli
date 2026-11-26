// FVertex + FMeshParticleInstanceVertex. 인스턴스 행렬이 정점을 월드까지 옮기므로 World/NormalMatrix는 쓰지 않는다.
// VertexFactory.hlsli에서만 include한다
#include "MeshParticleInstancing.hlsli"

struct FVertexFactoryInput
{
    float3 Position  : POSITION;
    float3 Normal    : NORMAL;
    float4 Tangent   : TANGENT;
    float2 UV        : TEXCOORD;
    float4 Color     : COLOR;
    float4 InstanceX : INSTANCE_TRANSFORM0;
    float4 InstanceY : INSTANCE_TRANSFORM1;
    float4 InstanceZ : INSTANCE_TRANSFORM2;
};

FVFIntermediates VF_GetIntermediates(FVertexFactoryInput Input, float4x4 WorldMatrix, float4x4 NormalMatrixIn)
{
    const float4x4 InstanceWorld = GetMeshParticleWorld(Input.InstanceX, Input.InstanceY, Input.InstanceZ);

    FVFIntermediates Result;
    Result.WorldPosition = mul(float4(Input.Position, 1.0f), InstanceWorld).xyz;
    Result.WorldNormal = mul(Input.Normal, GetMeshParticleNormalMatrix(InstanceWorld));
    const float Handedness = Input.Tangent.w * (determinant((float3x3)InstanceWorld) < 0.0f ? -1.0f : 1.0f);
    Result.WorldTangent = float4(mul(float4(Input.Tangent.xyz, 0.0f), InstanceWorld).xyz, Handedness);
    Result.UV = Input.UV;
    Result.Color = Input.Color;
    return Result;
}

bool VF_GetDebugWeightColor(FVertexFactoryInput Input, uint Bone, out float4 Color)
{
    Color = 0.0f;
    return false;
}
