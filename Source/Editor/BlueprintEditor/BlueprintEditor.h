#pragma once

#include "Editor/AssetEditor/AssetEditor.h"
#include "Editor/EditorModeManager.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Camera/ViewportCameraMovement.h"
#include "Containers/Array.h"
#include "Engine/BlueprintGraph.h"

class UClass;

enum class EBlueprintGraphKind : uint8
{
	EventGraph,
	ConstructionScript,
};
class AActor;
class UActorComponent;
class USceneComponent;

// 뷰포트 패널이 넘기는 마우스 상태. 분리된 창에서도 맞도록 이미지 크기에 대한 비율로 둔다.
struct FBlueprintViewportPointer
{
	FVector2 MouseUV = FVector2(0.0f, 0.0f);
	bool bHovered = false;
	bool bMousePressed = false;   // 이번 프레임에 왼쪽 버튼을 눌렀다
	bool bMouseDown = false;
	bool bCancel = false;         // Esc: 끌던 기즈모를 원래대로
};

// 데이터 Blueprint 에디터. UE의 Blueprint 에디터에서 Components / Viewport / Details 부분에 해당한다.
// 미리보기 월드에 BP 클래스로 액터(템플릿)를 하나 만들고, 그 액터를 고친 뒤 Save하면
// 템플릿의 지금 상태가 BP 기본값이 된다 (FBlueprint::SaveBlueprint).
//
// 컴포넌트 BP(부모가 UActorComponent 계열)는 빈 액터에 BP 컴포넌트를 하나 붙여 그 컴포넌트를 템플릿으로 쓴다.
// 이때는 UE5의 Component Blueprint 에디터처럼 Details와 Viewport만 보이고, 노드 그래프는 없다.
class FBlueprintEditor : public FAssetEditor
{
public:
	FBlueprintEditor(class UEditorEngine& InEditorEngine, class FEditorManager& InEditorManager, const FString& InAssetPath);

	void Tick(float DeltaTime) override;
	void OnRenderPreview() override;

	const char* GetEditorTypeName() const override { return "Blueprint"; }
	bool IsDirty() const override { return bDirty; }
	bool ConfirmClose() override;

	// 템플릿 상태를 .bp에 쓴다. 실패하면 로그와 메시지 창을 띄운다.
	bool Save();

