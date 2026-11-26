#include "EnginePCH.h"
#include "Editor/BlueprintEditor/BlueprintEditorTab.h"
#include "Editor/BlueprintEditor/BlueprintEditor.h"
#include "Editor/BlueprintEditor/BlueprintGraphPanel.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Core/EngineLog.h"
#include "Editor/Details/PropertyEditor.h"
#include "Editor/EditorUI/ComponentTypeInfo.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Editor/Viewports/EditorViewportToolbar.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"

namespace
{
	// UE5 선택 줄 색
	constexpr ImU32 SelectionBlue = IM_COL32(0, 112, 224, 255);
	constexpr ImU32 SelectionBlueHovered = IM_COL32(24, 128, 236, 255);

	// UE5 My Blueprint / Details의 섹션 머리줄 (어두운 막대에 접기 화살표)
	bool SectionHeader(const char* Label, ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_DefaultOpen)
	{
		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_TableHeaderBg));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
		const bool bOpen = ImGui::CollapsingHeader(Label, Flags);
		ImGui::PopStyleColor(3);
		return bOpen;
	}

	// 왼쪽에 이름, 오른쪽에 값 칸이 오는 Details 한 줄. 값 칸 너비를 정하고 커서를 옮긴다.
	void BeginDetailRow(const char* Label)
	{
		const float LabelWidth = ImGui::GetContentRegionAvail().x * 0.4f;
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Label);
		ImGui::SameLine(LabelWidth);
		ImGui::SetNextItemWidth(-FLT_MIN);
	}
}

// ---------------- 패널 공통 ----------------

FBlueprintEditorPanel::FBlueprintEditorPanel(FBlueprintEditor& InEditor, const char* InName)
	: Editor(InEditor)
	, PanelName(InName)
	, WindowName(PanelName + "###BlueprintPanel:" + InName + ":" + InEditor.GetAssetPath())
{
}

void FBlueprintEditorPanel::OnRender()
{
	// 같은 BP 에디터의 패널끼리만 도킹한다.
	ImGuiWindowClass WindowClass;
	WindowClass.ClassId = DockClassId;
	WindowClass.DockingAllowUnclassed = false;
	WindowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton;
	ImGui::SetNextWindowClass(&WindowClass);
	ImGui::SetNextWindowSize(ImVec2(480.0f, 320.0f), ImGuiCond_FirstUseEver);

	if (EditorPanelUI::BeginPanel(GetWindowName(), GetOpenFlag()))
		DrawContent();
	ImGui::End();
}

bool FBlueprintEditorPanel::IsDetached(ImGuiID InDockSpaceId) const
{
	const ImGuiWindow* Window = ImGui::FindWindowByName(GetWindowName());
	return Window && (!Window->DockNode || ImGui::DockNodeGetRootNode(Window->DockNode)->ID != InDockSpaceId);
}

// ---------------- Components ----------------

