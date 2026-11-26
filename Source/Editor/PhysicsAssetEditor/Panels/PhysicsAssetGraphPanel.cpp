#include "EnginePCH.h"
#include "PhysicsAssetGraphPanel.h"
#include "Physics/PhysicsAsset.h"

namespace NodeEditor = ax::NodeEditor;

namespace
{
	const ImVec4 BodyNodeColor(0.25f, 0.45f, 0.20f, 1.0f);
	const ImVec4 ConstraintNodeColor(0.45f, 0.42f, 0.20f, 1.0f);
	const ImVec4 LinkColor(0.85f, 0.85f, 0.85f, 1.0f);

	constexpr float ColumnGap = 60.0f;
	constexpr float RowGap = 20.0f;

	ImVec2 EstimateNodeSize(std::initializer_list<FString> Lines, int32 PinCount)
	{
		float TextWidth = 0.0f;
		for (const FString& Line : Lines)
		{
			TextWidth = std::max(TextWidth, ImGui::CalcTextSize(Line.c_str()).x);
		}
		const ImGuiStyle& Style = ImGui::GetStyle();
		const float PinWidth = ImGui::CalcTextSize("o").x + Style.ItemSpacing.x;
		const ImVec4& Padding = NodeEditor::GetStyle().NodePadding;
		const float TextHeight = ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(Lines.size()) - Style.ItemSpacing.y;
		return ImVec2(TextWidth + PinWidth * PinCount + Padding.x + Padding.z,
			TextHeight + Padding.y + Padding.w);
	}

	// 노드·핀·링크 ID: 루트는 고정, Constraint마다 10칸
	constexpr int32 RootNodeId = 1;
	constexpr int32 RootOutputPinId = 2;
	constexpr int32 ConstraintIdBase = 100;
	constexpr int32 ConstraintIdStride = 10;
	enum EConstraintSlot : int32
	{
		Slot_ConstraintNode = 0,
		Slot_ConstraintInPin,
		Slot_ConstraintOutPin,
		Slot_OtherBodyNode,
		Slot_OtherBodyInPin,
		Slot_LinkToConstraint,
		Slot_LinkToOtherBody,
	};

	int32 ConstraintSlotId(int32 ConstraintIndex, EConstraintSlot Slot)
	{
		return ConstraintIdBase + ConstraintIndex * ConstraintIdStride + Slot;
	}

	bool IsConstraintOnBone(const FConstraintInstance& Constraint, FName BoneName)
	{
		return Constraint.ConstraintBone1 == BoneName || Constraint.ConstraintBone2 == BoneName;
	}
}

FPhysicsAssetGraphPanel::~FPhysicsAssetGraphPanel()
{
	if (Context)
	{
		NodeEditor::DestroyEditor(Context);
	}
}

