#include "EnginePCH.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/Gizmo/GizmoRenderer.h"
#include "Editor/EditorModeManager.h"
#include "Render/SceneView.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Actor/LightActorBase.h"

UE::Widget::EWidgetMode FGizmo::GetWidgetMode() const
{
	return ModeTools ? ModeTools->GetWidgetMode() : UE::Widget::WM_Translate;
}

ECoordSystem FGizmo::GetWidgetCoordSystemSpace() const
{
	return ModeTools ? ModeTools->GetCoordSystem() : COORD_Local;
}

static const FVector AxisDirs[3] = {
	FVector(1, 0, 0),
	FVector(0, 1, 0),
	FVector(0, 0, 1),
};

using namespace GizmoHandle;

static const float HitPixels = 9.0f;

namespace
{
	// 화면상 볼록 사각형 안에 점이 있는지 (꼭짓점 순서와 무관).
	bool PointInConvexQuad(const FVector2& P, const FVector2 (&Q)[4])
	{
		bool bHasPos = false, bHasNeg = false;
		for (int i = 0; i < 4; ++i)
		{
			const FVector2& A = Q[i];
			const FVector2& B = Q[(i + 1) % 4];
			const float Cross = (B.X - A.X) * (P.Y - A.Y) - (B.Y - A.Y) * (P.X - A.X);
			if (Cross > 0.0f) bHasPos = true;
			if (Cross < 0.0f) bHasNeg = true;
		}
		return !(bHasPos && bHasNeg);
	}

	float ScreenDistance(const FVector2& A, const FVector2& B)
	{
		const FVector2 D = A - B;
		return sqrtf(D.X * D.X + D.Y * D.Y);
	}

	// From을 To로 돌리는 최단 회전.
	FQuat QuatFromTo(const FVector& From, const FVector& To)
	{
		const FVector Axis = From.Cross(To);
		const float SinAngle = Axis.Length();
		if (SinAngle < 1e-6f)
			return FQuat::Identity();
		return FQuat::MakeFromAxisAngle(Axis, atan2f(SinAngle, From.Dot(To)));
	}
}

// 그리는 도구를 만들고 축 메시, 셰이더, 상수 버퍼를 준비한다. FGizmoRenderer::Init의 렌더러 인자는 쓰이지 않는다.
FGizmo::FGizmo(FEditorModeTools* InModeTools)
	: ModeTools(InModeTools)
{
	Renderer = MakeUnique<FGizmoRenderer>();
	Renderer->Init(nullptr);
}

// FGizmoRenderer가 완전한 타입인 이 파일에서 소멸자를 정의해야 TUniquePtr가 지울 수 있다.
FGizmo::~FGizmo() = default;

void FGizmo::Render(const FSceneView* View) const
{
	Renderer->OnRender(*this, View->ViewProjectionMatrix, View->ViewLocation, !View->bIsPerspective, View->ViewForward);
}

// 입력 View의 카메라 조건으로 축 선택과 드래그를 갱신한다.
void FGizmo::Update(const FRay& MouseRay, const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH, bool bMouseDown, const FVector& CameraLocation, const bool bCameraOrthographic)
{
	ViewCameraLocation = CameraLocation;
	bViewCameraOrthographic = bCameraOrthographic;
	if (MouseRay.Direction.Length() > 1e-6f)
		ViewRayDirection = MouseRay.Direction.Normalized();

	if (!Target) // 만약 현재 타겟이 없다면
	{
		HoveredAxis = -1;
		DraggingAxis = -1;
		return;
	}

	// Select 모드(WM_None)는 기즈모를 그리지도 잡지도 않고 선택만 한다 (UE5 Select Mode, 단축키 Q).
	if (GetWidgetMode() == UE::Widget::WM_None)
	{
		if (DraggingAxis >= 0)
			EndDrag();
		HoveredAxis = -1;
		DraggingAxis = -1;
		return;
	}

	if (DraggingAxis >= 0) // 만약 드래그 중인 축이 있다면
	{
		if (bMouseDown)
			UpdateDrag(MouseRay, MousePos);
		else
			EndDrag();
		return;
	}

	HoveredAxis = PickAxis(MousePos, ViewProj, ScreenW, ScreenH); // 현재 마우스가 올라간 기즈모 축이 있으면

	if (bMouseDown && HoveredAxis >= 0) // 마우스가 올라간 축이 있고 마우스가 눌렸으면
		BeginDrag(HoveredAxis, MouseRay, MousePos);


}