void FBlueprintComponentsPanel::DrawContent()
{
	AActor* Template = Editor.GetTemplate();
	if (!Template)
		return;

	const float S = EditorIcons::GetScale();

	// [+ Add] : Details 패널과 같은 분류·검색 메뉴
	const float AddH = ImGui::GetFrameHeight() + 2.0f * S;
	const ImVec2 AddMin = ImGui::GetCursorScreenPos();
	const float AddW = EditorIcons::CalcButtonWidth(true, "Add", false, AddH);
	ImDrawList* DL = ImGui::GetWindowDrawList();
	DL->AddRectFilled(AddMin, ImVec2(AddMin.x + AddW, AddMin.y + AddH), ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f * S);
	DL->AddRect(AddMin, ImVec2(AddMin.x + AddW, AddMin.y + AddH), ImGui::GetColorU32(ImGuiCol_Border), 4.0f * S);
	if (EditorIcons::Button("##AddComponent", EEditorIcon::Plus, EditorIcons::PlayGreen, "Add", false, false, AddH))
	{
		AddSearch[0] = '\0';
		ImGui::OpenPopup("BlueprintAddComponent");
	}
	ImGui::SetItemTooltip("Add a component to this blueprint");
	// UE5처럼 [+ Add] 오른쪽에 트리 검색 칸
	ImGui::SameLine(0.0f, 6.0f * S);
	ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, AddMin.y + (AddH - ImGui::GetFrameHeight()) * 0.5f));
	EditorIcons::SearchBox("##ComponentTreeSearch", TreeSearch, sizeof(TreeSearch), "Search");
	ImGui::SetNextWindowSize(ImVec2(300.0f * S, 0.0f), ImGuiCond_Appearing);
	if (ImGui::BeginPopup("BlueprintAddComponent"))
	{
		if (UActorComponent* Added = FPropertyEditor::DrawAddComponentMenu(Template, AddSearch, sizeof(AddSearch)))
		{
			Editor.SelectComponent(Added);
			Editor.NoteEdited();
		}
		ImGui::EndPopup();
	}
	ImGui::Spacing();

	// 고른 줄은 UE5처럼 파란색으로 칠한다.
	ImGui::PushStyleColor(ImGuiCol_Header, SelectionBlue);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, SelectionBlueHovered);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, SelectionBlue);

	// 맨 위 줄은 액터 자신(Self). 누르면 액터 기본값(Class Defaults)을 고친다.
	{
		EEditorIcon Icon;
		ImU32 Color;
		ComponentTypeInfo::GetActorIcon(Template, Icon, Color);
		const FString Label = Editor.GetBlueprintClass()->Name + " (Self)";
		const float H = ImGui::GetTextLineHeight() + 6.0f * S;
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		if (ImGui::Selectable("##Self", Editor.GetSelectedComponent() == nullptr && !Editor.IsShowingClassSettings(), 0, ImVec2(0.0f, H)))
			Editor.SelectComponent(nullptr);
		EditorIcons::Draw(DL, Icon, ImVec2(Min.x + 11.0f * S, Min.y + H * 0.5f), 14.0f * S, Color);
		DL->AddText(ImVec2(Min.x + 24.0f * S, Min.y + (H - ImGui::GetFontSize()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), Label.c_str());
	}

	ImGui::Indent(12.0f * S);
	if (USceneComponent* Root = Template->GetRootComponent())
		DrawSceneNode(Root, Root);

	// 위치가 없는 컴포넌트(이동 등)
	for (UActorComponent* Component : Template->GetComponents())
	{
		if (!Component || Cast<USceneComponent>(Component) || !EditorIcons::MatchesFilter(Component->GetName().c_str(), TreeSearch))
			continue;
		ImGui::PushID(Component);
		const bool bSelected = Editor.GetSelectedComponent() == Component;
		ImGui::TreeNodeEx("##Node", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
			ImGuiTreeNodeFlags_SpanAvailWidth | (bSelected ? ImGuiTreeNodeFlags_Selected : 0), "%s", "");
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		if (ImGui::IsItemClicked())
			Editor.SelectComponent(Component);
		if (ImGui::BeginPopupContextItem("##Context"))
		{
			if (ImGui::MenuItem("Delete"))
				PendingDelete = Component;
			ImGui::EndPopup();
		}
		DrawRowLabel(Component, Min, Max);
		ImGui::PopID();
	}
	ImGui::Unindent(12.0f * S);
	ImGui::PopStyleColor(3);

	// 그리기가 끝난 뒤 지운다.
	if (PendingDelete)
	{
		if (RenamingComponent == PendingDelete)
			RenamingComponent = nullptr;
		Editor.DeleteComponent(PendingDelete);
		PendingDelete = nullptr;
	}
}

void FBlueprintComponentsPanel::DrawRowLabel(UActorComponent* Component, const ImVec2& Min, const ImVec2& Max)
{
	const float S = EditorIcons::GetScale();
	const float IconSize = 14.0f * S;
	const float MidY = (Min.y + Max.y) * 0.5f;
	float X = Min.x + ImGui::GetTreeNodeToLabelSpacing();
	ImDrawList* DL = ImGui::GetWindowDrawList();

	EEditorIcon Icon;
	ImU32 Color;
	ComponentTypeInfo::GetComponentIcon(Component->GetClass(), Icon, Color);
	EditorIcons::Draw(DL, Icon, ImVec2(X + IconSize * 0.5f, MidY), IconSize, Color);
	X += IconSize + 6.0f * S;

	const float TextY = MidY - ImGui::GetFontSize() * 0.5f;
	const FString Name = Component->GetName();
	DL->AddText(ImVec2(X, TextY), ImGui::GetColorU32(ImGuiCol_Text), Name.c_str());
	X += ImGui::CalcTextSize(Name.c_str()).x + 5.0f * S;
	const FString Kind = "(" + ComponentTypeInfo::GetComponentDisplayName(Component->GetClass()->Name) + ")";
	DL->AddText(ImVec2(X, TextY), ImGui::GetColorU32(ImGuiCol_TextDisabled), Kind.c_str());
}