void FPhysicsAssetGraphPanel::DrawContent()
{
	UPhysicsAsset* PhysicsAsset = Editor.GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		return;
	}

	SyncRootWithEditorSelection();
	if (RootBody < 0 || RootBody >= PhysicsAsset->SkeletalBodySetups.Num())
	{
		RootBody = INDEX_NONE;
		ImGui::TextDisabled("Select a body.");
		return;
	}

	if (!Context)
	{
		NodeEditor::Config Config;
		Config.SettingsFile = nullptr;
		Context = NodeEditor::CreateEditor(&Config);
	}

	NodeEditor::EditorContext* PreviousContext = NodeEditor::GetCurrentEditor();
	NodeEditor::SetCurrentEditor(Context);
	NodeEditor::Begin("PhysicsAssetGraph");

	Nodes.Reset();
	const FName RootBoneName = PhysicsAsset->SkeletalBodySetups[RootBody]->BoneName;

	struct FRow { int32 ConstraintIndex; int32 OtherBody; };
	TArray<FRow> Rows;
	for (int32 ConstraintIndex = 0; ConstraintIndex < PhysicsAsset->ConstraintSetup.Num(); ++ConstraintIndex)
	{
		const UPhysicsConstraintTemplate* Template = PhysicsAsset->ConstraintSetup[ConstraintIndex];
		if (!Template || !IsConstraintOnBone(Template->DefaultInstance, RootBoneName))
		{
			continue;
		}
		const FConstraintInstance& Constraint = Template->DefaultInstance;
		const FName OtherBoneName = Constraint.ConstraintBone1 == RootBoneName ? Constraint.ConstraintBone2 : Constraint.ConstraintBone1;
		Rows.Add({ ConstraintIndex, PhysicsAsset->FindBodyIndex(OtherBoneName) });
	}

	// 긴 이름의 노드가 다음 열과 겹치지 않도록 글자 크기로 간격을 정한다
	const ImVec2 RootSize = EstimateNodeSize({ "Body", GetBodyName(RootBody), GetShapesLabel(RootBody) }, 1);
	float ConstraintColumnWidth = 0.0f;
	float RowHeight = RootSize.y;
	for (const FRow& Row : Rows)
	{
		const ImVec2 Size = EstimateNodeSize({ "Constraint", GetConstraintLabel(Row.ConstraintIndex) }, 2);
		ConstraintColumnWidth = std::max(ConstraintColumnWidth, Size.x);
		RowHeight = std::max(RowHeight, Size.y);
		if (Row.OtherBody != INDEX_NONE)
		{
			RowHeight = std::max(RowHeight, EstimateNodeSize({ "Body", GetBodyName(Row.OtherBody), GetShapesLabel(Row.OtherBody) }, 1).y);
		}
	}
	const float ConstraintColumnX = RootSize.x + ColumnGap;
	const float OtherBodyColumnX = ConstraintColumnX + ConstraintColumnWidth + ColumnGap;
	const float RowSpacing = RowHeight + RowGap;

	DrawBodyNode(RootNodeId, RootBody, 0, RootOutputPinId, ImVec2(0.0f, 0.0f));

	for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
	{
		const int32 ConstraintIndex = Rows[RowIndex].ConstraintIndex;
		const int32 OtherBody = Rows[RowIndex].OtherBody;
		const float Y = (RowIndex - (Rows.Num() - 1) * 0.5f) * RowSpacing;

		DrawConstraintNode(ConstraintSlotId(ConstraintIndex, Slot_ConstraintNode), ConstraintIndex,
			ConstraintSlotId(ConstraintIndex, Slot_ConstraintInPin), ConstraintSlotId(ConstraintIndex, Slot_ConstraintOutPin),
			ImVec2(ConstraintColumnX, Y));
		NodeEditor::Link(ConstraintSlotId(ConstraintIndex, Slot_LinkToConstraint), RootOutputPinId,
			ConstraintSlotId(ConstraintIndex, Slot_ConstraintInPin), LinkColor);

		if (OtherBody != INDEX_NONE)
		{
			DrawBodyNode(ConstraintSlotId(ConstraintIndex, Slot_OtherBodyNode), OtherBody,
				ConstraintSlotId(ConstraintIndex, Slot_OtherBodyInPin), 0, ImVec2(OtherBodyColumnX, Y));
			NodeEditor::Link(ConstraintSlotId(ConstraintIndex, Slot_LinkToOtherBody), ConstraintSlotId(ConstraintIndex, Slot_ConstraintOutPin),
				ConstraintSlotId(ConstraintIndex, Slot_OtherBodyInPin), LinkColor);
		}
	}

	PushEditorSelectionToGraph();
	const NodeEditor::NodeId DoubleClickedNode = NodeEditor::GetDoubleClickedNode();
	NodeEditor::End();

	if (bLayoutDirty)
	{
		NodeEditor::NavigateToContent(0.0f);
		bLayoutDirty = false;
	}

	PullGraphSelectionToEditor();
	NodeEditor::SetCurrentEditor(PreviousContext);

	if (DoubleClickedNode)
	{
		const FGraphNode* Node = FindNode(static_cast<int32>(DoubleClickedNode.Get()));
		if (Node && Node->Type == EPhysicsAssetSelection::Body)
		{
			SetRootBody(Node->Index);
		}
	}
}

