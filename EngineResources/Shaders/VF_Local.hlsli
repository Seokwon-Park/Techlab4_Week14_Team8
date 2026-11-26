// FVertex (스태틱 메시, CPU 스킨 결과). VertexFactory.hlsli에서만 include한다
struct FVertexFactoryInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Tangent  : TANGENT;
    float2 UV       : TEXCOORD;
    float4 Color    : COLOR;
};

FVFIntermediates VF_GetIntermediates(FVertexFactoryInput Input, float4x4 WorldMatrix, float4x4 NormalMatrixIn)
{
    return VF_LocalToWorld(Input.Position, Input.Normal, Input.Tangent, Input.UV, Input.Color, WorldMatrix, NormalMatrixIn);
}

// 웨이트 히트맵은 스킨 정점에만 있다
bool VF_GetDebugWeightColor(FVertexFactoryInput Input, uint Bone, out float4 Color)
{
    Color = 0.0f;
    return false;
}
