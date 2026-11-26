#include "EnginePCH.h"
#include "Editor/BlueprintEditor/BlueprintGraphPanel.h"

#include <cstring>

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "GameFramework/Actor.h"

namespace NodeEditor = ax::NodeEditor;

namespace
{
	// My Blueprint에서 컴포넌트 변수를 끌 때 쓰는 드래그 종류. 내용은 컴포넌트 이름 (끝에 0).
	constexpr const char* ComponentVariablePayload = "BP_COMPONENT_VARIABLE";

	ImVec4 PinColor(EBPPinType Type)
	{
		switch (Type)
		{
		case EBPPinType::Exec:   return ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
		case EBPPinType::Bool:   return ImVec4(0.57f, 0.00f, 0.00f, 1.0f);
		case EBPPinType::Float:  return ImVec4(0.62f, 0.98f, 0.27f, 1.0f);
		case EBPPinType::String: return ImVec4(0.98f, 0.00f, 0.83f, 1.0f);
		case EBPPinType::Vector: return ImVec4(1.00f, 0.78f, 0.04f, 1.0f);
		case EBPPinType::Object: return ImVec4(0.00f, 0.66f, 0.94f, 1.0f);
		}
		return ImVec4(1, 1, 1, 1);
	}

	ImU32 HeaderColor(const FBPNode& Node, const FBPNodeDef* Def)
	{
		if (Node.Type == "GetComponent")
			return IM_COL32(20, 70, 110, 220);
		if (!Def)
			return IM_COL32(80, 80, 80, 220);
		switch (Def->Kind)
		{
		case EBPNodeKind::Event: return IM_COL32(140, 24, 24, 230);
		case EBPNodeKind::Pure:  return IM_COL32(50, 110, 50, 220);
		default:                 return Def->Category == "Flow Control" ? IM_COL32(90, 90, 90, 230) : IM_COL32(30, 80, 140, 230);
		}
	}

	// UE의 핀 모양: 실행 핀은 오각 화살표, 데이터 핀은 원. 이어져 있으면 채운다.
	void DrawPinIcon(EBPPinType Type, bool bLinked)
	{
		const float Size = ImGui::GetTextLineHeight();
		ImGui::Dummy(ImVec2(Size, Size));
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Center(Min.x + Size * 0.5f, Min.y + Size * 0.5f);
		const ImU32 Color = ImGui::ColorConvertFloat4ToU32(PinColor(Type));
		ImDrawList* DL = ImGui::GetWindowDrawList();
		if (Type == EBPPinType::Exec)
		{
			const float H = Size * 0.36f;
			const ImVec2 Points[5] = {
				ImVec2(Center.x - H, Center.y - H), ImVec2(Center.x + H * 0.2f, Center.y - H), ImVec2(Center.x + H, Center.y),
				ImVec2(Center.x + H * 0.2f, Center.y + H), ImVec2(Center.x - H, Center.y + H) };
			if (bLinked)
				DL->AddConvexPolyFilled(Points, 5, Color);
			else
				DL->AddPolyline(Points, 5, Color, ImDrawFlags_Closed, 1.5f);
		}
		else
		{
			const float R = Size * 0.3f;
			if (bLinked)
				DL->AddCircleFilled(Center, R, Color);
			else
				DL->AddCircle(Center, R, Color, 0, 1.8f);
		}
	}

	// 실행 핀 In / Then은 이름 없이 그린다 (UE와 같다).
	bool ShowsPinLabel(const FBPPin& Pin, const FBPNode& Node)
	{
		if (Node.Type == "GetComponent")
			return false;
		if (Pin.Type == EBPPinType::Exec && (Pin.Name == "In" || Pin.Name == "Then"))
			return false;
		return Pin.Name != "Return Value";
	}

	bool PinsCompatible(const FBPPinDef& Candidate, const FBPPin& From)
	{
		return Candidate.Type == From.Type && Candidate.bOutput != From.bOutput;
	}

