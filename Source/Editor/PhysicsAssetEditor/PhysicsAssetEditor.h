#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/EditorModeManager.h"
#include "Physics/AggregateGeom.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsHandle.h"
#include "Physics/PhysicsAssetUtils.h"

class UPhysicsAsset;
class UPhysicsConstraintTemplate;
class UBodySetup;
class USkeletalBodySetup;
class FPhysicsScene;

enum class EPhysicsAssetSelection : uint8 { None, Body, Constraint };

class FPhysicsAssetEditor : public FSkeletalPreviewEditor
{
public:
	// SkeletalMesh 에디터와 같은 메시 경로라 접미사로 구분한다
	static FString MakeDocumentKey(const FString& MeshPath) { return MeshPath + ".physicsasset"; }

	// InAsset을 주면 메시에 연결된 에셋 대신 그 에셋(InAssetPath 파일)을 편집한다
	FPhysicsAssetEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
		const FString& InMeshPath, USkeletalMesh& InMesh, UPhysicsAsset* InAsset = nullptr, const FString& InAssetPath = FString())
		: FSkeletalPreviewEditor(InEditorEngine, InEditorManager, InAsset ? InAssetPath : MakeDocumentKey(InMeshPath))
		, MeshPath(InMeshPath)
		, StandaloneAsset(InAsset)
		, StandaloneAssetPath(InAssetPath)
	{
		Mesh = &InMesh;
		CameraSpeed = 1.0f;
	}

	const char* GetEditorTypeName() const override { return "PhysicsAsset"; }

	void OnRenderLine(FLineBatcher& LineBatcher) override;

	const FGizmo* GetPreviewGizmo() const override { return PhysicsGizmo.GetTarget() ? &PhysicsGizmo : nullptr; }
	UE::Widget::EWidgetMode GetGizmoMode() const override { return ModeTools.GetWidgetMode(); }
	void SetGizmoMode(UE::Widget::EWidgetMode Mode) override;
	bool IsGizmoDragging() const override { return PhysicsGizmo.IsUsing(); }

	UPhysicsAsset* GetPhysicsAsset() const { return PhysicsAsset; }

	bool IsDirty() const override { return bDirty; }
	bool ConfirmClose() override;
	void MarkDirty() { bDirty = true; }
	bool Save();

	// 시작할 때 에셋으로 바디를 다시 만들고, 멈추면 컴포넌트 위치와 포즈를 되돌린다
	void ToggleSimulation();
	bool IsSimulating() const { return bSimulating; }

	void SelectBody(int32 BodyIndex);
	void SelectShape(int32 BodyIndex, EAggCollisionShape::Type ShapeType, int32 ShapeIndex);
	void SelectConstraint(int32 ConstraintIndex);
	EPhysicsAssetSelection GetSelectionType() const { return SelectionType; }
	int32 GetSelectedBodyIndex() const { return SelectionType == EPhysicsAssetSelection::Body ? SelectionIndex : INDEX_NONE; }
	// 바디만 선택했으면 Unknown
	EAggCollisionShape::Type GetSelectedShapeType() const { return SelectionType == EPhysicsAssetSelection::Body ? SelectedShapeType : EAggCollisionShape::Unknown; }
	int32 GetSelectedShapeIndex() const { return SelectionType == EPhysicsAssetSelection::Body ? SelectedShapeIndex : INDEX_NONE; }
	int32 GetSelectedConstraintIndex() const { return SelectionType == EPhysicsAssetSelection::Constraint ? SelectionIndex : INDEX_NONE; }
	int32 FindBoneIndexByName(FName BoneName) const;

	// 가장 가까운 조상 바디와 Constraint로 잇고, 자식 바디의 Constraint도 새 바디로 다시 잇는다
	void MakeNewBody(int32 BoneIndex);
	// 자식 바디의 Constraint를 지울 바디의 부모 바디로 옮긴 뒤 지운다
	void DeleteBody(int32 BodyIndex);
	void DeleteConstraint(int32 ConstraintIndex);
	// 바디와 Constraint는 두고 Shape만 본 정점으로 다시 만든다
	void RegenerateBody(int32 BodyIndex);
	// 확인 후 바디와 Constraint를 모두 지우고 에셋을 처음 만들 때와 같은 설정으로 다시 만든다
	void RegenerateAllBodies();
	// ChildBodyIndex 본 원점에 놓이는 Constraint로 두 바디를 잇는다. 이미 이어져 있으면 아무것도 하지 않는다
	void MakeNewConstraint(int32 ChildBodyIndex, int32 ParentBodyIndex);
	bool HasConstraintBetween(int32 BodyIndexA, int32 BodyIndexB) const;
	// 본 원점에 기본 크기로 만들고 선택한다
	void AddShape(int32 BodyIndex, EAggCollisionShape::Type ShapeType);
	// 마지막 Shape면 바디도 지운다
	void DeleteShape(int32 BodyIndex, EAggCollisionShape::Type ShapeType, int32 ShapeIndex);

	bool bShowBodies = true;
	bool bShowConstraints = true;
	float ConstraintDrawSize = 1.0f;