// 모드에 따라 선형 축 또는 회전 링 피킹을 선택한다.
int FGizmo::PickAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH)
{
	if (GetWidgetMode() == UE::Widget::WM_Rotate)
		return PickRotationAxis(MousePos, ViewProj, ScreenW, ScreenH);

	return PickLinearAxis(MousePos, ViewProj, ScreenW, ScreenH);
}

// 중앙 핸들 -> 축 -> 평면 순서로, 화면에 투영한 핸들과 마우스 위치를 비교해 고른다.
int FGizmo::PickLinearAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH)
{
	const FVector Origin = GetRenderLocation();
	const FVector ViewDir = GetViewDirection();
	const FVector2 Center = WorldToScreen(Origin, ViewProj, ScreenW, ScreenH);

	if (ScreenDistance(MousePos, Center) < CenterPickPixels)
		return Screen;

	const float AxisLength = (GetWidgetMode() == UE::Widget::WM_Scale) ? ScaleAxisLength : TranslateAxisLength;

	int Best = -1;
	float BestDist = HitPixels;
	for (int i = X; i <= Z; ++i)
	{
		if (!IsHandleVisible(i, ViewDir))
			continue;

		const FVector2 End = WorldToScreen(Origin + GetAxisDirection(i) * AxisLength, ViewProj, ScreenW, ScreenH);
		const float Dist = DistanceToSegment(MousePos, Center, End);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = i;
		}
	}
	if (Best >= 0)
		return Best;

	// 평면 핸들: 두 축이 만드는 작은 사각형 안을 누르면 그 평면에서 움직인다.
	float BestFacing = 0.0f;
	for (int Plane = PlaneYZ; Plane <= PlaneXY; ++Plane)
	{
		if (!IsHandleVisible(Plane, ViewDir))
			continue;

		const FVector A = GetAxisDirection(PlaneAxisA(Plane));
		const FVector B = GetAxisDirection(PlaneAxisB(Plane));
		const FVector2 Quad[4] = {
			WorldToScreen(Origin + A * PlaneInner + B * PlaneInner, ViewProj, ScreenW, ScreenH),
			WorldToScreen(Origin + A * PlaneOuter + B * PlaneInner, ViewProj, ScreenW, ScreenH),
			WorldToScreen(Origin + A * PlaneOuter + B * PlaneOuter, ViewProj, ScreenW, ScreenH),
			WorldToScreen(Origin + A * PlaneInner + B * PlaneOuter, ViewProj, ScreenW, ScreenH),
		};
		if (!PointInConvexQuad(MousePos, Quad))
			continue;

		// 겹치면 카메라를 더 정면으로 보는 평면을 고른다.
		const float Facing = fabsf(GetAxisDirection(PlaneNormalAxis(Plane)).Dot(ViewDir));
		if (Facing > BestFacing)
		{
			BestFacing = Facing;
			Best = Plane;
		}
	}
	return Best;
}