	bool AllowedInGraph(const FBPNodeDef& Def, EBlueprintGraphKind Kind)
	{
		if (Kind == EBlueprintGraphKind::EventGraph && Def.bConstructionScriptOnly)
			return false;
		if (Kind == EBlueprintGraphKind::ConstructionScript && Def.bEventGraphOnly)
			return false;
		return true;
	}

	bool GraphHasType(const FBPGraph& Graph, const FString& Type)
	{
		for (const FBPNode& Node : Graph.Nodes)
			if (Node.Type == Type)
				return true;
		return false;
	}
}

FBlueprintGraphPanel::FBlueprintGraphPanel(FBlueprintEditor& InEditor, EBlueprintGraphKind InKind, const char* InName)
	: FBlueprintEditorPanel(InEditor, InName)
	, Kind(InKind)
{
	NodeEditor::Config Config;
	Config.SettingsFile = nullptr;   // 화면 위치는 저장하지 않는다. 노드 위치는 그래프 데이터에 있다
	Context = NodeEditor::CreateEditor(&Config);
}

FBlueprintGraphPanel::~FBlueprintGraphPanel()
{
	if (Context)
		NodeEditor::DestroyEditor(Context);
}

void FBlueprintGraphPanel::OnRender()
{
	int32 NodeId = 0;
	if (Editor.ConsumeOpenGraphRequest(Kind, NodeId))
	{
		SetOpen(true);
		ImGui::SetNextWindowFocus();
		PendingFocusNode = NodeId;
	}
	FBlueprintEditorPanel::OnRender();
}

void FBlueprintGraphPanel::DrawContent()
{
	if (!Context)
		return;
	FBPGraph& Graph = Editor.GetGraph(Kind);

	const ImVec2 CanvasMin = ImGui::GetCursorScreenPos();
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	if (Avail.x < 1.0f || Avail.y < 1.0f)
		return;
	const ImVec2 CanvasMax(CanvasMin.x + Avail.x, CanvasMin.y + Avail.y);

	NodeEditor::EditorContext* Previous = NodeEditor::GetCurrentEditor();
	NodeEditor::SetCurrentEditor(Context);
	NodeEditor::Begin("BlueprintGraphCanvas");

	for (FBPNode& Node : Graph.Nodes)
		DrawNode(Graph, Node);
	DrawLinks(Graph);
	HandleCreate(Graph);
	HandleDelete(Graph);
	HandleComponentDrop(Graph, CanvasMin, CanvasMax);
	DrawPopups(Graph);

	// 처음 열었을 때 노드가 다 보이게. 크기는 한 번 그린 뒤에 알 수 있어 다음 프레임에 맞춘다.
	if (FramesDrawn == 1)
		NodeEditor::NavigateToContent(0.0f);
	if (PendingFocusNode != 0 && FramesDrawn >= 1 && Graph.FindNode(PendingFocusNode))
	{
		NodeEditor::ClearSelection();
		NodeEditor::SelectNode(NodeEditor::NodeId(PendingFocusNode));
		NodeEditor::NavigateToSelection(false, 0.25f);
		PendingFocusNode = 0;
	}

	NodeEditor::End();
	SyncNodePositions(Graph);
	NodeEditor::SetCurrentEditor(Previous);
	++FramesDrawn;

	// 노드 위 값을 고쳤거나 노드를 옮기고 놓았으면 저장 표시(*)를 다시 계산한다.
	const bool bHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	if (ImGui::GetCurrentContext()->ActiveIdHasBeenEditedThisFrame || (bHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left)))
		Editor.NoteEdited();
}

