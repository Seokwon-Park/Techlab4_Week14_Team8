#pragma once

#include "Render/Vertex.h"
#include "StaticMeshData.h"

class FGeometryGenerator
{
public:
	static TUniquePtr<FStaticMeshData> CreateTextQuad(const FVector& Start, const FVector& End, const FVector4& Color);
	static TUniquePtr<FStaticMeshData> CreateLine(const FVector& Start, const FVector& End, const FVector4& Color);
	static TUniquePtr<FStaticMeshData> CreatePlane(float Size, const FVector4& Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f));
	static TUniquePtr<FStaticMeshData> CreateAxis();

	static TUniquePtr<FStaticMeshData> CreateCone(float Radius, float Height, int Segments, const FVector4& Color);
	static TUniquePtr<FStaticMeshData> CreateCylinder(float Radius, float Height, int Segments, const FVector4& Color);
	static TUniquePtr<FStaticMeshData> CreateArrow(float BodyRadius, float BodyHeight, float HeadRadius, float HeadHeight, int Segments, const FVector4& Color); // Cylinder + Cone 합성
	static TUniquePtr<FStaticMeshData> CreateScaleBar(float BodyRadius, float BodyLength, float HeadSize, float Segments, const FVector4& Color); // Cylinder + Cone 합성
	static TUniquePtr<FStaticMeshData> CreateRing(float Radius, float TubeRadius, int Segments, int TubeSegments, const FVector4& Color);
	static TUniquePtr<FStaticMeshData> CreateCube(float Size, const FVector4& Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f));
	// XY 평면 위 원점 중심 원판. 기즈모 트랙볼 영역 표시에 쓴다.
	static TUniquePtr<FStaticMeshData> CreateDisc(float Radius, int Segments, const FVector4& Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f));
	// XY 평면의 [0,1]x[0,1] 사각형. UV가 위치와 같아 셰이더에서 테두리를 그릴 수 있다.
	static TUniquePtr<FStaticMeshData> CreateUnitQuad(const FVector4& Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f));

	static TUniquePtr<FStaticMeshData> CreateSphere(float _radius, uint32 _numSlices, uint32 _numStacks, const FVector4& Color);

	// 캡슐 조각. 반구는 +Z쪽 반쪽(밑면 없음), 원기둥은 Z축 높이 1에 뚜껑이 없다.
	// 둘레 분할 수를 같게 만들어야 이어 붙인 자리가 맞는다.
	static TUniquePtr<FStaticMeshData> CreateHemisphere(float Radius, uint32 NumSlices, uint32 NumStacks);
	static TUniquePtr<FStaticMeshData> CreateOpenCylinder(float Radius, uint32 NumSlices);
	// 꼭짓점 + 테두리 부채 모양의 매개변수 메시. 위치는 셰이더가 UV로 만든다 (UV.x = 둘레 비율, UV.y = 0 꼭짓점 / 1 테두리)
	static TUniquePtr<FStaticMeshData> CreateFanParameterMesh(uint32 Segments);

	static FStaticMeshData* GetMeshData(const FString& InName);

	static void CreateDefaultMeshDatas();

private:
	inline static TMap<FString, TUniquePtr<FStaticMeshData>> MeshDataMap;
};