bool FBlueprintComponentsPanel::SubtreeMatches(USceneComponent* Component) const
{
	if (!Component || Component->IsHiddenInDetails())
		return false;
	if (EditorIcons::MatchesFilter(Component->GetName().c_str(), TreeSearch))
		return true;
	for (USceneComponent* Child : Component->GetAttachChildren())
		if (Child && Child->GetOwner() == Component->GetOwner() && SubtreeMatches(Child))
			return true;
	return false;
}

void FBlueprintComponentsPanel::DrawSceneNode(USceneComponent* Component, USceneComponent* Root)
{
	if (!Component || Component->IsHiddenInDetails())
		return;
	// 검색 중에는 맞는 줄과 그 조상만 펼쳐서 보인다.
	const bool bFiltering = TreeSearch[0] != '\0';
	if (bFiltering && !SubtreeMatches(Component))
		return;

	TArray<USceneComponent*> Children;
	for (USceneComponent* Child : Component->GetAttachChildren())
		if (Child && !Child->IsHiddenInDetails() && Child->GetOwner() == Component->GetOwner())
			Children.Add(Child);

	const bool bSelected = Editor.GetSelectedComponent() == Component;
	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
	if (bSelected) Flags |= ImGuiTreeNodeFlags_Selected;
	if (Children.IsEmpty()) Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

	ImGui::PushID(Component);
	if (bFiltering && !Children.IsEmpty())
		ImGui::SetNextItemOpen(true);
	const bool bOpen = ImGui::TreeNodeEx("##Node", Flags, "%s", "");
	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	const bool bRenaming = RenamingComponent == Component;

	if (!bRenaming)
	{
		if (ImGui::IsItemClicked())
			Editor.SelectComponent(Component);
		// 더블클릭으로 이름 바꾸기 (루트 제외, 레벨 Details와 같다)
		if (Component != Root && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			RenamingComponent = Component;
			strncpy_s(RenameBuffer, sizeof(RenameBuffer), Component->GetName().c_str(), _TRUNCATE);
			bFocusRename = true;
		}
		if (ImGui::BeginPopupContextItem("##Context"))
		{
			if (ImGui::MenuItem("Delete", nullptr, false, Component != Root))
				PendingDelete = Component;
			ImGui::EndPopup();
		}
		DrawRowLabel(Component, Min, Max);
	}
	else
	{
		const float S = EditorIcons::GetScale();
		ImGui::SameLine(0.0f, 20.0f * S);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (bFocusRename)
		{
			ImGui::SetKeyboardFocusHere();
			bFocusRename = false;
		}
		const bool bEnter = ImGui::InputText("##Rename", RenameBuffer, sizeof(RenameBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		if (bEnter || ImGui::IsItemDeactivated())
		{
			Editor.RenameComponent(Component, RenameBuffer);
			RenamingComponent = nullptr;
		}
	}

	if (bOpen && !Children.IsEmpty())
	{
		for (USceneComponent* Child : Children)
			DrawSceneNode(Child, Root);
		ImGui::TreePop();
	}
	ImGui::PopID();
}

// ---------------- Viewport ----------------

void FBlueprintViewportPanel::DrawContent()
{
	FEditorViewportClient* Client = Editor.GetViewportClient();
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (!Client || Size.x < 1.0f || Size.y < 1.0f)
		return;

	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const ImVec2 ViewMax(Origin.x + Size.x, Origin.y + Size.y);
	if (FTexture2D* Target = Client->Viewport ? Client->Viewport->GetRenderTargetTexture() : nullptr; Target && Target->GetSRV())
		ImGui::GetWindowDrawList()->AddImage(Target->GetSRV(), Origin, ViewMax);

	// 툴바의 기즈모 버튼(이동/회전/크기)도 함께 그린다.
	const bool bToolbarBusy = EditorViewportUI::DrawAssetViewportToolbar(Client, Origin, ViewMax, Editor.GetGizmoMode(),
		[this](UE::Widget::EWidgetMode Mode) { Editor.SetGizmoMode(Mode); });

	ImGui::SetCursorScreenPos(Origin);
	ImGui::InvisibleButton("##BlueprintViewport", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	const bool bHovered = ImGui::IsItemHovered() && !bToolbarBusy;
	if (bHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		ImGui::SetWindowFocus();

	const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (bFocused && bHovered)
		EditorViewportUI::HandleViewTypeShortcuts(Client);
	if (bFocused && bHovered && ImGui::IsKeyPressed(ImGuiKey_F, false))
		Editor.FocusTemplate();

	const ImGuiIO& IO = ImGui::GetIO();
	const bool bMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
	const bool bCameraDragging = ImGui::IsMouseDown(ImGuiMouseButton_Right);
	const FBlueprintViewportPointer Pointer
	{
		.MouseUV = FVector2((IO.MousePos.x - Origin.x) / Size.x, (IO.MousePos.y - Origin.y) / Size.y),
		.bHovered = bHovered,
		.bMousePressed = bHovered && !bCameraDragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
		.bMouseDown = bMouseDown,
		.bCancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false),
	};

	// W/E/R: 이동/회전/크기, Space: 순환. 우클릭으로 카메라를 날리는 중에는 WASD가 이동이므로 바꾸지 않는다.
	if (bFocused && bHovered && !bCameraDragging && !IO.WantTextInput && !Editor.IsGizmoDragging())
	{
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) Editor.SetGizmoMode(UE::Widget::WM_Translate);
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) Editor.SetGizmoMode(UE::Widget::WM_Rotate);
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) Editor.SetGizmoMode(UE::Widget::WM_Scale);
		if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
			Editor.SetGizmoMode(static_cast<UE::Widget::EWidgetMode>((static_cast<int32>(Editor.GetGizmoMode()) + 1) % 3));
	}

	FViewportCameraInput Input
	{
		.bCaptured   = bFocused && ImGui::IsItemActive() && bCameraDragging && !bMouseDown && !Editor.IsGizmoDragging(),
		.bAllowWheel = bHovered && !Editor.IsGizmoDragging(),
		.bForward    = ImGui::IsKeyDown(ImGuiKey_W),
		.bBackward   = ImGui::IsKeyDown(ImGuiKey_S),
		.bRight      = ImGui::IsKeyDown(ImGuiKey_D),
		.bLeft       = ImGui::IsKeyDown(ImGuiKey_A),
		.bUp         = ImGui::IsKeyDown(ImGuiKey_E),
		.bDown       = ImGui::IsKeyDown(ImGuiKey_Q),
		.MouseX      = IO.MouseDelta.x,
		.MouseY      = IO.MouseDelta.y,
		.Wheel       = IO.MouseWheel * 120.0f,
	};
	Editor.SetViewportState(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y), Input, Pointer);
}