void FBlueprintGraphPanel::DrawNode(FBPGraph& Graph, FBPNode& Node)
{
	const FBPNodeDef* Def = FBlueprintNodeLibrary::Find(Node.Type);
	const NodeEditor::NodeId Id(Node.Id);
	if (PositionedNodes.insert(Node.Id).second)
		NodeEditor::SetNodePosition(Id, ImVec2(Node.X, Node.Y));

	NodeEditor::PushStyleVar(NodeEditor::StyleVar_NodePadding, ImVec4(8.0f, 4.0f, 8.0f, 8.0f));
	NodeEditor::PushStyleVar(NodeEditor::StyleVar_NodeRounding, 6.0f);
	NodeEditor::BeginNode(Id);
	ImGui::PushID(Node.Id);

	// 머리줄: 제목. 색 띠는 EndNode 뒤에 노드 뒤쪽에 칠한다.
	const FString Title = FBlueprintNodeLibrary::GetTitle(Node);
	const float NodeLeft = ImGui::GetCursorScreenPos().x;
	ImGui::Dummy(ImVec2(0.0f, 2.0f));
	ImGui::BeginGroup();
	ImGui::TextUnformatted(Title.c_str());
	// C++ 함수 노드는 UE처럼 "Target is Character"를 제목 아래에 작게 쓴다.
	const FString Subtitle = FBlueprintNodeLibrary::GetSubtitle(Node);
	if (!Subtitle.empty())
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.55f), "%s", Subtitle.c_str());
	ImGui::EndGroup();
	const float TitleWidth = ImGui::GetItemRectSize().x;
	const float HeaderBottom = ImGui::GetItemRectMax().y + 4.0f;
	ImGui::Dummy(ImVec2(0.0f, 4.0f));
	const float BodyTop = ImGui::GetCursorScreenPos().y;

	// 왼쪽 열: 입력 핀
	bool bHasInputs = false;
	for (const FBPPin& Pin : Node.Pins)
		bHasInputs |= !Pin.bOutput;
	float InputsWidth = 0.0f;
	if (bHasInputs)
	{
		ImGui::BeginGroup();
		for (FBPPin& Pin : Node.Pins)
			if (!Pin.bOutput)
				DrawPinRow(Graph, Pin);
		ImGui::EndGroup();
		InputsWidth = ImGui::GetItemRectSize().x;
	}

	// 오른쪽 열: 출력 핀. UE처럼 노드 오른쪽 끝에 맞춘다.
	// 줄마다 폭이 다르므로 (이름 + 핀 아이콘) 가장 넓은 줄에 맞춰 각 줄을 오른쪽 정렬한다.
	const float IconSize = ImGui::GetTextLineHeight();
	auto OutputRowWidth = [&](const FBPPin& Pin)
	{
		return ShowsPinLabel(Pin, Node) ? ImGui::CalcTextSize(Pin.Name.c_str()).x + 4.0f + IconSize : IconSize;
	};
	float OutputsWidth = 0.0f;
	for (const FBPPin& Pin : Node.Pins)
		if (Pin.bOutput)
			OutputsWidth = ImMax(OutputsWidth, OutputRowWidth(Pin));
	if (OutputsWidth > 0.0f)
	{
		// 입력 열 오른쪽 24px 뒤, 또는 제목이 더 넓으면 제목 오른쪽 끝에 맞춘다.
		const float OutputsLeft = NodeLeft + ImMax(bHasInputs ? InputsWidth + 24.0f : 0.0f, TitleWidth - OutputsWidth);
		ImGui::SetCursorScreenPos(ImVec2(OutputsLeft, BodyTop));
		ImGui::BeginGroup();
		for (FBPPin& Pin : Node.Pins)
		{
			if (!Pin.bOutput)
				continue;
			ImGui::SetCursorScreenPos(ImVec2(OutputsLeft + OutputsWidth - OutputRowWidth(Pin), ImGui::GetCursorScreenPos().y));
			DrawPinRow(Graph, Pin);
		}
		ImGui::EndGroup();
	}

	ImGui::PopID();
	NodeEditor::EndNode();
	NodeEditor::PopStyleVar(2);

	// UE처럼 종류별 색 머리줄
	if (ImDrawList* Background = NodeEditor::GetNodeBackgroundDrawList(Id))
	{
		// 노드 그리기 중의 좌표와 같은 캔버스 좌표다.
		const ImVec2 NodeMin = NodeEditor::GetNodePosition(Id);
		const ImVec2 NodeSize = NodeEditor::GetNodeSize(Id);
		const ImVec2 NodeMax(NodeMin.x + NodeSize.x, NodeMin.y + NodeSize.y);
		Background->AddRectFilled(ImVec2(NodeMin.x + 1.0f, NodeMin.y + 1.0f), ImVec2(NodeMax.x - 1.0f, HeaderBottom),
			HeaderColor(Node, Def), 5.0f, ImDrawFlags_RoundCornersTop);
	}
	if (Def && !Def->Tooltip.empty() && NodeEditor::GetHoveredNode() == Id && !ImGui::IsAnyItemHovered())
	{
		NodeEditor::Suspend();
		ImGui::SetTooltip("%s", Def->Tooltip.c_str());
		NodeEditor::Resume();
	}
}

