#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "imgui_node_editor.h"
#include "Containers/Array.h"

class FPhysicsAssetGraphPanel : public TAssetEditorPanel<FPhysicsAssetEditor>
{
public:
	explicit FPhysicsAssetGraphPanel(FPhysicsAssetEditor& InEditor)
		: TAssetEditorPanel<FPhysicsAssetEditor>(InEditor, "Graph", "PhysicsAssetPanel")
	{
	}
	~FPhysicsAssetGraphPanel() override;

protected:
	void DrawContent() override;

private:
	struct FGraphNode
	{
		int32 NodeId = 0;
		EPhysicsAssetSelection Type = EPhysicsAssetSelection::None;
		int32 Index = INDEX_NONE;
	};

	void SyncRootWithEditorSelection();
	void SetRootBody(int32 BodyIndex);
	bool IsConstraintInGraph(int32 ConstraintIndex) const;

	void DrawBodyNode(int32 NodeId, int32 BodyIndex, int32 InputPinId, int32 OutputPinId, const ImVec2& Position);
	void DrawConstraintNode(int32 NodeId, int32 ConstraintIndex, int32 InputPinId, int32 OutputPinId, const ImVec2& Position);


	FString GetBoneDisplayName(FName BoneName) const;
	FString GetBodyName(int32 BodyIndex) const;
	FString GetShapesLabel(int32 BodyIndex) const;
	FString GetConstraintLabel(int32 ConstraintIndex) const;

	void PushEditorSelectionToGraph();
	void PullGraphSelectionToEditor();
	const FGraphNode* FindNode(int32 NodeId) const;
	int32 FindNodeId(EPhysicsAssetSelection Type, int32 Index) const;

	ax::NodeEditor::EditorContext* Context = nullptr;
	TArray<FGraphNode> Nodes;
	int32 RootBody = INDEX_NONE;
	bool bLayoutDirty = true;

	EPhysicsAssetSelection SyncedType = EPhysicsAssetSelection::None;
	int32 SyncedIndex = INDEX_NONE;
	int32 SyncedGraphNodeId = 0;
	bool bPushSelection = true;
};