// ---------------- Details ----------------

void FBlueprintDetailsPanel::DrawContent()
{
	AActor* Template = Editor.GetTemplate();
	UClass* BlueprintClass = Editor.GetBlueprintClass();
	if (!Template || !BlueprintClass)
		return;

	// UE5처럼 맨 위에 속성 검색 칸
	EditorIcons::SearchBox("##BlueprintDetailsSearch", Search, sizeof(Search), "Search");
	ImGui::Spacing();

	// 고른 컴포넌트가 지워졌을 수 있으니 템플릿에 아직 있는지 확인한다.
	UActorComponent* Selected = Editor.GetSelectedComponent();
	if (Selected && Template->GetComponents().Find(Selected) == INDEX_NONE)
	{
		Editor.SelectComponent(nullptr);
		Selected = nullptr;
	}

	if (Editor.IsShowingClassSettings())
	{
		DrawClassSettings();
	}
	else if (Selected)
	{
		ImGui::PushID(Selected);
		if (!Editor.IsComponentBlueprint())   // 컴포넌트 BP는 BP 자체가 컴포넌트라 변수 이름이 없다
			DrawVariableSection(Selected);
		FPropertyEditor::DrawObjectProperties(Selected, nullptr, Search);
		if (Search[0] == '\0')
			FPropertyEditor::DrawComponentExtras(Selected);
		ImGui::PopID();
	}
	else
	{
		ImGui::PushID(Template);
		FPropertyEditor::DrawObjectProperties(Template, nullptr, Search);
		if (Search[0] == '\0')
			FPropertyEditor::DrawActorExtras(Template);
		ImGui::PopID();
	}

	// 이 패널에서 값이 바뀌었거나 마우스를 놓았으면 저장 표시(*)를 다시 계산한다.
	const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
	const bool bHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	if ((bFocused && ImGui::GetCurrentContext()->ActiveIdHasBeenEditedThisFrame) ||
		((bFocused || bHovered) && ImGui::IsMouseReleased(ImGuiMouseButton_Left)))
		Editor.NoteEdited();
}