protected:
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override;
	void ShutdownResources() override;
	void OnBoneSelected() override;
	void TickAfterWorld(float DeltaTime) override;
	bool IsViewportInteractionBusy() const override { return PhysicsGizmo.IsUsing() || MouseHandle.IsGrabbing(); }
	void CancelViewportInteraction() override { PhysicsGizmo.EndDrag(); MouseHandle.ReleaseComponent(); }

private:
	// 바인드 박스 대각선이 이보다 크면 cm 값으로 본다. 사람 크기 메시는 cm면 200 안팎, m면 2 안팎이다
	static constexpr float CentimeterMeshMinSize = 10.0f;

	FPhysAssetCreateParams MakeCreateParams() const;
	void UpdatePhysicsPicking();
	USkeletalBodySetup* GetSelectedBodySetup() const;
	// 선택한 Shape. 바디만 선택했으면 첫 Shape (Sphyl, Box, Sphere 순)
	bool ResolveSelectedShape(EAggCollisionShape::Type& OutType, int32& OutIndex) const;
	bool GetSelectedShapeLocal(FVector& OutCenter, FQuat& OutRotation) const;
	UPhysicsConstraintTemplate* GetSelectedConstraintTemplate() const;
	bool GetShapeGizmoTransform(FTransform& OutTransform) const;
	// 부모 쪽 프레임(Frame2) 위치. 크기 모드에서는 기즈모를 숨긴다
	bool GetConstraintGizmoTransform(FTransform& OutTransform) const;
	void SyncPhysicsGizmo();
	bool UpdatePhysicsGizmo();
	bool BeginShapeDrag();
	bool BeginConstraintDrag();
	void ApplyShapeTransform();
	void ApplyConstraintTransform();
	// 레이에 맞은 Body·Constraint 중 가장 가까운 것. Constraint가 맞으면 Body보다 우선한다
	bool PickPhysics(const struct FRay& Ray, EPhysicsAssetSelection& OutType, int32& OutIndex,
		EAggCollisionShape::Type& OutShapeType, int32& OutShapeIndex) const;
	bool PickBody(const struct FRay& Ray, int32& OutBodyIndex, EAggCollisionShape::Type& OutShapeType,
		int32& OutShapeIndex, float& OutT) const;
	FString GetSaveFilePath() const;
	void ClearSelection();
	bool IsBoneChildOf(int32 BoneIndex, int32 AncestorBoneIndex) const;
	// 자기 자신부터 위로 올라가며 처음 만나는 바디
	int32 FindControllingBodyIndex(int32 BoneIndex) const;
	int32 FindChildConstraintIndex(FName ChildBoneName) const;
	// bKeepProfile이면 각도 제한 설정은 그대로 두고 본과 프레임만 다시 잡는다
	void InitConstraintSetup(UPhysicsConstraintTemplate* Template, int32 ChildBodyIndex, int32 ParentBodyIndex, bool bKeepProfile);
	void RevertFromDisk();
	void StartSimulation();
	void StopSimulation();
	void CreateSimulationFloor(FPhysicsScene& Scene);
	// Ctrl/Shift + 우클릭 드래그로 MouseHandle을 잡고 옮긴다
	void UpdateGrab();
	void MoveGrab();

	FString MeshPath;
	// 메시에 연결되지 않은 에셋 파일을 열었을 때만 채운다
	UPhysicsAsset* StandaloneAsset = nullptr;
	FString StandaloneAssetPath;
	bool bDirty = false;

	bool bSimulating = false;
	FMatrix PreSimulationComponentWorld;
	UBodySetup* FloorBodySetup = nullptr;
	FBodyInstance FloorBody;
	class UStaticMeshComponent* FloorMeshComponent = nullptr;

	FPhysicsHandle MouseHandle;
	// 카메라 전방 축 기준 깊이. 마우스는 이 깊이의 카메라 평면 위에서 움직인다
	float GrabDepth = 0.0f;

	UPhysicsAsset* PhysicsAsset = nullptr;
	EPhysicsAssetSelection SelectionType = EPhysicsAssetSelection::None;
	int32 SelectionIndex = INDEX_NONE;
	EAggCollisionShape::Type SelectedShapeType = EAggCollisionShape::Unknown;
	int32 SelectedShapeIndex = INDEX_NONE;

	USceneComponent* GizmoTarget = nullptr;
	// PhysicsGizmo가 생성자에서 포인터를 받으므로 먼저 선언해야 한다
	FEditorModeTools ModeTools;
	FGizmo PhysicsGizmo{ &ModeTools };
	FKAggregateGeom DragStartGeom;
	FConstraintInstance DragStartConstraint;
	FMatrix DragBoneWorldInverse;
	FQuat DragBoneWorldRotation;
	FMatrix DragChildBoneWorldInverse;
	FMatrix DragChildFrameRelative;
};
