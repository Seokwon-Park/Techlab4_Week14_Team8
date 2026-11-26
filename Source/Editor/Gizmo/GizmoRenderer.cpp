#include "EnginePCH.h"
#include "Editor/Gizmo/GizmoRenderer.h"

#include "Render/GeometryGenerator.h"
#include "Camera/CameraComponent.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Asset/AssetManager.h"

using namespace GizmoHandle;

namespace
{
	// 채도를 살짝 낮춘 축 색. 순색보다 덜 튀고 서로 구분은 잘 된다.
	const FVector4 AxisColors[3] = {
		FVector4(0.93f, 0.27f, 0.25f, 1.0f),   // X
		FVector4(0.49f, 0.80f, 0.20f, 1.0f),   // Y
		FVector4(0.24f, 0.52f, 0.97f, 1.0f),   // Z
	};
	const FVector4 ScreenColor(0.90f, 0.90f, 0.92f, 1.0f);
	const FVector4 HighlightColor(1.0f, 0.83f, 0.16f, 1.0f);

	constexpr float SolidShade = 0.45f;
	constexpr float RingShade = 0.25f;
	constexpr float PlaneAlpha = 0.22f;
	constexpr float PlaneAlphaHover = 0.45f;
	constexpr float PlaneBorder = 0.10f;
	constexpr float TrackballAlphaHover = 0.08f;
	constexpr float TrackballAlphaDrag = 0.13f;

	// 행 벡터 규약(mul(pos, World))으로 로컬 X, Y, Z 축과 원점을 그대로 행에 넣는다.
	FMatrix MakeBasisMatrix(const FVector& AxisX, const FVector& AxisY, const FVector& AxisZ, const FVector& Origin)
	{
		FMatrix M = FMatrix::Identity;
		M.M[0][0] = AxisX.X;  M.M[0][1] = AxisX.Y;  M.M[0][2] = AxisX.Z;  M.M[0][3] = 0.0f;
		M.M[1][0] = AxisY.X;  M.M[1][1] = AxisY.Y;  M.M[1][2] = AxisY.Z;  M.M[1][3] = 0.0f;
		M.M[2][0] = AxisZ.X;  M.M[2][1] = AxisZ.Y;  M.M[2][2] = AxisZ.Z;  M.M[2][3] = 0.0f;
		M.M[3][0] = Origin.X; M.M[3][1] = Origin.Y; M.M[3][2] = Origin.Z; M.M[3][3] = 1.0f;
		return M;
	}

	// 메시의 +Z를 Direction으로 돌리는 행렬 (화살표, 링 공용).
	FMatrix MakeAlongMatrix(const FVector& Direction, const FVector& Origin)
	{
		FVector U, V;
		FGizmo::MakeBasis(Direction, U, V);
		return MakeBasisMatrix(U, V, Direction, Origin);
	}

	FVector4 WithAlpha(const FVector4& C, float A) { return FVector4(C.X, C.Y, C.Z, A); }
}

FGizmoRenderer::FGizmoRenderer()
{
}

// 기즈모 전용 메시, 셰이더, 상수 버퍼를 준비한다.
bool FGizmoRenderer::Init(FRenderer* InRenderer)
{
	Renderer = InRenderer;

	ArrowMesh = UAssetManager::GetAssetByPath<UStaticMesh>("GizmoArrow");
	ScaleHandleMesh = UAssetManager::GetAssetByPath<UStaticMesh>("GizmoScaleHandle");
	RingMesh = UAssetManager::GetAssetByPath<UStaticMesh>("GizmoRing");
	ScreenRingMesh = UAssetManager::GetAssetByPath<UStaticMesh>("GizmoScreenRing");
	CenterSphereMesh = UAssetManager::GetAssetByPath<UStaticMesh>("GizmoCenterSphere");
	CenterCubeMesh = UAssetManager::GetAssetByPath<UStaticMesh>("GizmoCenterCube");
	DiscMesh = UAssetManager::GetAssetByPath<UStaticMesh>("GizmoDisc");
	QuadMesh = UAssetManager::GetAssetByPath<UStaticMesh>("GizmoQuad");

	CB = RenderCommand::CreateConstantBuffer(sizeof(FGizmoData));

	Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/GizmoShader.hlsl");
	PipelineState.Shader = Shader;
	PipelineState.Topology = EPrimitiveTopology::TriangleList;

	return true;
}