void FBlueprintDetailsPanel::DrawVariableSection(UActorComponent* Component)
{
	// UE5 Details의 Variable 칸: 컴포넌트는 BP 안에서 변수 하나이고, 그 이름을 여기서 바꾼다.
	if (!EditorIcons::MatchesFilter("Variable Name", Search) && !EditorIcons::MatchesFilter("Component Class", Search))
		return;
	if (!SectionHeader("Variable"))
		return;

	// 다른 컴포넌트로 바뀌었거나 칸을 고치고 있지 않으면 지금 이름으로 다시 채운다.
	if (NameBufferOwner != Component || ImGui::GetActiveID() != ImGui::GetID("##VariableName"))
	{
		strncpy_s(NameBuffer, sizeof(NameBuffer), Component->GetName().c_str(), _TRUNCATE);
		NameBufferOwner = Component;
	}

	const bool bIsRoot = Editor.GetTemplate() && Component == Editor.GetTemplate()->GetRootComponent();
	BeginDetailRow("Variable Name");
	ImGui::BeginDisabled(bIsRoot);
	ImGui::InputText("##VariableName", NameBuffer, sizeof(NameBuffer), ImGuiInputTextFlags_AutoSelectAll);
	if (ImGui::IsItemDeactivatedAfterEdit() && !Editor.RenameComponent(Component, NameBuffer))
		strncpy_s(NameBuffer, sizeof(NameBuffer), Component->GetName().c_str(), _TRUNCATE);
	ImGui::EndDisabled();
	if (bIsRoot)
		ImGui::SetItemTooltip("The root component keeps its name");

	BeginDetailRow("Component Class");
	ImGui::TextDisabled("%s", ComponentTypeInfo::GetComponentDisplayName(Component->GetClass()->Name).c_str());
}

void FBlueprintDetailsPanel::DrawClassSettings()
{
	UClass* BlueprintClass = Editor.GetBlueprintClass();
	if (SectionHeader("Class Options"))
	{
		BeginDetailRow("Parent Class");
		ImGui::TextUnformatted(BlueprintClass->Super ? BlueprintClass->Super->Name.c_str() : "None");
		BeginDetailRow("Blueprint Type");
		ImGui::TextUnformatted(Editor.IsComponentBlueprint() ? "Component Blueprint" : "Actor Blueprint");
		BeginDetailRow("Blueprint");
		ImGui::TextUnformatted(BlueprintClass->Name.c_str());
		BeginDetailRow("Asset Path");
		ImGui::TextDisabled("%s", Editor.GetAssetPath().c_str());
	}
}

// ---------------- My Blueprint ----------------

