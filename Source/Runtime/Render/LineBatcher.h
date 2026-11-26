#pragma once

#include "Buffer.h"
#include "StaticMeshData.h"
#include "PipelineState.h"

class FRenderer;
class UWorld;

class FLineBatcher
{
public:
	FLineBatcher() = default;
	~FLineBatcher();
	bool Init(FRenderer* InRenderer, UWorld* InWorld);
	void BuildVertexBuffer();

	void BeginFrame();


	void AddLine(const FVector& A, const FVector& B, const FVector4& Color);
	// 반투명 면: 양면, 깊이 읽기 전용, 선보다 먼저 그린다
	void AddTriangle(const FVector& A, const FVector& B, const FVector& C, const FVector4& Color);
	void AddBox(FBox BoundingBox, FVector4 Color);

	// 로컬 AABB의 모서리를 각각 월드로 옮겨 그린다. 회전이 살아있는 OBB가 된다.
	void AddOrientedBox(const FBox& LocalBox, const FMatrix& WorldMatrix, const FVector4& Color);
	void AddPath(const TArray<FVector>& Points, const FVector4& Color);

	// 밑면 중심에서 꼭짓점으로 향하는 사각뿔의 모서리를 그립니다.
	void AddPyramid(const FVector& BaseCenter, const FVector& Tip, float HalfWidth, const FVector4& Color);
	// 서로 직교하는 세 원으로 와이어 구를 그립니다.
	void AddSphere(const FVector& Center, float Radius, const FVector4& Color, int32 Segments = 16);
	// FillAlpha > 0이면 면도 채운다 (RGB는 Color)
	void AddSwingCone(const FVector& Apex, const FVector& Axis, const FVector& Right,
		float Swing1Radian, float Swing2Radian, float Length, const FVector4& Color, int32 Segments = 24, float FillAlpha = 0.0f);
	void AddConvex(const TArray<FVector>& LocalVertices, const TArray<uint32>& Indices,
		const FMatrix& WorldMatrix, const FVector4& Color);
	void AddCapsule(const FVector& Center, const FVector& Up, const FVector& Right,
		float Radius, float HalfHeight, const FVector4& Color, int32 Segments = 16);
	void AddWireArc(const FVector& Center, const FVector& AxisA, const FVector& AxisB,
		float Radius, float StartRadian, float EndRadian, const FVector4& Color, int32 Segments = 24);
	void AddTwistArc(const FVector& Center, const FVector& Axis, const FVector& Ref,
		float LimitRadian, float Radius, const FVector4& Color, int32 Segments = 24, float FillAlpha = 0.0f);
	void AddCylinder(const FVector& Start, const FVector& End, float Radius, const FVector4& Color, int32 Segments = 8);
	// 닫힌 도형의 반투명 면: 뒷면은 그리지 않는다. HalfHeight == Radius면 구가 된다
	void AddSolidCapsule(const FVector& Center, const FVector& Up, const FVector& Right,
		float Radius, float HalfHeight, const FVector4& Color, int32 Segments = 16, int32 Rings = 6);
	void AddSolidOrientedBox(const FBox& LocalBox, const FMatrix& WorldMatrix, const FVector4& Color);

	

	void Flush();
	void OnRender(const FMatrix& ViewProjection);
	// 꺼 두면 다른 물체에 가려지지 않고 항상 위에 그린다 (디버그 표시용)
	void SetDepthTest(bool bEnable);

private:
	FRenderer* Renderer;

	UWorld* World;

	FShaderProgram* LineShader;
	FPipelineState PipelineState;
	FLineBatchVertex* VertexBufferBase;  
	FLineBatchVertex* VertexBufferPtr;  
	TUniquePtr<FConstantBuffer> CB;
	uint32 VertexCount;

	uint32 MaxVertices = 0;
	TUniquePtr<FVertexBuffer> VertexBuffer;

	FPipelineState TrianglePipelineState;
	FLineBatchVertex* TriangleVertexBase = nullptr;
	uint32 TriangleVertexCount = 0;
	uint32 MaxTriangleVertices = 0;
	TUniquePtr<FVertexBuffer> TriangleVertexBuffer;

	void AddSolidTriangle(const FVector& A, const FVector& B, const FVector& C, const FVector4& Color, const FVector& Outward);
	FPipelineState SolidPipelineState;
	FLineBatchVertex* SolidVertexBase = nullptr;
	uint32 SolidVertexCount = 0;
	TUniquePtr<FVertexBuffer> SolidVertexBuffer;
};