void FPhysicsAssetGraphPanel::SyncRootWithEditorSelection()
{
	const EPhysicsAssetSelection Type = Editor.GetSelectionType();
	const int32 Index = Type == EPhysicsAssetSelection::Body ? Editor.GetSelectedBodyIndex() : Editor.GetSelectedConstraintIndex();
	if (Type == SyncedType && Index == SyncedIndex)
	{
		return;
	}

	SyncedType = Type;
	SyncedIndex = Index;
	bPushSelection = true;

	if (Type == EPhysicsAssetSelection::Body)
	{
		SetRootBody(Index);
	}
	else if (Type == EPhysicsAssetSelection::Constraint && !IsConstraintInGraph(Index))
	{
		const UPhysicsAsset* PhysicsAsset = Editor.GetPhysicsAsset();
		const FConstraintInstance& Constraint = PhysicsAsset->ConstraintSetup[Index]->DefaultInstance;
		int32 Body = PhysicsAsset->FindBodyIndex(Constraint.ConstraintBone1);
		if (Body == INDEX_NONE)
		{
			Body = PhysicsAsset->FindBodyIndex(Constraint.ConstraintBone2);
		}
		SetRootBody(Body);
	}
}

void FPhysicsAssetGraphPanel::SetRootBody(int32 BodyIndex)
{
	if (BodyIndex == RootBody)
	{
		return;
	}
	RootBody = BodyIndex;
	bLayoutDirty = true;
	bPushSelection = true;
}

bool FPhysicsAssetGraphPanel::IsConstraintInGraph(int32 ConstraintIndex) const
{
	const UPhysicsAsset* PhysicsAsset = Editor.GetPhysicsAsset();
	if (RootBody < 0 || RootBody >= PhysicsAsset->SkeletalBodySetups.Num())
	{
		return false;
	}
	return IsConstraintOnBone(PhysicsAsset->ConstraintSetup[ConstraintIndex]->DefaultInstance,
		PhysicsAsset->SkeletalBodySetups[RootBody]->BoneName);
}

void FPhysicsAssetGraphPanel::DrawBodyNode(int32 NodeId, int32 BodyIndex, int32 InputPinId, int32 OutputPinId, const ImVec2& Position)
{
	Nodes.Add({ NodeId, EPhysicsAssetSelection::Body, BodyIndex });
	if (bLayoutDirty)
	{
		NodeEditor::SetNodePosition(NodeId, Position);
	}

	NodeEditor::PushStyleColor(NodeEditor::StyleColor_NodeBg, BodyNodeColor);
	NodeEditor::BeginNode(NodeId);
	if (InputPinId)
	{
		NodeEditor::BeginPin(InputPinId, NodeEditor::PinKind::Input);
		ImGui::TextUnformatted("o");
		NodeEditor::EndPin();
		ImGui::SameLine();
	}
	ImGui::BeginGroup();
	ImGui::TextUnformatted("Body");
	ImGui::TextUnformatted(GetBodyName(BodyIndex).c_str());
	ImGui::TextUnformatted(GetShapesLabel(BodyIndex).c_str());
	ImGui::EndGroup();
	if (OutputPinId)
	{
		ImGui::SameLine();
		NodeEditor::BeginPin(OutputPinId, NodeEditor::PinKind::Output);
		ImGui::TextUnformatted("o");
		NodeEditor::EndPin();
	}
	NodeEditor::EndNode();
	NodeEditor::PopStyleColor();
}