void FBlueprintGraphPanel::DrawPinRow(FBPGraph& Graph, FBPPin& Pin)
{
	const FBPNode* Node = nullptr;
	Graph.FindPin(Pin.Id, &Node);
	const bool bLinked = Graph.IsPinLinked(Pin.Id);
	const bool bLabel = Node && ShowsPinLabel(Pin, *Node);

	NodeEditor::BeginPin(NodeEditor::PinId(Pin.Id), Pin.bOutput ? NodeEditor::PinKind::Output : NodeEditor::PinKind::Input);
	NodeEditor::PinPivotAlignment(Pin.bOutput ? ImVec2(1.0f, 0.5f) : ImVec2(0.0f, 0.5f));
	NodeEditor::PinPivotSize(ImVec2(0.0f, 0.0f));
	if (Pin.bOutput)
	{
		if (bLabel)
		{
			ImGui::TextUnformatted(Pin.Name.c_str());
			ImGui::SameLine(0.0f, 4.0f);
		}
		DrawPinIcon(Pin.Type, bLinked);
	}
	else
	{
		DrawPinIcon(Pin.Type, bLinked);
		if (bLabel)
		{
			ImGui::SameLine(0.0f, 4.0f);
			ImGui::TextUnformatted(Pin.Name.c_str());
		}
	}
	NodeEditor::EndPin();

	// 이어지지 않은 입력 값은 핀 옆에서 고친다. Target은 비워 두면 Self다.
	if (!Pin.bOutput && !bLinked)
	{
		if (Pin.Type == EBPPinType::Object && Pin.Name == "Target")
		{
			ImGui::SameLine(0.0f, 6.0f);
			ImGui::TextDisabled("self");
		}
		else if (Pin.Type != EBPPinType::Exec && Pin.Type != EBPPinType::Object)
		{
			ImGui::SameLine(0.0f, 6.0f);
			if (DrawPinValueEditor(Pin))
				Editor.NoteEdited();
		}
	}
}

bool FBlueprintGraphPanel::DrawPinValueEditor(FBPPin& Pin)
{
	bool bChanged = false;
	ImGui::PushID(Pin.Id);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 1.0f));
	switch (Pin.Type)
	{
	case EBPPinType::Bool:
		bChanged = ImGui::Checkbox("##Value", &Pin.Default.Bool);
		break;
	case EBPPinType::Float:
		ImGui::SetNextItemWidth(56.0f);
		bChanged = ImGui::DragFloat("##Value", &Pin.Default.Float, 0.1f, 0.0f, 0.0f, "%.2f");
		break;
	case EBPPinType::String:
	{
		char Buffer[256];
		strncpy_s(Buffer, sizeof(Buffer), Pin.Default.String.c_str(), _TRUNCATE);
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::InputText("##Value", Buffer, sizeof(Buffer)))
		{
			Pin.Default.String = Buffer;
			bChanged = true;
		}
		break;
	}
	case EBPPinType::Vector:
	{
		float* Axes[3] = { &Pin.Default.Vector.X, &Pin.Default.Vector.Y, &Pin.Default.Vector.Z };
		const char* Ids[3] = { "##X", "##Y", "##Z" };
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Axis > 0)
				ImGui::SameLine(0.0f, 2.0f);
			ImGui::SetNextItemWidth(46.0f);
			bChanged |= ImGui::DragFloat(Ids[Axis], Axes[Axis], 0.1f, 0.0f, 0.0f, "%.1f");
		}
		break;
	}
	default:
		break;
	}
	ImGui::PopStyleVar();
	ImGui::PopID();
	return bChanged;
}