	// 템플릿 액터 전체가 화면에 들어오게 카메라를 옮긴다.
	void FocusTemplate();
	// 뷰포트 패널이 매 프레임 크기와 카메라·마우스 입력을 넘긴다. 보이지 않으면 0이다.
	void SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input,
		const FBlueprintViewportPointer& Pointer = {});

	// ---------------- 뷰포트 기즈모 ----------------
	// 고른 컴포넌트를 기즈모로 옮긴다. 루트(Self)는 옮기지 않는다: 스폰할 때 루트 Transform은 스폰 위치로 바뀌기 때문이다.
	// 뷰포트를 클릭하면 그 아래 컴포넌트를 고른다. W/E/R과 Space로 이동/회전/크기를 바꾼다.
	const FGizmo* GetPreviewGizmo() const override { return Gizmo.GetTarget() ? &Gizmo : nullptr; }
	UE::Widget::EWidgetMode GetGizmoMode() const { return ModeTools.GetWidgetMode(); }
	void SetGizmoMode(UE::Widget::EWidgetMode Mode);
	bool IsGizmoDragging() const { return Gizmo.IsUsing(); }

	UClass* GetBlueprintClass() const { return BlueprintClass; }
	AActor* GetTemplate() const { return Template; }
	// 컴포넌트 BP면 편집 중인 BP 컴포넌트, 아니면 nullptr.
	UActorComponent* GetComponentTemplate() const { return ComponentTemplate; }
	bool IsComponentBlueprint() const { return ComponentTemplate != nullptr; }

	// 고르고 있는 컴포넌트. nullptr이면 액터 자신(Self)이다. 컴포넌트 BP는 늘 BP 컴포넌트를 고른다.
	UActorComponent* GetSelectedComponent() const { return SelectedComponent; }
	void SelectComponent(UActorComponent* Component)
	{
		SelectedComponent = ComponentTemplate ? ComponentTemplate : Component;
		bShowClassSettings = false;
	}
	// 툴바 [Class Settings]: Details에 부모 클래스 같은 BP 자체 설정을 보인다. 컴포넌트를 고르면 풀린다.
	bool IsShowingClassSettings() const { return bShowClassSettings; }
	void ShowClassSettings() { SelectedComponent = ComponentTemplate; bShowClassSettings = true; }
	// 이름을 바꾼다. 빈 이름이거나 같은 이름의 컴포넌트가 이미 있으면 바꾸지 않는다.
	bool RenameComponent(UActorComponent* Component, const char* NewName);
	// 컴포넌트를 지운다. 루트는 지울 수 없다. 패널을 다 그린 뒤 부른다.
	void DeleteComponent(UActorComponent* Component);

	// 패널이 값을 바꿨을 수 있을 때 부른다. 다음 Tick에서 저장된 상태와 비교해 바뀌었는지 가린다.
	void NoteEdited() { bCheckDirty = true; }

	// ---------------- 노드 그래프 ----------------
	// 편집 중인 EventGraph / Construction Script. Save하면 .bp에 함께 쓴다.
	FBPGraphSet& GetGraphs() { return Graphs; }
	FBPGraph& GetGraph(EBlueprintGraphKind Kind) { return Kind == EBlueprintGraphKind::EventGraph ? Graphs.EventGraph : Graphs.ConstructionScript; }
	// My Blueprint에서 그래프(와 노드)를 고르면 그 그래프 탭을 앞으로 가져오고 노드로 화면을 옮긴다.
	void RequestOpenGraph(EBlueprintGraphKind Kind, int32 NodeId = 0) { PendingGraph = Kind; PendingNodeId = NodeId; bGraphOpenPending = true; }
	bool ConsumeOpenGraphRequest(EBlueprintGraphKind Kind, int32& OutNodeId)
	{
		if (!bGraphOpenPending || PendingGraph != Kind)
			return false;
		bGraphOpenPending = false;
		OutNodeId = PendingNodeId;
		return true;
	}

protected:
	TUniquePtr<class IEditorTab> CreateTab() override;
	bool InitializeResources() override;
	void ShutdownResources() override;

private:
	bool SerializeTemplate(TArray<uint8>& OutBytes) const;

	// 기즈모 대상을 지금 고른 컴포넌트에 맞춘다.
	void SyncGizmoTarget();
	// 기즈모를 끌거나, 기즈모 밖을 클릭하면 그 아래 컴포넌트를 고른다.
	void UpdateGizmoAndPicking();

	UClass* BlueprintClass = nullptr;
	AActor* Template = nullptr;
	UActorComponent* ComponentTemplate = nullptr;

	// 이 에디터의 편집 모드와 좌표계. 레벨 에디터와 따로 갖는다. Gizmo보다 먼저 선언해야 한다.
	FEditorModeTools ModeTools;
	FGizmo Gizmo{ &ModeTools };
	FBlueprintViewportPointer Pointer;
	FTransform DragStartTransform;
	UActorComponent* SelectedComponent = nullptr;
	bool bShowClassSettings = false;

	FBPGraphSet Graphs;
	EBlueprintGraphKind PendingGraph = EBlueprintGraphKind::EventGraph;
	int32 PendingNodeId = 0;
	bool bGraphOpenPending = false;

	// 마지막으로 저장한(또는 연) 상태. 지금 상태와 비교해 * 표시를 정한다.
	TArray<uint8> SavedBytes;
	bool bDirty = false;
	bool bCheckDirty = false;

	FViewportCameraInput CameraInput;
	uint32 ViewportWidth = 0;
	uint32 ViewportHeight = 0;
	bool bViewportVisible = false;
	bool bInitialFocusPending = true;
};