void FBlueprintMyBlueprintPanel::DrawContent()
{
	AActor* Template = Editor.GetTemplate();
	if (!Template)
		return;

	const float S = EditorIcons::GetScale();
	EditorIcons::SearchBox("##MyBlueprintSearch", Search, sizeof(Search), "Search");
	ImGui::Spacing();

	ImGui::PushStyleColor(ImGuiCol_Header, SelectionBlue);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, SelectionBlueHovered);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, SelectionBlue);

	// GRAPHS: EventGraph와 그 안의 이벤트. 누르면 그래프 탭을 열고 그 노드로 옮긴다.
	if (SectionHeader("GRAPHS"))
	{
		const FBPGraph& EventGraph = Editor.GetGraph(EBlueprintGraphKind::EventGraph);
		ImGui::Indent(8.0f * S);
		const bool bOpen = ImGui::TreeNodeEx("EventGraph", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow);
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
			Editor.RequestOpenGraph(EBlueprintGraphKind::EventGraph);
		if (bOpen)
		{
			for (const FBPNode& Node : EventGraph.Nodes)
			{
				const FBPNodeDef* Def = FBlueprintNodeLibrary::Find(Node.Type);
				if (!Def || Def->Kind != EBPNodeKind::Event)
					continue;
				const FString Title = FBlueprintNodeLibrary::GetTitle(Node);
				if (!EditorIcons::MatchesFilter(Title.c_str(), Search))
					continue;
				ImGui::PushID(Node.Id);
				if (ImGui::Selectable(Title.c_str()))
					Editor.RequestOpenGraph(EBlueprintGraphKind::EventGraph, Node.Id);
				ImGui::PopID();
			}
			ImGui::TreePop();
		}
		ImGui::Unindent(8.0f * S);
	}

	// FUNCTIONS: Construction Script (UE에서도 함수 칸에 있다)
	if (SectionHeader("FUNCTIONS"))
	{
		ImGui::Indent(8.0f * S);
		if (EditorIcons::MatchesFilter("Construction Script", Search) && ImGui::Selectable("Construction Script"))
			Editor.RequestOpenGraph(EBlueprintGraphKind::ConstructionScript);
		ImGui::Unindent(8.0f * S);
	}
	ImGui::PopStyleColor(3);

	// 컴포넌트는 UE처럼 Variables > Components 아래에 변수로 보인다. 그래프로 끌어다 놓을 수 있다.
	if (!SectionHeader("VARIABLES"))
		return;

	ImGui::Indent(8.0f * S);
	if (ImGui::TreeNodeEx("Components", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
	{
		ImGui::PushStyleColor(ImGuiCol_Header, SelectionBlue);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, SelectionBlueHovered);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, SelectionBlue);
		ImDrawList* DL = ImGui::GetWindowDrawList();
		for (UActorComponent* Component : Template->GetComponents())
		{
			if (!Component)
				continue;
			if (const USceneComponent* Scene = Cast<USceneComponent>(Component); Scene && Scene->IsHiddenInDetails())
				continue;
			const FString Name = Component->GetName();
			if (!EditorIcons::MatchesFilter(Name.c_str(), Search))
				continue;

			ImGui::PushID(Component);
			const float H = ImGui::GetTextLineHeight() + 4.0f * S;
			const ImVec2 Min = ImGui::GetCursorScreenPos();
			if (ImGui::Selectable("##Var", Editor.GetSelectedComponent() == Component, 0, ImVec2(0.0f, H)))
				Editor.SelectComponent(Component);
			// 그래프에 끌어다 놓으면 이 컴포넌트를 가리키는 노드가 생긴다.
			if (ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload("BP_COMPONENT_VARIABLE", Name.c_str(), Name.size() + 1);
				ImGui::Text("Get %s", Name.c_str());
				ImGui::EndDragDropSource();
			}
			ImGui::SetItemTooltip("%s (drag into a graph)", ComponentTypeInfo::GetComponentDisplayName(Component->GetClass()->Name).c_str());
			const float TextY = Min.y + (H - ImGui::GetFontSize()) * 0.5f;
			DL->AddText(ImVec2(Min.x + 12.0f * S, TextY), ImGui::GetColorU32(ImGuiCol_Text), Name.c_str());
			// UE의 오브젝트 참조 변수 색 표시(파란 알약)
			const float PillX = Min.x + 12.0f * S + ImGui::CalcTextSize(Name.c_str()).x + 8.0f * S;
			const float MidY = Min.y + H * 0.5f;
			DL->AddRectFilled(ImVec2(PillX, MidY - 3.0f * S), ImVec2(PillX + 16.0f * S, MidY + 3.0f * S),
				IM_COL32(0, 168, 240, 255), 3.0f * S);
			ImGui::PopID();
		}
		ImGui::PopStyleColor(3);
		ImGui::TreePop();
	}
	ImGui::Unindent(8.0f * S);
}