void FBlueprintGraphPanel::DrawLinks(const FBPGraph& Graph)
{
	for (const FBPLink& Link : Graph.Links)
	{
		const FBPPin* From = Graph.FindPin(Link.FromPin);
		const EBPPinType Type = From ? From->Type : EBPPinType::Exec;
		NodeEditor::Link(NodeEditor::LinkId(Link.Id), NodeEditor::PinId(Link.FromPin), NodeEditor::PinId(Link.ToPin),
			PinColor(Type), Type == EBPPinType::Exec ? 3.0f : 2.0f);
	}
}

void FBlueprintGraphPanel::HandleCreate(FBPGraph& Graph)
{
	if (NodeEditor::BeginCreate(ImVec4(1, 1, 1, 1), 2.0f))
	{
		NodeEditor::PinId A, B;
		if (NodeEditor::QueryNewLink(&A, &B) && A && B)
		{
			const int32 PinA = static_cast<int32>(A.Get());
			const int32 PinB = static_cast<int32>(B.Get());
			FString Reason;
			if (Graph.CanConnect(PinA, PinB, &Reason))
			{
				if (NodeEditor::AcceptNewItem(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), 3.0f))
				{
					Graph.Connect(PinA, PinB);
					Editor.NoteEdited();
				}
			}
			else
			{
				NodeEditor::RejectNewItem(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), 2.0f);
				NodeEditor::Suspend();
				ImGui::SetTooltip("%s", Reason.c_str());
				NodeEditor::Resume();
			}
		}

		// 핀에서 끌어 빈 곳에 놓으면: 그 핀에 이을 수 있는 노드만 보이는 메뉴를 연다 (UE와 같다).
		NodeEditor::PinId FromPin;
		if (NodeEditor::QueryNewNode(&FromPin) && FromPin)
		{
			if (NodeEditor::AcceptNewItem())
			{
				CreateFromPin = static_cast<int32>(FromPin.Get());
				bOpenCreateMenu = true;
			}
		}
	}
	NodeEditor::EndCreate();
}

void FBlueprintGraphPanel::HandleDelete(FBPGraph& Graph)
{
	if (NodeEditor::BeginDelete())
	{
		NodeEditor::LinkId LinkId;
		while (NodeEditor::QueryDeletedLink(&LinkId))
		{
			if (NodeEditor::AcceptDeletedItem())
			{
				Graph.RemoveLink(static_cast<int32>(LinkId.Get()));
				Editor.NoteEdited();
			}
		}
		NodeEditor::NodeId NodeId;
		while (NodeEditor::QueryDeletedNode(&NodeId))
		{
			const int32 Id = static_cast<int32>(NodeId.Get());
			const FBPNode* Node = Graph.FindNode(Id);
			// Construction Script 시작 노드는 지울 수 없다.
			if (Node && Node->Type == "ConstructionScript")
			{
				NodeEditor::RejectDeletedItem();
				continue;
			}
			if (NodeEditor::AcceptDeletedItem())
			{
				Graph.RemoveNode(Id);
				PositionedNodes.erase(Id);
				Editor.NoteEdited();
			}
		}
	}
	NodeEditor::EndDelete();
}

