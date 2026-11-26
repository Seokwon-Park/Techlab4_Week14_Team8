#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/EditorModeManager.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Containers/Map.h"
#include "Containers/Set.h"


class FSkeletalMeshEditor : public FSkeletalPreviewEditor
{
public:
	FSkeletalMeshEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
		const FString& InAssetPath, USkeletalMesh& InMesh)
		: FSkeletalPreviewEditor(InEditorEngine, InEditorManager, InAssetPath)
	{
		Mesh = &InMesh;
	}

	const char* GetEditorTypeName() const override { return "SkeletalMesh"; }
	const FGizmo* GetPreviewGizmo() const override { return !bClothPaintMode && BoneGizmo.GetTarget() ? &BoneGizmo : nullptr; }
	void OnRenderLine(FLineBatcher& LineBatcher) override;

	// Cloth 섹션 선택과 이동 반경 편집 상태
	void SelectClothSection(int32 SectionIndex);
	int32 GetClothSectionIndex() const { return ClothSectionIndex; }
	bool IsClothPaintMode() const { return bClothPaintMode; }
	void SetClothPaintMode(bool bEnabled);
	float GetClothBrushRadius() const { return ClothBrushRadius; }
	void SetClothBrushRadius(float Radius) { ClothBrushRadius = Radius; }
	float GetClothBrushMaxDistance() const { return ClothBrushMaxDistance; }
	void SetClothBrushMaxDistance(float MaxDistance);
	int32 GetClothPinnedVertexCount() const { return ClothPinnedVertices.Num(); }
	int32 GetClothCappedVertexCount() const { return ClothMaxDistances.Num(); }
	int32 GetClothSectionVertexCount() const { return ClothSectionVertices.Num(); }
	bool ApplyClothBinding(FString& OutError);

	// 선택한 본의 프리뷰 포즈를 부모 기준 로컬 변환으로 조회·수정합니다.
	bool GetSelectedBoneLocalTransform(FTransform& OutTransform) const;
	void SetSelectedBoneLocalTransform(const FTransform& Transform);
	void ResetPose();
	void ResetBone(int32 BoneIndex);

	void SetGizmoMode(UE::Widget::EWidgetMode Mode) override;
	void SetGizmoSpace(ECoordSystem Space);
	UE::Widget::EWidgetMode  GetGizmoMode() const override { return ModeTools.GetWidgetMode(); }
	ECoordSystem GetGizmoSpace() const { return ModeTools.GetCoordSystem(); }
	bool IsGizmoDragging() const override { return BoneGizmo.IsUsing(); }

	void OpenPhysicsAssetEditor();

protected:
	// 이 에디터에 연결된 탭을 생성합니다.
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override;     // InitializePreview() 후 BoneGizmoTarget 생성
	void ShutdownResources() override;       // 기즈모 정리 후 Super

	void TickAfterWorld(float DeltaTime) override;
	bool IsViewportInteractionBusy() const override { return BoneGizmo.IsUsing(); }
	void OnBoneSelected() override { BoneGizmo.EndDrag(); SyncBoneGizmo(); }
	void CancelViewportInteraction() override
	{
		BoneGizmo.EndDrag();
		bClothStrokeActive = false;
	}

private:
	void UpdateClothPaintStroke();
	void PaintClothVertices(const FVector2& Start, const FVector2& End);

	void SyncBoneGizmo();
	bool UpdateBoneGizmo();
	void ApplyBoneGizmoTransform();
	FQuat GetBoneWorldRotation(int32 BoneIndex) const;

	USceneComponent* BoneGizmoTarget = nullptr;
	// 이 에디터의 편집 모드와 좌표계. 에셋 에디터는 레벨 에디터와 따로 하나씩 갖는다. BoneGizmo보다 먼저 선언해야 한다.
	FEditorModeTools ModeTools;
	FGizmo BoneGizmo{ &ModeTools };
	FTransform DragStartBoneTransform;
	FMatrix DragParentInverse;
	FQuat DragParentRotation;

	int32 ClothSectionIndex = INDEX_NONE;
	TArray<uint32> ClothSectionVertices;
	TSet<uint32> ClothPinnedVertices;
	TMap<uint32, float> ClothMaxDistances;
	float ClothBrushRadius = 20.0f;
	float ClothBrushMaxDistance = 0.0f;
	bool bClothPaintMode = false;
	bool bClothStrokeActive = false;
	bool bPreviousClothErase = false;
	FVector2 PreviousClothMouseUV;
};