// 회전 링을 선분으로 나누어 화면 거리로 회전 축을 고른다.
// 축 링은 카메라 쪽 절반만 그리므로 피킹도 앞쪽 절반만 본다.
// 어느 링에도 닿지 않고 카메라 축 링 안쪽이면 트랙볼(자유 회전)을 고른다.
int FGizmo::PickRotationAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH)
{
	const FVector Origin = GetRenderLocation();
	const FVector ViewDir = GetViewDirection();
	const int Segments = 64;

	int Best = -1;
	float BestDist = HitPixels;

	auto TestRing = [&](const FVector& U, const FVector& V, float Radius, int Handle, bool bFrontOnly)
	{
		FVector PrevWorld = Origin + U * Radius;
		FVector2 Prev = WorldToScreen(PrevWorld, ViewProj, ScreenW, ScreenH);
		for (int s = 1; s <= Segments; ++s)
		{
			const float Theta = (float)s / Segments * 2.0f * PI;
			const FVector World = Origin + U * (Radius * cosf(Theta)) + V * (Radius * sinf(Theta));
			const FVector2 Cur = WorldToScreen(World, ViewProj, ScreenW, ScreenH);

			const FVector Mid = (PrevWorld + World) * 0.5f;
			if (!bFrontOnly || (Mid - Origin).Dot(ViewDir) <= 0.0f)
			{
				const float Dist = DistanceToSegment(MousePos, Prev, Cur);
				if (Dist < BestDist)
				{
					BestDist = Dist;
					Best = Handle;
				}
			}
			Prev = Cur;
			PrevWorld = World;
		}
	};

	for (int Axis = X; Axis <= Z; ++Axis)
		TestRing(GetAxisDirection((Axis + 1) % 3), GetAxisDirection((Axis + 2) % 3), RingRadius, Axis, true);

	FVector U, V;
	MakeBasis(ViewDir, U, V);
	TestRing(U, V, ScreenRingRadius, Screen, false);

	if (Best >= 0)
		return Best;

	// 카메라 축 링 안쪽 빈 공간
	const FVector2 Center = WorldToScreen(Origin, ViewProj, ScreenW, ScreenH);
	const float ScreenRadius = ScreenDistance(WorldToScreen(Origin + U * ScreenRingRadius, ViewProj, ScreenW, ScreenH), Center);
	if (ScreenDistance(MousePos, Center) < ScreenRadius - HitPixels)
		return Trackball;

	return -1;
}

bool FGizmo::IsHandleVisible(int Handle, const FVector& ViewDir) const
{
	if (IsAxis(Handle))
		return fabsf(GetAxisDirection(Handle).Dot(ViewDir)) < AxisHideDot;
	if (IsPlane(Handle))
		return fabsf(GetAxisDirection(PlaneNormalAxis(Handle)).Dot(ViewDir)) > PlaneHideDot;
	return true;
}

void FGizmo::MakeBasis(const FVector& N, FVector& OutU, FVector& OutV)
{
	// 시선이 거의 수직(탑 뷰 등)이면 Z 대신 X를 기준으로 삼아 기저가 무너지지 않게 한다.
	const FVector Ref = (fabsf(N.Z) > 0.99f) ? FVector(1, 0, 0) : FVector(0, 0, 1);
	OutU = Ref.Cross(N).Normalized();
	OutV = N.Cross(OutU);
}

FVector FGizmo::ProjectToTrackball(const FRay& MouseRay) const
{
	const FVector Center = DragStartRenderLocation;
	const FVector Dir = MouseRay.Direction.Normalized();
	const float Radius = RingRadius;

	const float TClosest = (Center - MouseRay.Origin).Dot(Dir);
	const FVector Closest = MouseRay.Origin + Dir * TClosest;
	const FVector ToClosest = Closest - Center;
	const float DistSq = ToClosest.Dot(ToClosest);

	if (DistSq <= Radius * Radius)
	{
		const float T = TClosest - sqrtf(Radius * Radius - DistSq);
		return (MouseRay.Origin + Dir * T - Center).Normalized();
	}
	// 구 밖: 시선에 수직인 외곽선 위의 점. 이 영역에서 끌면 시선 축 둘레로 돈다.
	return ToClosest.Normalized();
}