void FPhysicsAssetGraphPanel::DrawConstraintNode(int32 NodeId, int32 ConstraintIndex, int32 InputPinId, int32 OutputPinId, const ImVec2& Position)
{
	Nodes.Add({ NodeId, EPhysicsAssetSelection::Constraint, ConstraintIndex });
	if (bLayoutDirty)
	{
		NodeEditor::SetNodePosition(NodeId, Position);
	}

	NodeEditor::PushStyleColor(NodeEditor::StyleColor_NodeBg, ConstraintNodeColor);
	NodeEditor::BeginNode(NodeId);
	NodeEditor::BeginPin(InputPinId, NodeEditor::PinKind::Input);
	ImGui::TextUnformatted("o");
	NodeEditor::EndPin();
	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::TextUnformatted("Constraint");
	ImGui::TextUnformatted(GetConstraintLabel(ConstraintIndex).c_str());
	ImGui::EndGroup();
	ImGui::SameLine();
	NodeEditor::BeginPin(OutputPinId, NodeEditor::PinKind::Output);
	ImGui::TextUnformatted("o");
	NodeEditor::EndPin();
	NodeEditor::EndNode();
	NodeEditor::PopStyleColor();
}

void FPhysicsAssetGraphPanel::PushEditorSelectionToGraph()
{
	if (!bPushSelection)
	{
		return;
	}
	bPushSelection = false;

	const int32 NodeId = FindNodeId(SyncedType, SyncedIndex);
	if (NodeId)
	{
		NodeEditor::SelectNode(NodeId);
	}
	else
	{
		NodeEditor::ClearSelection();
	}
	SyncedGraphNodeId = NodeId;
}

void FPhysicsAssetGraphPanel::PullGraphSelectionToEditor()
{
	NodeEditor::NodeId SelectedNode;
	const int32 Count = NodeEditor::GetSelectedNodes(&SelectedNode, 1);
	const int32 NodeId = Count > 0 ? static_cast<int32>(SelectedNode.Get()) : 0;
	if (NodeId == SyncedGraphNodeId)
	{
		return;
	}
	SyncedGraphNodeId = NodeId;

	// 클릭은 선택만 바꾸고 루트는 유지한다
	const FGraphNode* Node = FindNode(NodeId);
	if (!Node)
	{
		return;
	}
	if (Node->Type == EPhysicsAssetSelection::Body)
	{
		Editor.SelectBody(Node->Index);
	}
	else
	{
		Editor.SelectConstraint(Node->Index);
	}
	SyncedType = Node->Type;
	SyncedIndex = Node->Index;
}

const FPhysicsAssetGraphPanel::FGraphNode* FPhysicsAssetGraphPanel::FindNode(int32 NodeId) const
{
	for (const FGraphNode& Node : Nodes)
	{
		if (Node.NodeId == NodeId)
		{
			return &Node;
		}
	}
	return nullptr;
}

int32 FPhysicsAssetGraphPanel::FindNodeId(EPhysicsAssetSelection Type, int32 Index) const
{
	for (const FGraphNode& Node : Nodes)
	{
		if (Node.Type == Type && Node.Index == Index)
		{
			return Node.NodeId;
		}
	}
	return 0;
}

FString FPhysicsAssetGraphPanel::GetBoneDisplayName(FName BoneName) const
{
	const int32 BoneIndex = Editor.FindBoneIndexByName(BoneName);
	return BoneIndex >= 0 ? Editor.GetBoneItems()[BoneIndex].Name : FString("?");
}

FString FPhysicsAssetGraphPanel::GetBodyName(int32 BodyIndex) const
{
	return GetBoneDisplayName(Editor.GetPhysicsAsset()->SkeletalBodySetups[BodyIndex]->BoneName);
}

FString FPhysicsAssetGraphPanel::GetShapesLabel(int32 BodyIndex) const
{
	return "Shapes: " + std::to_string(Editor.GetPhysicsAsset()->SkeletalBodySetups[BodyIndex]->AggGeom.GetElementCount());
}

FString FPhysicsAssetGraphPanel::GetConstraintLabel(int32 ConstraintIndex) const
{
	const FConstraintInstance& Constraint = Editor.GetPhysicsAsset()->ConstraintSetup[ConstraintIndex]->DefaultInstance;
	return GetBoneDisplayName(Constraint.ConstraintBone1) + " : " + GetBoneDisplayName(Constraint.ConstraintBone2);
}