void FGizmoRenderer::BindState(EBlendState Blend, ERasterizerState Raster, EDepthStencilState Depth)
{
	PipelineState.BlendState = Blend;
	PipelineState.RasterizerState = Raster;
	PipelineState.DepthStencilState = Depth;
	RenderCommand::BindPipelineState(PipelineState);
}

// 렌더 View의 카메라로 표시 위치를 구해 모드별 핸들을 그린다.
void FGizmoRenderer::OnRender(
	const FGizmo& Gizmo,
	const FMatrix& ViewProj,
	const FVector& CameraLocation,
	const bool bCameraOrthographic,
	const FVector& ViewForward)
{
	if (!Gizmo.GetTarget())
		return;

	const FVector Origin = Gizmo.GetRenderLocationForView(CameraLocation, bCameraOrthographic);
	FVector ViewDir = bCameraOrthographic ? ViewForward : (Origin - CameraLocation);
	if (ViewDir.Length() < 1e-6f)
		ViewDir = FVector(1, 0, 0);
	ViewDir = ViewDir.Normalized();

	switch (Gizmo.GetWidgetMode())
	{
	case UE::Widget::WM_Translate: RenderLinear(Gizmo, Origin, ViewDir, ViewProj, false); break;
	case UE::Widget::WM_Scale:     RenderLinear(Gizmo, Origin, ViewDir, ViewProj, true);  break;
	case UE::Widget::WM_Rotate:    RenderRotate(Gizmo, Origin, ViewDir, ViewProj);        break;
	default: break;
	}
}

// 이동/크기: 축 3개 + 중앙 핸들 (불투명), 평면 핸들 3개 (반투명).
void FGizmoRenderer::RenderLinear(const FGizmo& Gizmo, const FVector& Origin, const FVector& ViewDir, const FMatrix& ViewProj, bool bScale)
{
	const int Active = Gizmo.GetHoveredAxis();   // 드래그 중에도 잡은 핸들을 유지한다
	const bool bDragging = Gizmo.IsUsing();

	FGizmoData Data{};
	Data.ViewProj = ViewProj;
	Data.ViewDirShade = FVector4(ViewDir.X, ViewDir.Y, ViewDir.Z, SolidShade);
	Data.OriginClip = FVector4(Origin.X, Origin.Y, Origin.Z, 0.0f);
	Data.Params = FVector4(0.0f, 0.0f, 0.0f, 0.0f);

	BindState(EBlendState::Opaque, ERasterizerState::SolidBack, EDepthStencilState::Default);

	UStaticMesh* AxisMesh = bScale ? ScaleHandleMesh : ArrowMesh;
	for (int i = X; i <= Z; ++i)
	{
		const bool bActive = (i == Active) || PlaneContainsAxis(Active, i) || (bScale && Active == Screen);
		if (!bActive && !Gizmo.IsHandleVisible(i, ViewDir))
			continue;
		// 드래그 중에는 관련 없는 축을 숨겨 조작 대상이 잘 보이게 한다.
		if (bDragging && !bActive)
			continue;

		Data.World = MakeAlongMatrix(Gizmo.GetAxisDirection(i), Origin);
		Data.Color = bActive ? HighlightColor : AxisColors[i];
		DrawMesh(AxisMesh, Data);
	}

	if (!bDragging || Active == Screen)
	{
		if (bScale)
		{
			Data.World = MakeBasisMatrix(Gizmo.GetAxisDirection(X), Gizmo.GetAxisDirection(Y), Gizmo.GetAxisDirection(Z), Origin);
			Data.Color = (Active == Screen) ? HighlightColor : ScreenColor;
			DrawMesh(CenterCubeMesh, Data);
		}
		else
		{
			Data.World = FMatrix::MakeTranslation(Origin);
			Data.Color = (Active == Screen) ? HighlightColor : ScreenColor;
			DrawMesh(CenterSphereMesh, Data);
		}
	}

	// 평면 핸들: 양면, 깊이 쓰기 없이 반투명으로 그린다.
	BindState(EBlendState::AlphaBlend, ERasterizerState::SolidNone, EDepthStencilState::ReadOnly);
	Data.ViewDirShade.W = 0.0f;
	Data.Params = FVector4(PlaneBorder, 0.0f, 0.0f, 0.0f);

	const float Size = PlaneOuter - PlaneInner;
	for (int Plane = PlaneYZ; Plane <= PlaneXY; ++Plane)
	{
		const bool bActive = (Plane == Active);
		if (bDragging && !bActive)
			continue;
		if (!bActive && !Gizmo.IsHandleVisible(Plane, ViewDir))
			continue;

		const FVector A = Gizmo.GetAxisDirection(PlaneAxisA(Plane));
		const FVector B = Gizmo.GetAxisDirection(PlaneAxisB(Plane));
		const FVector N = Gizmo.GetAxisDirection(PlaneNormalAxis(Plane));

		Data.World = MakeBasisMatrix(A * Size, B * Size, N, Origin + (A + B) * PlaneInner);
		Data.Color = bActive
			? WithAlpha(HighlightColor, PlaneAlphaHover)
			: WithAlpha(AxisColors[PlaneNormalAxis(Plane)], PlaneAlpha);
		DrawMesh(QuadMesh, Data);
	}
}