// LocalLightComponet 가져오기 (Rect, Point, Spot 등 Radius를 파라미터로 것들)
ULocalLightComponent* FGizmo::GetTargetLight() const
{
    if (!Target) return nullptr;
    ALightActorBase* Actor = Cast<ALightActorBase>(Target->GetOwner());
    return Actor ? Cast<ULocalLightComponent>(Actor->GetLightComponent()) : nullptr;
}

// 조명이면 radius만 바꾸고 true를 반환
bool FGizmo::ApplyScaleToLight(float Factor)
{
    ULocalLightComponent* Light = GetTargetLight();
    if (!Light) return false;
    Light->SetAttenuationRadius(DragStartRadius * Factor);
    return true;
}

// 시작 Transform·교차점을 저장하고 조작 축의 드래그 평면을 정한다.
void FGizmo::BeginDrag(int Axis, const FRay& MouseRay, const FVector2& MousePos)
{
	// 부모의 변환 행렬에 Scale이 0이 포함되면 역행렬이 존재하지 않아서 드래그를 진행할 수 없음
	// 따라서, 부모의 WorldMatrix의 행렬식이 0이라면 드래그 종료
	if (GetWidgetMode() == UE::Widget::WM_Translate && Target->GetAttachParent() &&
		fabsf(Target->GetAttachParent()->GetWorldMatrix().Determinant()) < 1e-12f)
	{
		EndDrag();
		return;
	}

	DragStartMousePos = MousePos;
	DraggingAxis = Axis;
	DragStartLocation = Target->GetWorldLocation();
	DragStartRenderLocation = GetRenderLocation();    
	if (GetWidgetMode() == UE::Widget::WM_Translate)
	{
		// 이동량은 모든 에디터에서 실제 대상 위치를 지나는 평면으로 계산합니다.
		DragStartRenderLocation = Target->GetWorldLocation();
	}

	DragStartRotation = GetRotation();
	DragStartScale = GetScale();

	// Local LightComponent면 Radius 가져와서 저장해놓기
	if (ULocalLightComponent* Light = GetTargetLight())
    	DragStartRadius = Light->GetAttenuationRadius();

	DragAxisDirection = GetAxisDirection(Axis);
	FVector axis = DragAxisDirection;

	if (Axis == Trackball)
	{
		DragAxisDirection = GetViewDirection();
		DragPlaneNormal = DragAxisDirection;
		DragStartPoint = DragStartRenderLocation;
		TrackballAccum = FQuat::Identity();
		TrackballPrevVector = ProjectToTrackball(MouseRay);
		return;
	}

	if (IsPlane(Axis))
	{
		// 평면 법선으로 드래그 평면을 잡는다. 크기 조절은 두 축의 대각선 방향 이동량을 쓴다.
		DragPlaneNormal = GetAxisDirection(PlaneNormalAxis(Axis));
		DragAxisDirection = (GetAxisDirection(PlaneAxisA(Axis)) + GetAxisDirection(PlaneAxisB(Axis))).Normalized();
	}
	else if (Axis == Screen)
	{
		if (GetWidgetMode() == UE::Widget::WM_Rotate)
		{
			DragAxisDirection = (GetRenderLocation() - GetCameraLocation()).Normalized();
			DragPlaneNormal = DragAxisDirection;
		}
		else
		{
			DragPlaneNormal = -MouseRay.Direction;
		}
	}
	else if (GetWidgetMode() == UE::Widget::WM_Rotate)
	{
		FVector ToCamera = (GetCameraLocation() - GetRenderLocation()).Normalized();
		if (DragAxisDirection.Dot(ToCamera) < 0.0f)
			DragAxisDirection = -DragAxisDirection;

		DragPlaneNormal = DragAxisDirection;
	}
	else
	{
		FVector ToCamera = -MouseRay.Direction;
		FVector Ortho = axis.Cross(ToCamera);
		DragPlaneNormal = Ortho.Cross(axis).Normalized();
	}

	float t;
	if (RayIntersectsPlane(MouseRay, DragStartRenderLocation, DragPlaneNormal, t))
		DragStartPoint = MouseRay.Origin + MouseRay.Direction * t;
	else
		DragStartPoint = DragStartRenderLocation;

	if (GetWidgetMode() == UE::Widget::WM_Rotate)
		DragStartAngle = ComputeAngleOnPlane(DragStartPoint, Axis);
}