void FBlueprintGraphPanel::HandleComponentDrop(FBPGraph& Graph, const ImVec2& CanvasMin, const ImVec2& CanvasMax)
{
	const ImGuiPayload* Payload = ImGui::GetDragDropPayload();
	if (!Payload || !Payload->IsDataType(ComponentVariablePayload) || !ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		return;
	NodeEditor::Suspend();
	const ImVec2 Mouse = ImGui::GetMousePos();
	const bool bInside = Mouse.x >= CanvasMin.x && Mouse.y >= CanvasMin.y && Mouse.x < CanvasMax.x && Mouse.y < CanvasMax.y
		&& ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	const ImVec2 Position = NodeEditor::ScreenToCanvas(Mouse);
	NodeEditor::Resume();
	if (!bInside)
		return;
	CreatePosition = Position;
	CreateFromPin = 0;
	SpawnNode(Graph, "GetComponent", FString(static_cast<const char*>(Payload->Data)));
}

void FBlueprintGraphPanel::DrawPopups(FBPGraph& Graph)
{
	NodeEditor::NodeId NodeId;
	NodeEditor::PinId PinId;
	NodeEditor::LinkId LinkId;
	const bool bNodeMenu = NodeEditor::ShowNodeContextMenu(&NodeId);
	const bool bPinMenu = !bNodeMenu && NodeEditor::ShowPinContextMenu(&PinId);
	const bool bLinkMenu = !bNodeMenu && !bPinMenu && NodeEditor::ShowLinkContextMenu(&LinkId);
	const bool bBackgroundMenu = !bNodeMenu && !bPinMenu && !bLinkMenu && NodeEditor::ShowBackgroundContextMenu();

	// 팝업은 화면 좌표로 그린다.
	NodeEditor::Suspend();
	if (bBackgroundMenu)
	{
		CreateFromPin = 0;
		bOpenCreateMenu = true;
	}
	if (bOpenCreateMenu)
	{
		CreatePosition = NodeEditor::ScreenToCanvas(ImGui::GetMousePos());
		CreateSearch[0] = '\0';
		bFocusCreateSearch = true;
		ImGui::OpenPopup("BlueprintCreateNode");
		bOpenCreateMenu = false;
	}
	if (bNodeMenu)
	{
		ContextNodeId = static_cast<int32>(NodeId.Get());
		ImGui::OpenPopup("BlueprintNodeMenu");
	}
	if (bPinMenu)
	{
		ContextPinId = static_cast<int32>(PinId.Get());
		ImGui::OpenPopup("BlueprintPinMenu");
	}
	if (bLinkMenu)
	{
		ContextLinkId = static_cast<int32>(LinkId.Get());
		ImGui::OpenPopup("BlueprintLinkMenu");
	}

	DrawCreateMenu(Graph);

	if (ImGui::BeginPopup("BlueprintNodeMenu"))
	{
		const FBPNode* Node = Graph.FindNode(ContextNodeId);
		if (!Node)
		{
			ImGui::CloseCurrentPopup();
		}
		else
		{
			ImGui::TextDisabled("%s", FBlueprintNodeLibrary::GetTitle(*Node).c_str());
			ImGui::Separator();
			if (ImGui::MenuItem("Break Node Link(s)"))
			{
				for (const FBPPin& Pin : Node->Pins)
					Graph.BreakPinLinks(Pin.Id);
				Editor.NoteEdited();
			}
			if (ImGui::MenuItem("Delete", "Del", false, Node->Type != "ConstructionScript"))
			{
				Graph.RemoveNode(ContextNodeId);
				PositionedNodes.erase(ContextNodeId);
				Editor.NoteEdited();
			}
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("BlueprintPinMenu"))
	{
		if (ImGui::MenuItem("Break Link(s)", nullptr, false, Graph.IsPinLinked(ContextPinId)))
		{
			Graph.BreakPinLinks(ContextPinId);
			Editor.NoteEdited();
		}
		ImGui::EndPopup();
	}
	if (ImGui::BeginPopup("BlueprintLinkMenu"))
	{
		if (ImGui::MenuItem("Delete"))
		{
			Graph.RemoveLink(ContextLinkId);
			Editor.NoteEdited();
		}
		ImGui::EndPopup();
	}
	NodeEditor::Resume();
}

void FBlueprintGraphPanel::DrawCreateMenu(FBPGraph& Graph)
{
	ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_Appearing);
	ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 0.0f), ImVec2(320.0f, 460.0f));
	if (!ImGui::BeginPopup("BlueprintCreateNode"))
		return;

	const FBPPin* FromPin = CreateFromPin ? Graph.FindPin(CreateFromPin) : nullptr;
	ImGui::TextDisabled(FromPin ? "Actions taking %s" : "All Actions for this Blueprint", FromPin ? FromPin->Name.c_str() : "");
	if (bFocusCreateSearch)
	{
		ImGui::SetKeyboardFocusHere();
		bFocusCreateSearch = false;
	}
	EditorIcons::SearchBox("##CreateNodeSearch", CreateSearch, sizeof(CreateSearch), "Search");
	const bool bSearching = CreateSearch[0] != '\0';
	ImGui::Separator();

	ImGui::BeginChild("##CreateNodeList", ImVec2(0.0f, 340.0f));

	// 카테고리 순서는 라이브러리에 넣은 순서
	TArray<FString> Categories;
	for (const FBPNodeDef* Def : FBlueprintNodeLibrary::GetAll())
		if (Def->Type != "GetComponent" && Def->Category != "Functions" && Categories.Find(Def->Category) == INDEX_NONE)
			Categories.Add(Def->Category);

	auto Matches = [&](const FBPNodeDef& Def)
	{
		if (Def.Type == "GetComponent" || Def.Type == "CallFunction" || Def.Type == "CallPureFunction" || !AllowedInGraph(Def, Kind))
			return false;
		if (Def.bUnique && GraphHasType(Graph, Def.Type))
			return false;
		if (bSearching && !EditorIcons::MatchesFilter(Def.Title.c_str(), CreateSearch) && !EditorIcons::MatchesFilter(Def.Category.c_str(), CreateSearch))
			return false;
		if (FromPin)
		{
			bool bCompatible = false;
			for (const FBPPinDef& Pin : Def.Pins)
				bCompatible |= PinsCompatible(Pin, *FromPin);
			if (!bCompatible)
				return false;
		}
		return true;
	};

	// 컴포넌트 변수 (Get <이름>)
	if (AActor* Template = Editor.GetTemplate(); Template && (!FromPin || (FromPin->Type == EBPPinType::Object && !FromPin->bOutput)))
	{
		TArray<FString> Names;
		for (UActorComponent* Component : Template->GetComponents())
		{
			if (!Component)
				continue;
			if (const USceneComponent* Scene = Cast<USceneComponent>(Component); Scene && Scene->IsHiddenInDetails())
				continue;
			const FString Label = "Get " + Component->GetName();
			if (!bSearching || EditorIcons::MatchesFilter(Label.c_str(), CreateSearch))
				Names.Add(Component->GetName());
		}
		if (!Names.IsEmpty())
		{
			if (bSearching)
				ImGui::SetNextItemOpen(true);
			if (ImGui::TreeNodeEx("Variables", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
			{
				for (const FString& Name : Names)
					if (ImGui::Selectable(("Get " + Name).c_str()))
					{
						SpawnNode(Graph, "GetComponent", Name);
						ImGui::CloseCurrentPopup();
					}
				ImGui::TreePop();
			}
		}
	}

	// C++ UFUNCTION(BlueprintCallable / BlueprintPure): 클래스별로 묶는다.
	{
		TArray<FBPCallableFunction> Functions;
		FBlueprintNodeLibrary::GetCallableFunctions(Functions);
		TArray<UClass*> Classes;
		for (const FBPCallableFunction& Entry : Functions)
			if (Classes.Find(Entry.Class) == INDEX_NONE)
				Classes.Add(Entry.Class);

		auto FunctionMatches = [&](const FBPCallableFunction& Entry, FString& OutTitle)
		{
			FBPNode Probe;
			Probe.Type = Entry.Type;
			Probe.Param = Entry.Param;
			OutTitle = FBlueprintNodeLibrary::GetTitle(Probe);
			if (bSearching && !EditorIcons::MatchesFilter(OutTitle.c_str(), CreateSearch) &&
				!EditorIcons::MatchesFilter(Entry.Class->Name.c_str(), CreateSearch))
				return false;
			if (FromPin)
			{
				TArray<FBPPinDef> Pins;
				if (!FBlueprintNodeLibrary::GetPinDefs(Entry.Type, Entry.Param, Pins))
					return false;
				bool bCompatible = false;
				for (const FBPPinDef& Pin : Pins)
					bCompatible |= PinsCompatible(Pin, *FromPin);
				return bCompatible;
			}
			return true;
		};

		for (UClass* Class : Classes)
		{
			bool bAny = false;
			FString Title;
			for (const FBPCallableFunction& Entry : Functions)
				bAny |= Entry.Class == Class && FunctionMatches(Entry, Title);
			if (!bAny)
				continue;
			if (bSearching || FromPin)
				ImGui::SetNextItemOpen(true);
			const FString Label = FBlueprintNodeLibrary::ToDisplayName(Class->Name, true) + " (C++)";
			if (ImGui::TreeNodeEx(Label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
			{
				for (const FBPCallableFunction& Entry : Functions)
				{
					if (Entry.Class != Class || !FunctionMatches(Entry, Title))
						continue;
					ImGui::PushID(Entry.Param.c_str());
					if (ImGui::Selectable(Title.c_str()))
					{
						SpawnNode(Graph, Entry.Type, Entry.Param);
						ImGui::CloseCurrentPopup();
					}
					ImGui::SetItemTooltip("%s::%s%s", Class->Name.c_str(), Entry.Function->Name.c_str(),
						Entry.Type == "CallPureFunction" ? " (pure)" : "");
					ImGui::PopID();
				}
				ImGui::TreePop();
			}
		}
	}

	for (const FString& Category : Categories)
	{
		bool bAny = false;
		for (const FBPNodeDef* Def : FBlueprintNodeLibrary::GetAll())
			bAny |= Def->Category == Category && Matches(*Def);
		if (!bAny)
			continue;
		if (bSearching || FromPin)
			ImGui::SetNextItemOpen(true);
		if (ImGui::TreeNodeEx(Category.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth))
		{
			for (const FBPNodeDef* Def : FBlueprintNodeLibrary::GetAll())
			{
				if (Def->Category != Category || !Matches(*Def))
					continue;
				if (ImGui::Selectable(Def->Title.c_str()))
				{
					SpawnNode(Graph, Def->Type);
					ImGui::CloseCurrentPopup();
				}
				if (!Def->Tooltip.empty())
					ImGui::SetItemTooltip("%s", Def->Tooltip.c_str());
			}
			ImGui::TreePop();
		}
	}
	ImGui::EndChild();
	ImGui::EndPopup();
}

void FBlueprintGraphPanel::SpawnNode(FBPGraph& Graph, const FString& Type, const FString& Param)
{
	// 핀 값을 미리 복사한다. AddNode가 노드 배열을 다시 잡으면 핀 포인터가 무효가 된다.
	FBPPin From;
	bool bHasFrom = false;
	if (const FBPPin* Pin = CreateFromPin ? Graph.FindPin(CreateFromPin) : nullptr)
	{
		From = *Pin;
		bHasFrom = true;
	}

	FBPNode* Node = Graph.AddNode(Type, CreatePosition.x, CreatePosition.y, Param);
	if (!Node)
		return;
	if (bHasFrom)
	{
		for (const FBPPin& Pin : Node->Pins)
		{
			if (Pin.Type == From.Type && Pin.bOutput != From.bOutput)
			{
				Graph.Connect(From.Id, Pin.Id);
				break;
			}
		}
	}
	CreateFromPin = 0;
	Editor.NoteEdited();
}

void FBlueprintGraphPanel::SyncNodePositions(FBPGraph& Graph)
{
	bool bMoved = false;
	for (FBPNode& Node : Graph.Nodes)
	{
		if (PositionedNodes.find(Node.Id) == PositionedNodes.end())
			continue;
		const ImVec2 Position = NodeEditor::GetNodePosition(NodeEditor::NodeId(Node.Id));
		if (Position.x != Node.X || Position.y != Node.Y)
		{
			Node.X = Position.x;
			Node.Y = Position.y;
			bMoved = true;
		}
	}
	if (bMoved && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		Editor.NoteEdited();
}