// ---------------- Tab ----------------

FBlueprintEditorTab::FBlueprintEditorTab(FBlueprintEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, BlueprintEditor(InEditor)
{
	const ImGuiID AssetId = ImHashStr(InEditor.GetAssetPath().c_str());
	DockSpaceId = ImHashStr("BlueprintDockSpace", 0, AssetId);
	PanelClassId = ImHashStr("BlueprintPanelClass", 0, AssetId);

	// 순서는 BuildDefaultLayout이 쓴다.
	if (InEditor.IsComponentBlueprint())
	{
		// UE5 Component Blueprint 에디터처럼 Details + Viewport만 둔다 (컴포넌트 트리와 노드 그래프 없음).
		Panels.Add(MakeUnique<FBlueprintDetailsPanel>(InEditor));
		Panels.Add(MakeUnique<FBlueprintViewportPanel>(InEditor));
		for (auto& Panel : Panels)
		{
			Panel->SetDockClass(PanelClassId);
			Panel->Init();
		}
		return;
	}
	Panels.Add(MakeUnique<FBlueprintComponentsPanel>(InEditor));
	Panels.Add(MakeUnique<FBlueprintMyBlueprintPanel>(InEditor));
	Panels.Add(MakeUnique<FBlueprintDetailsPanel>(InEditor));
	Panels.Add(MakeUnique<FBlueprintViewportPanel>(InEditor));
	Panels.Add(MakeUnique<FBlueprintGraphPanel>(InEditor, EBlueprintGraphKind::ConstructionScript, "Construction Script"));
	Panels.Add(MakeUnique<FBlueprintGraphPanel>(InEditor, EBlueprintGraphKind::EventGraph, "EventGraph"));
	for (auto& Panel : Panels)
	{
		Panel->SetDockClass(PanelClassId);
		Panel->Init();
	}
}

void FBlueprintEditorTab::DrawMenuBar()
{
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", "Ctrl+S", false, BlueprintEditor.IsDirty()))
			BlueprintEditor.Save();
		ImGui::Separator();
		if (ImGui::MenuItem("Close Blueprint"))
			BlueprintEditor.Close();
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Window"))
	{
		for (auto& Panel : Panels)
			if (ImGui::MenuItem(Panel->GetPanelName(), nullptr, Panel->IsOpen()))
				Panel->SetOpen(!Panel->IsOpen());
		ImGui::EndMenu();
	}
}

void FBlueprintEditorTab::DrawToolbar()
{
	const float S = EditorIcons::GetScale();
	const float ButtonHeight = 28.0f * S;
	const float BarHeight = ButtonHeight + 10.0f * S;
	const ImU32 IconGray = ImGui::GetColorU32(ImGuiCol_Text, 0.78f);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * S, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f * S, 4.0f * S));
	if (ImGui::BeginChild("##BlueprintToolbar", ImVec2(0.0f, BarHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
	{
		ImGui::SetCursorPosY((BarHeight - ButtonHeight) * 0.5f);
		auto Gap = [S]()
		{
			ImGui::SameLine(0.0f, 8.0f * S);
			const ImVec2 P = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddLine(ImVec2(P.x, P.y + 4.0f * S), ImVec2(P.x, P.y + 24.0f * S),
				ImGui::GetColorU32(ImGuiCol_Separator));
			ImGui::Dummy(ImVec2(1.0f, 0.0f));
			ImGui::SameLine(0.0f, 8.0f * S);
		};

		// Save
		ImGui::BeginDisabled(!BlueprintEditor.IsDirty());
		if (EditorIcons::Button("##BPSave", EEditorIcon::Save, IconGray, nullptr, false, false, ButtonHeight))
			BlueprintEditor.Save();
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Save (Ctrl+S)");

		// Class Settings / Class Defaults: Details에 보일 것을 고른다.
		Gap();
		const bool bClassSettings = BlueprintEditor.IsShowingClassSettings();
		const bool bClassDefaults = !bClassSettings &&
			BlueprintEditor.GetSelectedComponent() == BlueprintEditor.GetComponentTemplate();
		if (EditorIcons::Button("##BPClassSettings", EEditorIcon::Settings, IconGray, "Class Settings", false, bClassSettings, ButtonHeight))
			BlueprintEditor.ShowClassSettings();
		ImGui::SetItemTooltip("Edit Class Settings (parent class and blueprint options)");
		ImGui::SameLine();
		const EEditorIcon DefaultsIcon = BlueprintEditor.IsComponentBlueprint() ? EEditorIcon::ClassComponent : EEditorIcon::ClassActor;
		if (EditorIcons::Button("##BPClassDefaults", DefaultsIcon, IconGray, "Class Defaults", false, bClassDefaults, ButtonHeight))
			BlueprintEditor.SelectComponent(nullptr);
		ImGui::SetItemTooltip("Edit the default values of this blueprint");

		// 뷰포트
		Gap();
		if (EditorIcons::Button("##BPFocus", EEditorIcon::Camera, IconGray, "Focus", false, false, ButtonHeight))
			BlueprintEditor.FocusTemplate();
		ImGui::SetItemTooltip("Frame the whole blueprint in the viewport (F)");
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
}

void FBlueprintEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	// UE5 Blueprint 에디터처럼 왼쪽 위 Components, 왼쪽 아래 My Blueprint, 오른쪽 Details, 가운데 Viewport
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);
	ImGuiID ViewportId = InDockSpaceId;
	if (BlueprintEditor.IsComponentBlueprint())
	{
		// 컴포넌트 BP: 가운데 Viewport, 오른쪽 Details
		const ImGuiID DetailsOnlyId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.40f, nullptr, &ViewportId);
		ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), DetailsOnlyId);
		ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), ViewportId);
		ImGui::DockBuilderFinish(InDockSpaceId);
		return;
	}
	ImGuiID MyBlueprintId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Left, 0.16f, nullptr, &ViewportId);
	const ImGuiID ComponentsId = ImGui::DockBuilderSplitNode(MyBlueprintId, ImGuiDir_Up, 0.35f, nullptr, &MyBlueprintId);
	const ImGuiID DetailsId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.30f, nullptr, &ViewportId);

	ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ComponentsId);
	ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), MyBlueprintId);
	ImGui::DockBuilderDockWindow(Panels[2]->GetWindowName(), DetailsId);
	// 가운데는 UE처럼 Viewport | Construction Script | EventGraph 탭
	ImGui::DockBuilderDockWindow(Panels[3]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[4]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[5]->GetWindowName(), ViewportId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}