// 평면 교차점 변화를 이동·회전·크기에 반영한다.
void FGizmo::UpdateDrag(const FRay& MouseRay, const FVector2& MousePos)
{
	if (DraggingAxis == Trackball)
	{
		// 직전 프레임의 구 위 점에서 현재 점까지 돌린 양을 누적한다. 누적이라 끌수록 계속 돈다.
		const FVector Current = ProjectToTrackball(MouseRay);
		TrackballAccum = (QuatFromTo(TrackballPrevVector, Current) * TrackballAccum).Normalized();
		TrackballPrevVector = Current;

		Target->SetRelativeRotation((TrackballAccum * DragStartRotation.Quaternion()).ToFRotator());
		return;
	}

	float t;
	if (!RayIntersectsPlane(MouseRay, DragStartRenderLocation, DragPlaneNormal, t))
		return;

	FVector current = MouseRay.Origin + MouseRay.Direction * t;

	if (DraggingAxis == Screen && GetWidgetMode() != UE::Widget::WM_Rotate)
	{
		if (GetWidgetMode() == UE::Widget::WM_Translate)
			Target->SetWorldLocation(DragStartLocation + (current - DragStartPoint), false);
		else
		{
			float dx = MousePos.X - DragStartMousePos.X;
			float dy = DragStartMousePos.Y - MousePos.Y;   // 화면 Y는 아래가 +
			float scaleDelta = (dx + dy) * 0.005f;

			float factor = 1.0f + scaleDelta;
			if (factor < 0.01f) factor = 0.01f;

			if (!ApplyScaleToLight(factor)) 	// 조명이면 여기서 radius만 변경하고 return
				Target->SetRelativeScale3D(DragStartScale * factor);
		}
		return;
	}

	if (GetWidgetMode() == UE::Widget::WM_Rotate)
	{
		float currentAngle = ComputeAngleOnPlane(current, DraggingAxis);
		float deltaAngle = currentAngle - DragStartAngle;

		FQuat delta = FQuat::MakeFromAxisAngle(DragAxisDirection, deltaAngle);
		FQuat start = DragStartRotation.Quaternion();

		FQuat result = delta * start;

		Target->SetRelativeRotation(result.ToFRotator());
		return;
	}

	FVector delta = current - DragStartPoint;

	if (IsPlane(DraggingAxis))
	{
		if (GetWidgetMode() == UE::Widget::WM_Translate)
		{
			// 교차점이 평면 위에 있으므로 변화량이 곧 평면 안의 이동량이다.
			Target->SetWorldLocation(DragStartLocation + delta, false);
		}
		else
		{
			float factor = 1.0f + delta.Dot(DragAxisDirection);
			if (factor < 0.01f) factor = 0.01f;

			if (ApplyScaleToLight(factor)) return;

			FVector NewScale = DragStartScale;
			for (int Axis : { PlaneAxisA(DraggingAxis), PlaneAxisB(DraggingAxis) })
			{
				if (Axis == X)      NewScale.X *= factor;
				else if (Axis == Y) NewScale.Y *= factor;
				else                NewScale.Z *= factor;
			}
			Target->SetRelativeScale3D(NewScale);
		}
		return;
	}

	float amount = delta.Dot(DragAxisDirection);

	if (GetWidgetMode() == UE::Widget::WM_Translate)
		Target->SetWorldLocation(DragStartLocation + DragAxisDirection * amount, false);
	else   // Scale
	{
		float factor = 1.0f + amount;
		if (factor < 0.01f) factor = 0.01f;

		// 조명이면 여기서 radius만 반영하고 return
		if (ApplyScaleToLight(factor)) return;

		FVector NewScale = DragStartScale;
		if (DraggingAxis == 0)      NewScale.X *= factor;
		else if (DraggingAxis == 1) NewScale.Y *= factor;
		else                        NewScale.Z *= factor;

		Target->SetRelativeScale3D(NewScale);
	}
}