// 회전: 축 링 3개(앞쪽 절반), 카메라 축 링, 트랙볼 영역.
void FGizmoRenderer::RenderRotate(const FGizmo& Gizmo, const FVector& Origin, const FVector& ViewDir, const FMatrix& ViewProj)
{
	const int Active = Gizmo.GetHoveredAxis();
	const bool bDragging = Gizmo.IsUsing();
	// 축 링이나 카메라 링을 돌리는 중에는 그 링만 보여 준다. 트랙볼은 전체를 보여 준다.
	const bool bSoloRing = bDragging && Active != Trackball;

	FGizmoData Data{};
	Data.ViewProj = ViewProj;
	Data.ViewDirShade = FVector4(ViewDir.X, ViewDir.Y, ViewDir.Z, RingShade);
	Data.Params = FVector4(0.0f, 0.0f, 0.0f, 0.0f);

	BindState(EBlendState::Opaque, ERasterizerState::SolidBack, EDepthStencilState::Default);

	// 축 링은 카메라 반대편 절반을 잘라 겹침 없이 깔끔하게 보이게 한다.
	Data.OriginClip = FVector4(Origin.X, Origin.Y, Origin.Z, 1.0f);
	for (int i = X; i <= Z; ++i)
	{
		if (bSoloRing && i != Active)
			continue;

		Data.World = MakeAlongMatrix(Gizmo.GetAxisDirection(i), Origin);
		Data.Color = (i == Active) ? HighlightColor : AxisColors[i];
		DrawMesh(RingMesh, Data);
	}

	Data.OriginClip.W = 0.0f;
	if (!bSoloRing || Active == Screen)
	{
		Data.World = MakeAlongMatrix(ViewDir, Origin);
		Data.Color = (Active == Screen) ? HighlightColor : ScreenColor;
		DrawMesh(ScreenRingMesh, Data);
	}

	// 트랙볼 영역: 올려 두거나 끄는 중일 때만 옅은 원판으로 표시한다.
	if (Active == Trackball)
	{
		BindState(EBlendState::AlphaBlend, ERasterizerState::SolidNone, EDepthStencilState::ReadOnly);
		Data.ViewDirShade.W = 0.0f;
		Data.World = MakeAlongMatrix(ViewDir, Origin);
		Data.Color = WithAlpha(FVector4(1.0f, 1.0f, 1.0f, 1.0f), bDragging ? TrackballAlphaDrag : TrackballAlphaHover);
		DrawMesh(DiscMesh, Data);
	}
}

// 상수를 올리고 지정 메시를 그린다. 픽셀 셰이더도 음영·테두리 값을 읽으므로 두 단계 모두에 바인딩한다.
void FGizmoRenderer::DrawMesh(UStaticMesh* Mesh, const FGizmoData& Data)
{
	if (!Mesh)
		return;

	RenderCommand::BindMesh(Mesh);
	RenderCommand::UpdateBufferData(CB.get(), &Data, sizeof(FGizmoData));
	RenderCommand::BindConstantBuffer(0, CB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel);
	RenderCommand::DrawIndexed(Mesh->IndexBuffer->GetIndexCount());
}
