#pragma once

#include "Editor/BlueprintEditor/BlueprintEditorTab.h"
#include "Editor/BlueprintEditor/BlueprintEditor.h"
#include "imgui_node_editor.h"

#include <unordered_set>

// UE5 Blueprint 에디터의 EventGraph / Construction Script 탭. imgui-node-editor로 노드 그래프를 그리고 고친다.
//   - 빈 곳 우클릭, 또는 핀에서 선을 끌어 빈 곳에 놓으면 노드 메뉴(All Actions)가 열린다.
//     핀에서 열었으면 새 노드의 맞는 핀에 바로 잇는다.
//   - 핀끼리 끌어 잇고, 선·노드는 Delete나 우클릭 메뉴로 지운다.
//   - 연결하지 않은 입력 핀 값은 노드 위에서 바로 고친다.
//   - My Blueprint의 컴포넌트 변수를 끌어다 놓으면 그 컴포넌트를 가리키는 노드가 생긴다.
class FBlueprintGraphPanel : public FBlueprintEditorPanel
{
public:
	FBlueprintGraphPanel(FBlueprintEditor& InEditor, EBlueprintGraphKind InKind, const char* InName);
	~FBlueprintGraphPanel() override;

	void OnRender() override;

protected:
	void DrawContent() override;

private:
	void DrawNode(FBPGraph& Graph, FBPNode& Node);
	void DrawPinRow(FBPGraph& Graph, FBPPin& Pin);
	bool DrawPinValueEditor(FBPPin& Pin);
	void DrawLinks(const FBPGraph& Graph);
	void HandleCreate(FBPGraph& Graph);
	void HandleDelete(FBPGraph& Graph);
	void HandleComponentDrop(FBPGraph& Graph, const ImVec2& CanvasMin, const ImVec2& CanvasMax);
	void DrawPopups(FBPGraph& Graph);
	void DrawCreateMenu(FBPGraph& Graph);
	void SpawnNode(FBPGraph& Graph, const FString& Type, const FString& Param = FString());
	void SyncNodePositions(FBPGraph& Graph);

	EBlueprintGraphKind Kind;
	ax::NodeEditor::EditorContext* Context = nullptr;
	std::unordered_set<int32> PositionedNodes;
	int32 FramesDrawn = 0;
	int32 PendingFocusNode = 0;

	// 노드 만들기 메뉴
	bool bOpenCreateMenu = false;
	bool bFocusCreateSearch = false;
	ImVec2 CreatePosition = ImVec2(0.0f, 0.0f);
	int32 CreateFromPin = 0;   // 핀에서 끌어 연 메뉴면 그 핀
	char CreateSearch[128] = {};

	// 노드 / 핀 / 선 우클릭 메뉴 대상
	int32 ContextNodeId = 0;
	int32 ContextPinId = 0;
	int32 ContextLinkId = 0;
};
