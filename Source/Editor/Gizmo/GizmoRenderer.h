#pragma once

#include "Render/Renderer.h"
#include "Render/PipelineState.h"
#include "Editor/Gizmo/Gizmo.h"

class UCameraComponent;

struct FGizmoData
{
	// Gizmo 한 Draw에 필요한 상수. GizmoShader.hlsl의 cbuffer와 순서가 같아야 한다.
	FMatrix World;
	FMatrix ViewProj;
	FVector4 Color;
	FVector4 ViewDirShade;   // xyz: 카메라 -> 기즈모 방향, w: 음영 세기
	FVector4 OriginClip;     // xyz: 기즈모 중심, w: 1이면 뒤쪽 절반을 자른다
	FVector4 Params;         // x: 평면 핸들 테두리 두께 (UV)
};

class FGizmoRenderer
{
public:
	FGizmoRenderer();
	~FGizmoRenderer() = default;

	bool Init(FRenderer* InRenderer);
	// View별 카메라 조건으로 계산한 위치에 기즈모 핸들을 그린다. ViewForward는 직교 뷰의 시선 방향이다.
	void OnRender(const FGizmo& Gizmo, const FMatrix& ViewProj, const FVector& CameraLocation, bool bCameraOrthographic, const FVector& ViewForward);

private:
	void DrawMesh(UStaticMesh* Mesh, const FGizmoData& Data);
	void BindState(EBlendState Blend, ERasterizerState Raster, EDepthStencilState Depth);

	void RenderLinear(const FGizmo& Gizmo, const FVector& Origin, const FVector& ViewDir, const FMatrix& ViewProj, bool bScale);
	void RenderRotate(const FGizmo& Gizmo, const FVector& Origin, const FVector& ViewDir, const FMatrix& ViewProj);

	FRenderer* Renderer = nullptr;

	FShaderProgram* Shader = nullptr;
	FPipelineState PipelineState;

	UStaticMesh* ArrowMesh = nullptr;
	UStaticMesh* ScaleHandleMesh = nullptr;
	UStaticMesh* RingMesh = nullptr;
	UStaticMesh* ScreenRingMesh = nullptr;
	UStaticMesh* CenterSphereMesh = nullptr;
	UStaticMesh* CenterCubeMesh = nullptr;
	UStaticMesh* DiscMesh = nullptr;
	UStaticMesh* QuadMesh = nullptr;
	TUniquePtr<FConstantBuffer> CB;
};
