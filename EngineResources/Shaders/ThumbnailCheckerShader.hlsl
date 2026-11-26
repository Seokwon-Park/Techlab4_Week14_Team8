// 메시 썸네일 배경 체커. 깊이 Z=1.0(배경 픽셀)에만 그려져 3D 메시 뒤로 깔린다.
cbuffer ThumbnailBackgroundConstants : register(b0)
{
	float4 CheckerColorA;
	float4 CheckerColorB;
	float  CellSize;      // 렌더 타깃 픽셀 단위 한 칸 크기
	float3 Padding;
}

struct VSOutput
{
	float4 Position : SV_POSITION;
};

VSOutput mainVS(uint VertexID : SV_VertexID)
{
	VSOutput Output;
	float2 UV = float2((VertexID << 1) & 2, VertexID & 2);
	Output.Position = float4(UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 1.0f, 1.0f);
	return Output;
}

float4 mainPS(VSOutput Input) : SV_Target
{
	int2 Cell = int2(floor(Input.Position.xy / max(CellSize, 1.0f)));
	bool bEven = ((Cell.x + Cell.y) & 1) == 0;
	return float4(bEven ? CheckerColorA.rgb : CheckerColorB.rgb, 1.0f);
}
