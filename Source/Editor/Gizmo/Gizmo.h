#pragma once

#include "Collision/Ray.h"
#include "Math/Transform.h"
#include "Components/SceneComponent.h"
#include "Components/LocalLightComponent.h"

#include "Editor/UnrealWidgetFwd.h"

class UCameraComponent;
class FGizmoRenderer;
class FEditorModeTools;
struct FSceneView;

// 기즈모 핸들 번호와 치수. 피킹(FGizmo)과 그리기(FGizmoRenderer)가 같은 값을 쓴다.
// 치수는 기즈모 로컬 단위다. 원근 뷰에서는 기즈모를 카메라 앞 일정 거리에 그리므로 화면 크기가 일정하다.
namespace GizmoHandle
{
	constexpr int X = 0;
	constexpr int Y = 1;
	constexpr int Z = 2;
	constexpr int PlaneYZ = 3;   // 평면 핸들 = 3 + 평면 법선 축
	constexpr int PlaneZX = 4;
	constexpr int PlaneXY = 5;
	constexpr int Screen = 6;    // 이동: 화면 평면 자유 이동, 크기: 균등 크기, 회전: 카메라 축 링
	constexpr int Trackball = 7; // 회전: 카메라 축 링 안쪽 빈 공간을 끌어 자유 회전

	constexpr bool IsAxis(int Handle) { return Handle >= X && Handle <= Z; }
	constexpr bool IsPlane(int Handle) { return Handle >= PlaneYZ && Handle <= PlaneXY; }
	// 평면 핸들이 포함하는 두 축과 법선 축
	constexpr int PlaneNormalAxis(int Handle) { return Handle - PlaneYZ; }
	constexpr int PlaneAxisA(int Handle) { return (Handle - PlaneYZ + 1) % 3; }
	constexpr int PlaneAxisB(int Handle) { return (Handle - PlaneYZ + 2) % 3; }
	constexpr bool PlaneContainsAxis(int Handle, int Axis) { return IsPlane(Handle) && Axis != PlaneNormalAxis(Handle); }

	constexpr float TranslateAxisLength = 1.25f;
	constexpr float ScaleAxisLength = 1.20f;
	constexpr float PlaneInner = 0.28f;   // 평면 핸들 사각형이 축을 따라 시작하는 거리
	constexpr float PlaneOuter = 0.56f;   // 끝나는 거리
	constexpr float CenterPickPixels = 11.0f;
	constexpr float RingRadius = 1.0f;
	constexpr float ScreenRingRadius = 1.2f;

	// 축이 시선과 거의 나란하거나 평면이 시선과 거의 나란하면 조작이 불안정하므로 숨긴다.
	constexpr float AxisHideDot = 0.985f;
	constexpr float PlaneHideDot = 0.18f;
}

class FGizmo   // 상태 + 로직
{
public:
	// InModeTools는 이 기즈모가 편집 모드와 좌표계를 읽어 올 모드 툴이다. 기즈모보다 오래 살아 있어야 한다.
	explicit FGizmo(FEditorModeTools* InModeTools);
	~FGizmo();

	// 이 기즈모를 View의 카메라 기준으로 그린다. 씬 위에 올리는 패스 안에서 부른다.
	// UE의 FWidget::Render와 같은 자리다. UE는 Render(View, PDI, ViewportClient)이고 이 엔진은 View만 받는다.
	void Render(const FSceneView* View) const;

	// Gizmo 대상 설정
	void SetTarget(USceneComponent* InTarget, bool InbUseActorBounds = false)
	{ 
		Target = InTarget; 
		bUseActorBounds = InbUseActorBounds; 
	}
	USceneComponent* GetTarget() const { return Target; }

	// 지금의 편집 모드(이동, 회전, 크기)와 좌표계. 기즈모가 갖지 않고 모드 툴에서 읽는다. 바꾸려면 모드 툴에 설정한다.
	// UE에서는 FWidget이 뷰포트 클라이언트의 GetWidgetMode, GetWidgetCoordSystemSpace로 읽고,
	// 뷰포트 클라이언트는 자기 모드 툴에 묻는다. 이 엔진은 아직 기즈모가 뷰포트에 속하지 않아 모드 툴에 바로 묻는다.
	UE::Widget::EWidgetMode GetWidgetMode() const;
	ECoordSystem GetWidgetCoordSystemSpace() const;