void FBlueprintEditorTab::OnRender(bool bActive)
{
	BlueprintEditor.SetViewportState(0, 0, {});
	if (BlueprintEditor.IsCloseRequested())
		return;

	// Ctrl+S: 이 탭이 앞에 있을 때 BP를 저장한다 (레벨 저장 단축키는 레벨 탭에서만 동작한다).
	if (bActive && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && BlueprintEditor.IsDirty())
		BlueprintEditor.Save();

	if (bActive)
		DrawToolbar();

	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		const ImVec2 Size = ImGui::GetContentRegionAvail();
		if (bActive && Size.x > 0.0f && Size.y > 0.0f && (!bLayoutInitialized || !ImGui::DockBuilderGetNode(DockSpaceId)))
		{
			ImGui::DockBuilderRemoveNode(DockSpaceId);
			BuildDefaultLayout(DockSpaceId, Size);
			bLayoutInitialized = true;
		}

		ImGuiWindowClass WindowClass;
		WindowClass.ClassId = PanelClassId;
		WindowClass.DockingAllowUnclassed = false;
		const ImGuiDockNodeFlags Flags = bActive ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_KeepAliveOnly;
		if (bActive || ImGui::DockBuilderGetNode(DockSpaceId))
			EditorPanelUI::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f), Flags, &WindowClass);
	}

	for (auto& Panel : Panels)
		if (Panel->IsOpen() && (bActive || Panel->IsDetached(DockSpaceId)))
			Panel->OnRender();
}