// 드래그 축을 비워 조작을 종료한다.
void FGizmo::EndDrag()
{
	DraggingAxis = -1;
	HoveredAxis = -1;
}

// 평면 기저에 점을 투영하고 atan2로 회전각을 구한다.
float FGizmo::ComputeAngleOnPlane(const FVector& Point, int Axis) const
{
	FVector u, v;

	if (DraggingAxis >= 0)
	{
		FVector Ref = (fabsf(DragAxisDirection.Z) > 0.9f) ? FVector(1, 0, 0) : FVector(0, 0, 1);
		u = Ref.Cross(DragAxisDirection).Normalized();
		v = DragAxisDirection.Cross(u);
	}
	else if (Axis == Screen)
	{
		FVector Forward = (GetRenderLocation() - GetCameraLocation()).Normalized();
		u = FVector(0, 0, 1).Cross(Forward).Normalized();
		v = Forward.Cross(u);
	}
	else
	{
		u = AxisDirs[(Axis + 1) % 3];
		v = AxisDirs[(Axis + 2) % 3];
	}

	FVector local = Point - DragStartRenderLocation;
	return atan2f(local.Dot(v), local.Dot(u));
}

FRotator FGizmo::GetAxisRotation() const
{
	// 이동 Ray와 평면은 월드 공간이므로 부모 회전까지 포함한 축을 사용합니다.
	return GetWidgetMode() == UE::Widget::WM_Translate && Target ? Target->GetWorldRotation() : GetRotation();
}

// 월드 축 또는 로컬 회전으로 변환한 정규화 축을 반환한다.
FVector FGizmo::GetAxisDirection(int Axis) const
{
	if (Axis < 0 || Axis > 2) return FVector(0, 0, 0);
	bool bUseLocal = (GetWidgetCoordSystemSpace() == COORD_Local) || (GetWidgetMode() == UE::Widget::WM_Scale);

	if (bUseLocal && Target)
	{
		FMatrix rot = GetAxisRotation().Quaternion().ToFMatrix();
		FVector4 v = rot.TransformVector(AxisDirs[Axis]);
		return FVector(v.X, v.Y, v.Z).Normalized();
	}
	return AxisDirs[Axis];
}

// 입력 View 카메라 조건으로 Gizmo 표시 위치를 구한다.
FVector FGizmo::GetRenderLocation() const
{
	return GetRenderLocationForView(ViewCameraLocation, bViewCameraOrthographic);
}

// 직교에서는 대상 위치, 원근에서는 카메라 앞 일정 거리로 표시 위치를 정한다.
FVector FGizmo::GetRenderLocationForView(const FVector& CameraLocation, const bool bCameraOrthographic) const
{
	if (!Target) return FVector(0, 0, 0);

	FVector PivotLocation = Target->GetWorldLocation();

	// bounds 합집합 중앙에 기즈모 표시
	/*if (bUseActorBounds)
	{
		FBox Bounds;
		if (Target->GetOwner()->TryGetActorBounds(Bounds))
		{
			PivotLocation = (Bounds.Min + Bounds.Max) * 0.5f;
			
		}
	}*/

	if (bCameraOrthographic) return PivotLocation;

	return (PivotLocation - CameraLocation).Normalized() * 10.0f + CameraLocation;

}

FVector FGizmo::GetViewDirection() const
{
	if (bViewCameraOrthographic)
		return ViewRayDirection;
	return (GetRenderLocation() - ViewCameraLocation).Normalized();
}

// 현재 입력 View의 카메라 위치를 반환한다.
FVector FGizmo::GetCameraLocation() const
{
	return ViewCameraLocation;
}