	// Active View의 Ray와 행렬만 사용해 Hover·Drag 상태를 갱신한다.
	void Update(const FRay& MouseRay, const FVector2& MousePos,
		const FMatrix& ViewProj, int ScreenW, int ScreenH,
		bool bMouseDown, const FVector& CameraLocation, bool bCameraOrthographic);

	bool IsUsing() const { return DraggingAxis >= 0; }
	int GetHoveredAxis() const { return HoveredAxis; }
	int GetDraggingAxis() const { return DraggingAxis; }

	// 카메라 -> 기즈모 방향에서 이 핸들이 보이고 잡을 수 있는지. 그리기와 피킹이 같이 쓴다.
	bool IsHandleVisible(int Handle, const FVector& ViewDir) const;
	// N에 수직인 정규 직교 기저 U, V를 만든다. U x V = N이 되도록 고른다.
	static void MakeBasis(const FVector& N, FVector& OutU, FVector& OutV);

	int PickAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH);

	int PickLinearAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH);
	int PickRotationAxis(const FVector2& MousePos, const FMatrix& ViewProj, int ScreenW, int ScreenH);


	void BeginDrag(int Axis, const FRay& MouseRay, const FVector2& MousePos);
	void UpdateDrag(const FRay& MouseRay, const FVector2& MousePos);
	void EndDrag();

	float ComputeAngleOnPlane(const FVector& Point, int Axis) const;
	// 마우스 Ray를 기즈모 중심의 트랙볼 구에 투영한 단위 벡터. 구를 벗어나면 외곽선 위로 붙인다.
	FVector ProjectToTrackball(const FRay& MouseRay) const;

	inline FTransform GetTransform() const { return Target ? Target->GetTransform() : FTransform(); }
	inline FVector GetLocation() const { return Target ? Target->GetRelativeLocation() : FVector(0, 0, 0); }
	inline FRotator GetRotation() const { return Target ? Target->GetRelativeRotation() : FRotator(0, 0, 0); }
	inline FVector GetScale() const { return Target ? Target->GetRelativeScale3D() : FVector(0, 0, 0); }
	FRotator GetAxisRotation() const;

	FVector GetRenderLocation() const;
	// 원근 View에서만 카메라 거리에 맞춰 크기 보정 위치를 구하고 직교 View는 월드 위치를 유지한다.
	FVector GetRenderLocationForView(const FVector& CameraLocation, bool bCameraOrthographic) const;
	FVector GetCameraLocation() const;
	// 현재 입력 View의 시선 방향 (카메라 -> 기즈모). 직교 뷰는 Ray 방향을 쓴다.
	FVector GetViewDirection() const;
	FVector GetAxisDirection(int Axis) const;
private:
	FEditorModeTools* ModeTools = nullptr;
	FTransform Transform = FTransform();
	USceneComponent* Target = nullptr;
	bool bUseActorBounds = false;

	int HoveredAxis = -1;
	int DraggingAxis = -1;

	FVector2 DragStartMousePos;
	FVector DragStartPoint;
	FVector DragStartLocation;
	FRotator DragStartRotation;
	FVector DragStartScale;
	FVector DragAxisDirection;
	FVector DragStartRenderLocation;

	FVector DragPlaneNormal;

	FVector ViewCameraLocation{};
	FVector ViewRayDirection{ 1.0f, 0.0f, 0.0f };
	bool bViewCameraOrthographic = false;

	// 트랙볼 회전: 매 프레임 직전 구 위 점에서 현재 점까지의 회전을 누적한다.
	FVector TrackballPrevVector{};
	FQuat TrackballAccum = FQuat::Identity();

	float DragStartAngle = 0.0f;
	float RingRadius = GizmoHandle::RingRadius;

	// Light Component인 경우 Radius를 증감하기 위한 멤버변수와 함수
	float DragStartRadius = 0.0f;
	bool ApplyScaleToLight(float Factor);
	ULocalLightComponent* GetTargetLight() const;

	// 축 메시와 셰이더로 실제 그리기를 맡는 도구. 기즈모마다 하나씩 갖는다.
	TUniquePtr<FGizmoRenderer> Renderer;
};
