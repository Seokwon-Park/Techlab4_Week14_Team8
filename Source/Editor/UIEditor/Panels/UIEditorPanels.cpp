#include "EnginePCH.h"
#include "Editor/UIEditor/Panels/UIEditorPanels.h"

#include "Editor/Details/PropertyEditor.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "UI/UserWidget.h"
#include "UObject/UObjectHash.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace
{
	const char* PanelIdPrefix = "UIEditorPanel";
	const char* PalettePayload = "UI_PALETTE_CLASS";

	// UE UMG 디자이너 색
	const ImU32 ColorCanvasBg = IM_COL32(30, 30, 30, 255);
	const ImU32 ColorGridMinor = IM_COL32(36, 36, 36, 255);
	const ImU32 ColorGridMajor = IM_COL32(44, 44, 44, 255);
	const ImU32 ColorRulerBg = IM_COL32(26, 26, 26, 255);
	const ImU32 ColorRulerTick = IM_COL32(100, 100, 100, 255);
	const ImU32 ColorRulerText = IM_COL32(150, 150, 150, 255);
	const ImU32 ColorCategoryBg = IM_COL32(40, 40, 40, 255);
	const ImU32 ColorActiveButton = IM_COL32(15, 110, 230, 255);
	const ImU32 ColorSelection = IM_COL32(255, 170, 40, 255);

	// UE 디자이너 줌 단계. 인덱스 7이 1:1
	const float ZoomLevels[] = { 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f };
	constexpr int32 ZoomOneIndex = 7;

	const FVector2 ScreenSizes[] = { {1920, 1080}, {1280, 720}, {2560, 1440}, {2560, 1080}, {1080, 1920}, {750, 1334} };

	ImU32 IconGray() { return ImGui::GetColorU32(ImGuiCol_Text, 0.82f); }

	bool IsSelfOrAncestor(const UWidget* Candidate, const UWidget* Widget)
	{
		for (const UWidget* It = Widget; It; It = It->GetParent())
			if (It == Candidate) return true;
		return false;
	}

	// 팔레트·계층에서 쓰는 위젯 종류 표시 정보
	enum EPaletteCategory { Category_Common, Category_Panel, Category_Misc, Category_Count };
	const char* CategoryNames[] = { "COMMON", "PANEL", "MISC" };

	struct FWidgetTypeInfo
	{
		FString Label;
		EEditorIcon Icon = EEditorIcon::Widget;
		EPaletteCategory Category = Category_Misc;
	};

	FWidgetTypeInfo GetWidgetTypeInfo(const UClass* Class)
	{
		struct FKnown { const char* ClassName; const char* Label; EEditorIcon Icon; EPaletteCategory Category; };
		static const FKnown Known[] = {
			{ "UButton",      "Button",       EEditorIcon::WidgetButton,      Category_Common },
			{ "UImage",       "Image",        EEditorIcon::WidgetImage,       Category_Common },
			{ "UProgressBar", "Progress Bar", EEditorIcon::WidgetProgressBar, Category_Common },
			{ "UTextBlock",   "Text",         EEditorIcon::WidgetText,        Category_Common },
			{ "UCanvasPanel", "Canvas Panel", EEditorIcon::WidgetCanvasPanel, Category_Panel },
			{ "UVerticalBox", "Vertical Box", EEditorIcon::WidgetVerticalBox, Category_Panel },
		};
		FWidgetTypeInfo Info;
		if (!Class) return Info;
		for (const FKnown& Entry : Known)
		{
			if (Class->Name == Entry.ClassName)
			{
				Info.Label = Entry.Label;
				Info.Icon = Entry.Icon;
				Info.Category = Entry.Category;
				return Info;
			}
		}
		// 모르는 클래스: U 접두사를 빼고 대문자 앞에 공백 (UMyFancyWidget -> My Fancy Widget)
		const FString& Name = Class->Name;
		const size_t Start = (Name.size() > 1 && Name[0] == 'U' && std::isupper(static_cast<unsigned char>(Name[1]))) ? 1 : 0;
		for (size_t I = Start; I < Name.size(); ++I)
		{
			if (I > Start && std::isupper(static_cast<unsigned char>(Name[I])) && !std::isupper(static_cast<unsigned char>(Name[I - 1])))
				Info.Label += ' ';
			Info.Label += Name[I];
		}
		Info.Category = Class->IsChildOf(UPanelWidget::StaticClass()) ? Category_Panel : Category_Misc;
		if (Info.Category == Category_Panel) Info.Icon = EEditorIcon::WidgetCanvasPanel;
		return Info;
	}

	FString GetWidgetDisplayName(const UWidget* Widget)
	{
		return Widget->WidgetName.empty() ? GetWidgetTypeInfo(Widget->GetClass()).Label : Widget->WidgetName;
	}

	// 파일 경로에서 확장자 없는 이름 (Content/UI/GameOver.widget -> GameOver)
	FString GetAssetName(const FString& Path)
	{
		const size_t Slash = Path.find_last_of("/\\");
		FString Name = Slash == FString::npos ? Path : Path.substr(Slash + 1);
		const size_t Dot = Name.find_last_of('.');
		return Dot == FString::npos ? Name : Name.substr(0, Dot);
	}

	// 앵커 미리보기: 부모 사각형 안에 앵커 점(작은 사각형) 또는 늘어나는 영역(막대)을 그린다
	void DrawAnchorGlyph(ImDrawList* DL, const ImVec2& Min, float Size, const FVector4& A, ImU32 Color)
	{
		const float S = EditorIcons::GetScale();
		const float Inset = Size * 0.18f;
		const ImVec2 P0(Min.x + Inset, Min.y + Inset);
		const ImVec2 P1(Min.x + Size - Inset, Min.y + Size - Inset);
		DL->AddRect(P0, P1, ImGui::GetColorU32(ImGuiCol_Text, 0.45f), 1.0f * S);
		const float W = P1.x - P0.x, H = P1.y - P0.y;
		const float X0 = P0.x + W * A.X, X1 = P0.x + W * A.Z;
		const float Y0 = P0.y + H * A.Y, Y1 = P0.y + H * A.W;
		const float Bar = std::max(2.0f, 3.0f * S);
		const bool bStretchX = A.X != A.Z, bStretchY = A.Y != A.W;
		if (bStretchX && bStretchY)
			DL->AddRectFilled({ X0 + 2.0f * S, Y0 + 2.0f * S }, { X1 - 2.0f * S, Y1 - 2.0f * S }, Color, 1.0f * S);
		else if (bStretchX)
			DL->AddRectFilled({ X0 + 2.0f * S, Y0 - Bar * 0.5f }, { X1 - 2.0f * S, Y0 + Bar * 0.5f }, Color);
		else if (bStretchY)
			DL->AddRectFilled({ X0 - Bar * 0.5f, Y0 + 2.0f * S }, { X0 + Bar * 0.5f, Y1 - 2.0f * S }, Color);
		else
		{
			const float Half = 3.0f * S;
			const float CX = std::clamp(X0, P0.x + Half, P1.x - Half);
			const float CY = std::clamp(Y0, P0.y + Half, P1.y - Half);
			DL->AddRectFilled({ CX - Half, CY - Half }, { CX + Half, CY + Half }, Color);
		}
	}

	void ItemTooltip(const char* Text)
	{
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", Text);
	}
}

////////////////////////////////////////////////////////////
// Palette
////////////////////////////////////////////////////////////

FUIPalettePanel::FUIPalettePanel(FUIEditor& InEditor)
	: FUIEditorPanel(InEditor, "Palette", PanelIdPrefix)
{
}

void FUIPalettePanel::DrawContent()
{
	Editor.HandleShortcuts();
	const float S = EditorIcons::GetScale();

	EditorIcons::SearchBox("##PaletteSearch", Search, sizeof(Search), "Search Palette");
	ImGui::Dummy(ImVec2(0.0f, 2.0f * S));

	struct FEntry { UClass* Class; FWidgetTypeInfo Info; };
	TArray<FEntry> Entries;
	TArray<UClass*> Classes;
	GetDerivedClasses(UWidget::StaticClass(), Classes);
	for (UClass* Class : Classes)
	{
		// 생성할 수 없는 클래스, 베이스 패널, UUserWidget 계열은 뺀다.
		if (!Class->Constructor || Class == UPanelWidget::StaticClass() || Class->IsChildOf(UUserWidget::StaticClass()))
			continue;
		FEntry Entry{ Class, GetWidgetTypeInfo(Class) };
		if (EditorIcons::MatchesFilter(Entry.Info.Label.c_str(), Search) || EditorIcons::MatchesFilter(Class->Name.c_str(), Search))
			Entries.Add(Entry);
	}
	std::sort(Entries.begin(), Entries.end(), [](const FEntry& A, const FEntry& B) { return A.Info.Label < B.Info.Label; });

	ImGui::BeginChild("##PaletteList", ImVec2(0.0f, 0.0f));
	ImDrawList* DL = ImGui::GetWindowDrawList();
	const float RowH = ImGui::GetFontSize() + 9.0f * S;
	const float Width = ImGui::GetContentRegionAvail().x;
	const bool bSearching = Search[0] != '\0';

	for (int32 Category = 0; Category < Category_Count; ++Category)
	{
		const bool bHasAny = std::any_of(Entries.begin(), Entries.end(), [Category](const FEntry& E) { return E.Info.Category == Category; });
		if (!bHasAny) continue;

		// 카테고리 머리줄 (UE처럼 대문자 + 접기 화살표)
		ImGui::PushID(Category);
		const ImVec2 HeaderMin = ImGui::GetCursorScreenPos();
		if (ImGui::InvisibleButton("##Category", ImVec2(Width, RowH))) bCollapsed[Category] = !bCollapsed[Category];
		const bool bOpen = bSearching || !bCollapsed[Category];
		DL->AddRectFilled(HeaderMin, { HeaderMin.x + Width, HeaderMin.y + RowH },
			ImGui::IsItemHovered() ? IM_COL32(50, 50, 50, 255) : ColorCategoryBg);
		EditorIcons::Draw(DL, bOpen ? EEditorIcon::ChevronDown : EEditorIcon::ChevronRight,
			{ HeaderMin.x + 10.0f * S, HeaderMin.y + RowH * 0.5f }, 9.0f * S, IconGray());
		DL->AddText({ HeaderMin.x + 22.0f * S, HeaderMin.y + (RowH - ImGui::GetFontSize()) * 0.5f },
			ImGui::GetColorU32(ImGuiCol_Text, 0.9f), CategoryNames[Category]);
		ImGui::PopID();
		if (!bOpen) continue;

		for (const FEntry& Entry : Entries)
		{
			if (Entry.Info.Category != Category) continue;
			ImGui::PushID(Entry.Class);
			const ImVec2 Min = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton("##Item", ImVec2(Width, RowH))) Editor.AddWidget(Entry.Class);
			if (ImGui::IsItemHovered()) DL->AddRectFilled(Min, { Min.x + Width, Min.y + RowH }, IM_COL32(255, 255, 255, 14));
			ItemTooltip((Entry.Class->Name + "\nClick to add under the selected panel, or drag onto a panel in the Hierarchy.").c_str());
			if (ImGui::BeginDragDropSource())
			{
				UClass* Class = Entry.Class;
				ImGui::SetDragDropPayload(PalettePayload, &Class, sizeof(UClass*));
				ImGui::TextUnformatted(Entry.Info.Label.c_str());
				ImGui::EndDragDropSource();
			}
			EditorIcons::Draw(DL, Entry.Info.Icon, { Min.x + 34.0f * S, Min.y + RowH * 0.5f }, 14.0f * S, IconGray());
			DL->AddText({ Min.x + 48.0f * S, Min.y + (RowH - ImGui::GetFontSize()) * 0.5f },
				ImGui::GetColorU32(ImGuiCol_Text), Entry.Info.Label.c_str());
			ImGui::PopID();
		}
	}
	if (Entries.IsEmpty()) ImGui::TextDisabled("No widgets match \"%s\".", Search);
	ImGui::EndChild();
}

////////////////////////////////////////////////////////////
// Hierarchy
////////////////////////////////////////////////////////////

FUIHierarchyPanel::FUIHierarchyPanel(FUIEditor& InEditor)
	: FUIEditorPanel(InEditor, "Hierarchy", PanelIdPrefix)
{
}

void FUIHierarchyPanel::DrawContent()
{
	Editor.HandleShortcuts();
	const float S = EditorIcons::GetScale();

	EditorIcons::SearchBox("##HierarchySearch", Search, sizeof(Search), "Search Widgets");
	ImGui::Dummy(ImVec2(0.0f, 2.0f * S));

	ImGui::BeginChild("##HierarchyTree", ImVec2(0.0f, 0.0f));
	if (UPanelWidget* Root = Editor.GetRoot())
		DrawNode(Root, 0);
	ImGui::EndChild();
}

bool FUIHierarchyPanel::MatchesSearch(const UWidget* Widget) const
{
	if (EditorIcons::MatchesFilter(GetWidgetDisplayName(Widget).c_str(), Search)) return true;
	if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		for (UWidget* Child : Panel->GetChildren())
			if (MatchesSearch(Child)) return true;
	return false;
}

void FUIHierarchyPanel::DrawNode(UWidget* Widget, int32 Depth)
{
	if (Search[0] && !MatchesSearch(Widget)) return;

	const float S = EditorIcons::GetScale();
	UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
	const bool bIsRoot = Widget == Editor.GetRoot();

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth
		| ImGuiTreeNodeFlags_FramePadding;
	if (!Panel || Panel->GetChildCount() == 0) Flags |= ImGuiTreeNodeFlags_Leaf;
	if (Widget == Editor.GetSelected()) Flags |= ImGuiTreeNodeFlags_Selected;
	if (Search[0]) ImGui::SetNextItemOpen(true);

	// 라벨은 직접 그린다 (아이콘 + 이름). 루트는 UE처럼 [에셋 이름]
	const FString Name = bIsRoot ? "[" + GetAssetName(Editor.GetFilePath()) + "]" : GetWidgetDisplayName(Widget);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f * S, 3.0f * S));
	const bool bOpen = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(Widget->GetUUID())), Flags, "%s", "");
	ImGui::PopStyleVar();
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) Editor.SetSelected(Widget);

	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	const float MidY = (Min.y + Max.y) * 0.5f;
	float X = Min.x + ImGui::GetTreeNodeToLabelSpacing();
	ImDrawList* DL = ImGui::GetWindowDrawList();
	const bool bDimmed = Search[0] && !EditorIcons::MatchesFilter(Name.c_str(), Search);
	if (!bIsRoot)
	{
		EditorIcons::Draw(DL, GetWidgetTypeInfo(Widget->GetClass()).Icon, { X + 7.0f * S, MidY }, 14.0f * S, IconGray());
		X += 20.0f * S;
	}
	DL->AddText({ X, MidY - ImGui::GetFontSize() * 0.5f }, ImGui::GetColorU32(bDimmed ? ImGuiCol_TextDisabled : ImGuiCol_Text), Name.c_str());

	// 끌어서 다른 패널 아래로 옮긴다.
	if (!bIsRoot && ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("UI_WIDGET", &Widget, sizeof(UWidget*));
		ImGui::TextUnformatted(Name.c_str());
		ImGui::EndDragDropSource();
	}
	if (Panel && ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("UI_WIDGET"))
		{
			UWidget* Dropped = *static_cast<UWidget* const*>(Payload->Data);
			if (!IsSelfOrAncestor(Dropped, Panel))   // 자기 자손 아래로는 못 옮긴다
				Editor.RequestReparent(Dropped, Panel);
		}
		// 팔레트에서 끌어 온 위젯은 이 패널 아래에 만든다
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(PalettePayload))
		{
			UClass* Class = *static_cast<UClass* const*>(Payload->Data);
			if (UWidget* Added = Editor.AddWidget(Class))
				if (Added->GetParent() != Panel) Editor.RequestReparent(Added, Panel);
		}
		ImGui::EndDragDropTarget();
	}

	if (bOpen)
	{
		if (Panel)
			for (UWidget* Child : Panel->GetChildren())
				DrawNode(Child, Depth + 1);
		ImGui::TreePop();
	}
}

////////////////////////////////////////////////////////////
// Canvas (Designer)
////////////////////////////////////////////////////////////

FUICanvasPanel::FUICanvasPanel(FUIEditor& InEditor)
	: FUIEditorPanel(InEditor, "Designer", PanelIdPrefix)
{
}

void FUICanvasPanel::DrawContent()
{
	Editor.HandleShortcuts();

	// 텍스처가 아직 없어도 보였다고 알려야 다음 프레임에 미리보기가 그려진다.
	Editor.MarkCanvasVisible();

	const float S = EditorIcons::GetScale();
	const ImGuiIO& IO = ImGui::GetIO();
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	if (Avail.x <= 1.0f || Avail.y <= 1.0f)
	{
		Editor.SetCanvasDragging(false);
		return;
	}

	const float RulerSize = 20.0f * S;
	const ImVec2 AreaMin(Origin.x + RulerSize, Origin.y + RulerSize);
	const ImVec2 AreaMax(Origin.x + Avail.x, Origin.y + Avail.y);
	const FVector2& PreviewSize = Editor.GetPreviewSize();
	if (PreviewSize.X != LastPreviewSize.X || PreviewSize.Y != LastPreviewSize.Y)
	{
		LastPreviewSize = PreviewSize;
		bFitPending = true;
	}

	// 화면에 맞춤: 맞는 가장 큰 줌 단계 + 가운데 정렬
	if (bFitPending)
	{
		const float AreaW = AreaMax.x - AreaMin.x, AreaH = AreaMax.y - AreaMin.y;
		const float Fit = std::min((AreaW - 60.0f * S) / PreviewSize.X, (AreaH - 60.0f * S) / PreviewSize.Y);
		ZoomIndex = 0;
		for (int32 Index = 0; Index < IM_ARRAYSIZE(ZoomLevels); ++Index)
			if (ZoomLevels[Index] <= Fit) ZoomIndex = Index;
		const float Scale = ZoomLevels[ZoomIndex];
		Pan = ImVec2(std::floor((AreaW - PreviewSize.X * Scale) * 0.5f), std::floor((AreaH - PreviewSize.Y * Scale) * 0.5f));
		bFitPending = false;
	}

	ImDrawList* DL = ImGui::GetWindowDrawList();
	DL->ChannelsSplit(2);

	// 겹쳐 그리는 툴바를 먼저 내야 아래 캔버스 버튼보다 hover를 먼저 가져간다
	DL->ChannelsSetCurrent(1);
	DrawOverlayToolbar(AreaMin, AreaMax);
	DL->ChannelsSetCurrent(0);

	ImGui::SetCursorScreenPos(Origin);
	ImGui::InvisibleButton("##UICanvas", Avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool bItemHovered = ImGui::IsItemHovered();
	const bool bItemActive = ImGui::IsItemActive();
	const bool bInArea = IO.MousePos.x >= AreaMin.x && IO.MousePos.y >= AreaMin.y && IO.MousePos.x < AreaMax.x && IO.MousePos.y < AreaMax.y;

	// 휠 줌 (마우스 아래 지점 고정) · 오른쪽/가운데 드래그 팬
	if (bItemHovered && bInArea && IO.MouseWheel != 0.0f)
	{
		const float OldScale = ZoomLevels[ZoomIndex];
		ZoomIndex = std::clamp(ZoomIndex + (IO.MouseWheel > 0.0f ? 1 : -1), 0, static_cast<int32>(IM_ARRAYSIZE(ZoomLevels)) - 1);
		const float NewScale = ZoomLevels[ZoomIndex];
		const ImVec2 Local(IO.MousePos.x - AreaMin.x, IO.MousePos.y - AreaMin.y);
		Pan.x = Local.x - (Local.x - Pan.x) * NewScale / OldScale;
		Pan.y = Local.y - (Local.y - Pan.y) * NewScale / OldScale;
	}
	if (bItemActive && (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)))
	{
		Pan.x += IO.MouseDelta.x;
		Pan.y += IO.MouseDelta.y;
	}

	ViewScale = ZoomLevels[ZoomIndex];
	CanvasMin = ImVec2(std::floor(AreaMin.x + Pan.x), std::floor(AreaMin.y + Pan.y));
	const ImVec2 CanvasMax(CanvasMin.x + PreviewSize.X * ViewScale, CanvasMin.y + PreviewSize.Y * ViewScale);

	DL->AddRectFilled(Origin, AreaMax, ColorCanvasBg);
	DL->PushClipRect(AreaMin, AreaMax, true);

	// 격자 (디자인 좌표 기준, 화면에서 너무 촘촘하지 않은 간격)
	if (bShowGrid)
	{
		float Step = 8.0f;
		while (Step * ViewScale < 24.0f * S) Step *= 2.0f;
		const float ScreenStep = Step * ViewScale;
		const float FirstX = CanvasMin.x + std::floor((AreaMin.x - CanvasMin.x) / ScreenStep) * ScreenStep;
		const float FirstY = CanvasMin.y + std::floor((AreaMin.y - CanvasMin.y) / ScreenStep) * ScreenStep;
		for (float X = FirstX; X < AreaMax.x; X += ScreenStep)
		{
			const bool bMajor = static_cast<int64>(std::lround((X - CanvasMin.x) / ScreenStep)) % 4 == 0;
			DL->AddLine({ X, AreaMin.y }, { X, AreaMax.y }, bMajor ? ColorGridMajor : ColorGridMinor);
		}
		for (float Y = FirstY; Y < AreaMax.y; Y += ScreenStep)
		{
			const bool bMajor = static_cast<int64>(std::lround((Y - CanvasMin.y) / ScreenStep)) % 4 == 0;
			DL->AddLine({ AreaMin.x, Y }, { AreaMax.x, Y }, bMajor ? ColorGridMajor : ColorGridMinor);
		}
	}

	FTexture2D* Preview = Editor.GetPreviewTexture();
	if (Preview && Preview->GetSRV())
		DL->AddImage(Preview->GetSRV(), CanvasMin, CanvasMax);
	DL->AddRect({ CanvasMin.x - 1.0f, CanvasMin.y - 1.0f }, { CanvasMax.x + 1.0f, CanvasMax.y + 1.0f }, IM_COL32(110, 110, 110, 255));

	const UWidget* Selected = Editor.GetSelected();
	if (Selected && Selected != Editor.GetRoot())
	{
		const FUIRect& R = Selected->GetCachedRect();
		DL->AddRect(ToScreen(R.X, R.Y), ToScreen(R.X + R.W, R.Y + R.H), ColorSelection, 0.0f, 0, 2.0f);

		// 오른쪽 아래 크기 조절 손잡이
		const ImVec2 H = ToScreen(R.X + R.W, R.Y + R.H);
		DL->AddRectFilled(ImVec2(H.x - 5, H.y - 5), ImVec2(H.x + 5, H.y + 5), ColorSelection);

		// 앵커: 부모 영역 안의 점(마름모) 또는 영역(사각형)
		if (const UPanelWidget* Parent = Selected->GetParent())
		{
			const FUIRect& P = Parent->GetCachedRect();
			const FVector4& A = Selected->Anchors;
			const ImVec2 A0 = ToScreen(P.X + P.W * A.X, P.Y + P.H * A.Y);
			const ImVec2 A1 = ToScreen(P.X + P.W * A.Z, P.Y + P.H * A.W);
			if (A.X == A.Z && A.Y == A.W)
				DL->AddQuad(ImVec2(A0.x, A0.y - 7), ImVec2(A0.x + 7, A0.y), ImVec2(A0.x, A0.y + 7), ImVec2(A0.x - 7, A0.y), ColorSelection, 2.0f);
			else
				DL->AddRect(A0, A1, IM_COL32(255, 170, 40, 140));
		}
	}

	// 정보 글자 (UE 디자이너처럼 큰 회색 글자)
	ImFont* Font = ImGui::GetFont();
	const float BigSize = ImGui::GetFontSize() * 1.55f;
	const ImU32 InfoColor = IM_COL32(110, 110, 110, 255);
	char Text[64];
	if (ZoomIndex == ZoomOneIndex) std::snprintf(Text, sizeof(Text), "Zoom 1:1");
	else std::snprintf(Text, sizeof(Text), "Zoom %+d", ZoomIndex - ZoomOneIndex);
	DL->AddText(Font, BigSize, { AreaMin.x + 8.0f * S, AreaMin.y + 6.0f * S }, InfoColor, Text);

	const int32 W = static_cast<int32>(PreviewSize.X), H = static_cast<int32>(PreviewSize.Y);
	int32 A = W, B = H;
	while (B != 0) { const int32 T = A % B; A = B; B = T; }
	const int32 G = std::max(A, 1);
	std::snprintf(Text, sizeof(Text), "%d x %d (%d:%d)", W, H, W / G, H / G);
	DL->AddText(Font, BigSize, { AreaMin.x + 8.0f * S, AreaMax.y - BigSize - 6.0f * S }, InfoColor, Text);
	// 엔진 UI 배율 = 화면 높이 / 1080 (FUIGeometry::Scale)
	std::snprintf(Text, sizeof(Text), "DPI Scale %.2f", PreviewSize.Y / 1080.0f);
	const ImVec2 DpiSize = Font->CalcTextSizeA(BigSize, FLT_MAX, 0.0f, Text);
	DL->AddText(Font, BigSize, { AreaMax.x - DpiSize.x - 10.0f * S, AreaMax.y - BigSize - 6.0f * S }, InfoColor, Text);

	DL->PopClipRect();
	DrawRulers(Origin, AreaMin, AreaMax, RulerSize);
	DL->ChannelsMerge();

	HandleInput(bItemHovered && bInArea);
	Editor.SetCanvasDragging(DragMode != EDragMode::None);
}

void FUICanvasPanel::DrawRulers(const ImVec2& Origin, const ImVec2& AreaMin, const ImVec2& AreaMax, float RulerSize)
{
	const float S = EditorIcons::GetScale();
	ImDrawList* DL = ImGui::GetWindowDrawList();
	DL->AddRectFilled(Origin, { AreaMax.x, AreaMin.y }, ColorRulerBg);
	DL->AddRectFilled(Origin, { AreaMin.x, AreaMax.y }, ColorRulerBg);
	DL->AddLine({ AreaMin.x, AreaMin.y - 0.5f }, { AreaMax.x, AreaMin.y - 0.5f }, IM_COL32(14, 14, 14, 255));
	DL->AddLine({ AreaMin.x - 0.5f, AreaMin.y }, { AreaMin.x - 0.5f, AreaMax.y }, IM_COL32(14, 14, 14, 255));

	// 숫자 간격: 화면에서 80px 이상
	const float Steps[] = { 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000 };
	float Major = Steps[IM_ARRAYSIZE(Steps) - 1];
	for (float Candidate : Steps)
		if (Candidate * ViewScale >= 80.0f * S) { Major = Candidate; break; }
	const float Minor = Major / 5.0f;
	const float FontH = ImGui::GetFontSize();

	// 위
	DL->PushClipRect({ AreaMin.x, Origin.y }, { AreaMax.x, AreaMin.y }, true);
	for (float D = std::floor((AreaMin.x - CanvasMin.x) / ViewScale / Minor) * Minor;; D += Minor)
	{
		const float X = CanvasMin.x + D * ViewScale;
		if (X > AreaMax.x) break;
		const bool bMajor = std::fmod(std::abs(D), Major) < 0.5f;
		DL->AddLine({ X, AreaMin.y - (bMajor ? RulerSize : 5.0f * S) }, { X, AreaMin.y }, ColorRulerTick);
		if (bMajor)
		{
			char Label[16];
			std::snprintf(Label, sizeof(Label), "%d", static_cast<int32>(std::lround(D)));
			DL->AddText({ X + 3.0f * S, Origin.y + (RulerSize - FontH) * 0.5f - 1.0f * S }, ColorRulerText, Label);
		}
	}
	DL->PopClipRect();

	// 왼쪽 (UE처럼 숫자를 세로로 한 글자씩)
	DL->PushClipRect({ Origin.x, AreaMin.y }, { AreaMin.x, AreaMax.y }, true);
	for (float D = std::floor((AreaMin.y - CanvasMin.y) / ViewScale / Minor) * Minor;; D += Minor)
	{
		const float Y = CanvasMin.y + D * ViewScale;
		if (Y > AreaMax.y) break;
		const bool bMajor = std::fmod(std::abs(D), Major) < 0.5f;
		DL->AddLine({ AreaMin.x - (bMajor ? RulerSize : 5.0f * S), Y }, { AreaMin.x, Y }, ColorRulerTick);
		if (bMajor)
		{
			char Label[16];
			std::snprintf(Label, sizeof(Label), "%d", static_cast<int32>(std::lround(D)));
			float CharY = Y + 2.0f * S;
			for (const char* C = Label; *C; ++C)
			{
				const char Glyph[2] = { *C, '\0' };
				const float GlyphW = ImGui::CalcTextSize(Glyph).x;
				DL->AddText({ Origin.x + (RulerSize - GlyphW) * 0.5f - 2.0f * S, CharY }, ColorRulerText, Glyph);
				CharY += FontH * 0.85f;
			}
		}
	}
	DL->PopClipRect();

	DL->AddRectFilled(Origin, AreaMin, ColorRulerBg);
}

void FUICanvasPanel::DrawOverlayToolbar(const ImVec2& AreaMin, const ImVec2& AreaMax)
{
	const float S = EditorIcons::GetScale();
	ImDrawList* DL = ImGui::GetWindowDrawList();
	const float BtnH = 24.0f * S;
	const FVector2& PreviewSize = Editor.GetPreviewSize();

	char SizeLabel[32];
	std::snprintf(SizeLabel, sizeof(SizeLabel), "%d x %d", static_cast<int32>(PreviewSize.X), static_cast<int32>(PreviewSize.Y));
	const float GridW = EditorIcons::CalcButtonWidth(true, nullptr, false, BtnH);
	const float FitW = EditorIcons::CalcButtonWidth(true, nullptr, false, BtnH);
	const float SizeW = EditorIcons::CalcButtonWidth(true, "Screen Size", true, BtnH);
	const float Gap = 4.0f * S;
	const float TotalW = GridW + FitW + SizeW + Gap * 2.0f;
	float X = AreaMax.x - TotalW - 10.0f * S;
	const float Y = AreaMin.y + 8.0f * S;
	if (X < AreaMin.x + 120.0f * S) return;   // 너무 좁으면 숨긴다

	// 버튼 뒤 배경 칩
	DL->AddRectFilled({ X - 3.0f * S, Y - 3.0f * S }, { X + TotalW + 3.0f * S, Y + BtnH + 3.0f * S }, IM_COL32(20, 20, 20, 210), 5.0f * S);

	if (bShowGrid) DL->AddRectFilled({ X, Y }, { X + GridW, Y + BtnH }, ColorActiveButton, 3.0f * S);
	ImGui::SetCursorScreenPos({ X, Y });
	if (EditorIcons::Button("##DesignerGrid", EEditorIcon::Grid, bShowGrid ? IM_COL32_WHITE : IconGray(), nullptr, false, false, BtnH))
		bShowGrid = !bShowGrid;
	ItemTooltip("Show Grid");
	X += GridW + Gap;

	ImGui::SetCursorScreenPos({ X, Y });
	if (EditorIcons::Button("##DesignerFit", EEditorIcon::ZoomFit, IconGray(), nullptr, false, false, BtnH))
		bFitPending = true;
	ItemTooltip("Zoom to Fit");
	X += FitW + Gap;

	ImGui::SetCursorScreenPos({ X, Y });
	if (EditorIcons::Button("##DesignerScreenSize", EEditorIcon::Viewport, IconGray(), "Screen Size", true, false, BtnH))
	{
		ImGui::OpenPopup("##DesignerScreenSizeMenu");
		ImGui::SetNextWindowPos({ ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f });
	}
	ItemTooltip(SizeLabel);
	if (ImGui::BeginPopup("##DesignerScreenSizeMenu"))
	{
		EditorIcons::MenuSection("Screen Size");
		for (const FVector2& Size : ScreenSizes)
		{
			char Label[32];
			std::snprintf(Label, sizeof(Label), "%d x %d", static_cast<int32>(Size.X), static_cast<int32>(Size.Y));
			const bool bCurrent = Size.X == PreviewSize.X && Size.Y == PreviewSize.Y;
			if (EditorIcons::MenuRow(Label, Size.X >= Size.Y ? EEditorIcon::Viewport : EEditorIcon::LayoutSingle, bCurrent, true, false, nullptr, true))
				Editor.SetPreviewSize(Size);
		}
		ImGui::EndPopup();
	}
}

UWidget* FUICanvasPanel::PickWidget(UWidget* Widget, float X, float Y) const
{
	if (!Widget || Widget->IsCollapsed() || Widget->Visibility == static_cast<int32>(EUIVisibility::Hidden))
		return nullptr;

	if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		TArray<UWidget*> Sorted;
		Panel->GetSortedChildren(Sorted);
		for (auto It = Sorted.rbegin(); It != Sorted.rend(); ++It)
			if (UWidget* Hit = PickWidget(*It, X, Y))
				return Hit;
	}
	// 루트는 빈 곳 클릭으로 선택되지 않게 한다.
	return (Widget != Editor.GetRoot() && Widget->GetCachedRect().Contains(X, Y)) ? Widget : nullptr;
}

bool FUICanvasPanel::IsOverResizeHandle(float X, float Y) const
{
	const UWidget* Selected = Editor.GetSelected();
	if (!Selected) return false;
	const FUIRect& R = Selected->GetCachedRect();
	const float Grab = 8.0f / ViewScale;   // 화면에서 8픽셀
	return std::abs(X - (R.X + R.W)) < Grab && std::abs(Y - (R.Y + R.H)) < Grab;
}

void FUICanvasPanel::HandleInput(bool bCanvasHovered)
{
	const ImVec2 Mouse = ImGui::GetMousePos();
	const float X = (Mouse.x - CanvasMin.x) / ViewScale;   // 디자인 좌표
	const float Y = (Mouse.y - CanvasMin.y) / ViewScale;

	if (bCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		DragMode = EDragMode::None;
		if (IsOverResizeHandle(X, Y))
		{
			DragMode = EDragMode::Resize;
		}
		else
		{
			Editor.SetSelected(PickWidget(Editor.GetRoot(), X, Y));
			if (Editor.GetSelected())
				DragMode = EDragMode::Move;
		}

		if (UWidget* Selected = Editor.GetSelected())
		{
			DragStartX = X;
			DragStartY = Y;
			DragStartOffsets = Selected->Offsets;
			if (DragMode == EDragMode::Resize && Selected->bAutoSize)
			{
				// 자동 크기였다면 지금 보이는 크기에서 시작한다.
				const FUIRect& R = Selected->GetCachedRect();
				DragStartOffsets.Z = R.W;
				DragStartOffsets.W = R.H;
				Selected->bAutoSize = false;
			}
		}
	}

	// 되돌리기로 트리가 바뀌어 선택이 사라졌거나 마우스를 놓았으면 드래그를 끝낸다.
	if (!Editor.GetSelected() || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		DragMode = EDragMode::None;   // 놓는 순간 CommitUndoIfChanged가 스냅샷을 쌓는다
		return;
	}
	if (DragMode == EDragMode::None) return;

	float DX = X - DragStartX;
	float DY = Y - DragStartY;
	if (ImGui::GetIO().KeyShift)   // Shift: 8픽셀 격자에 맞춤
	{
		DX = std::round(DX / 8.0f) * 8.0f;
		DY = std::round(DY / 8.0f) * 8.0f;
	}
	ApplyDrag(DX, DY);
}

void FUICanvasPanel::ApplyDrag(float DX, float DY)
{
	UWidget* Selected = Editor.GetSelected();
	FVector4 O = DragStartOffsets;
	const FVector4& A = Selected->Anchors;
	const bool bStretchX = A.X != A.Z;
	const bool bStretchY = A.Y != A.W;

	if (DragMode == EDragMode::Move)
	{
		O.X += DX; if (bStretchX) O.Z -= DX;
		O.Y += DY; if (bStretchY) O.W -= DY;
	}
	else
	{
		// 늘어나는 축은 오른쪽/아래 여백이 줄고, 점 앵커 축은 폭/높이가 는다.
		O.Z += bStretchX ? -DX : DX;
		O.W += bStretchY ? -DY : DY;
	}
	Selected->Offsets = O;
}

////////////////////////////////////////////////////////////
// Details
////////////////////////////////////////////////////////////

FUIDetailsPanel::FUIDetailsPanel(FUIEditor& InEditor)
	: FUIEditorPanel(InEditor, "Details", PanelIdPrefix)
{
}

void FUIDetailsPanel::DrawContent()
{
	Editor.HandleShortcuts();
	const float S = EditorIcons::GetScale();

	UWidget* Selected = Editor.GetSelected();
	if (!Selected)
	{
		ImGui::TextDisabled("Select a widget in the Designer or Hierarchy.");
		return;
	}

	// 머리줄: 위젯 종류 아이콘 + 이름 칸 (UE처럼 맨 위)
	const FWidgetTypeInfo Info = GetWidgetTypeInfo(Selected->GetClass());
	const ImVec2 HeadMin = ImGui::GetCursorScreenPos();
	const float HeadH = ImGui::GetFrameHeight();
	EditorIcons::Draw(ImGui::GetWindowDrawList(), Info.Icon, { HeadMin.x + 9.0f * S, HeadMin.y + HeadH * 0.5f }, 16.0f * S, IconGray());
	ImGui::SetCursorScreenPos({ HeadMin.x + 24.0f * S, HeadMin.y });
	char NameBuffer[128];
	std::snprintf(NameBuffer, sizeof(NameBuffer), "%s", Selected->WidgetName.c_str());
	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ImGui::InputTextWithHint("##WidgetName", Info.Label.c_str(), NameBuffer, sizeof(NameBuffer)))
		Selected->WidgetName = NameBuffer;
	ImGui::TextDisabled("%s", Selected->GetClass()->Name.c_str());
	ImGui::Separator();

	// 앵커 프리셋: UE처럼 4x4 그림 칸
	if (Selected != Editor.GetRoot())
	{
		struct FAnchorPreset { const char* Label; FVector4 Anchors; };
		static const FAnchorPreset Presets[] = {
			{ "Top Left", {0, 0, 0, 0} },       { "Top Center", {0.5f, 0, 0.5f, 0} },       { "Top Right", {1, 0, 1, 0} },       { "Top Stretch", {0, 0, 1, 0} },
			{ "Center Left", {0, 0.5f, 0, 0.5f} }, { "Center", {0.5f, 0.5f, 0.5f, 0.5f} },  { "Center Right", {1, 0.5f, 1, 0.5f} }, { "Center Horizontal Stretch", {0, 0.5f, 1, 0.5f} },
			{ "Bottom Left", {0, 1, 0, 1} },    { "Bottom Center", {0.5f, 1, 0.5f, 1} },    { "Bottom Right", {1, 1, 1, 1} },    { "Bottom Stretch", {0, 1, 1, 1} },
			{ "Left Stretch", {0, 0, 0, 1} },   { "Center Vertical Stretch", {0.5f, 0, 0.5f, 1} }, { "Right Stretch", {1, 0, 1, 1} }, { "Fill", {0, 0, 1, 1} },
		};

		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Anchors");
		ImGui::SameLine(90.0f * S);
		const float GlyphBtn = 30.0f * S;
		const ImVec2 BtnMin = ImGui::GetCursorScreenPos();
		const bool bPressed = ImGui::InvisibleButton("##AnchorButton", { GlyphBtn + 16.0f * S, GlyphBtn });
		ImDrawList* DL = ImGui::GetWindowDrawList();
		DL->AddRectFilled(BtnMin, { BtnMin.x + GlyphBtn + 16.0f * S, BtnMin.y + GlyphBtn },
			ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), 3.0f * S);
		DrawAnchorGlyph(DL, BtnMin, GlyphBtn, Selected->Anchors, IM_COL32_WHITE);
		EditorIcons::Draw(DL, EEditorIcon::ChevronDown, { BtnMin.x + GlyphBtn + 7.0f * S, BtnMin.y + GlyphBtn * 0.5f }, 8.0f * S, IconGray());
		ItemTooltip("Anchor presets (keeps the widget where it is on screen)");
		if (bPressed)
		{
			ImGui::OpenPopup("##AnchorPresets");
			ImGui::SetNextWindowPos({ BtnMin.x, BtnMin.y + GlyphBtn + 2.0f });
		}
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("(%.2g, %.2g) - (%.2g, %.2g)", Selected->Anchors.X, Selected->Anchors.Y, Selected->Anchors.Z, Selected->Anchors.W);

		if (ImGui::BeginPopup("##AnchorPresets"))
		{
			EditorIcons::MenuSection("Anchors");
			const float Cell = 46.0f * S;
			for (int32 Index = 0; Index < IM_ARRAYSIZE(Presets); ++Index)
			{
				if (Index % 4 != 0) ImGui::SameLine(0.0f, 4.0f * S);
				ImGui::PushID(Index);
				const ImVec2 Min = ImGui::GetCursorScreenPos();
				if (ImGui::InvisibleButton("##Preset", { Cell, Cell }))
				{
					Editor.SetAnchorsKeepRect(Selected, Presets[Index].Anchors);
					ImGui::CloseCurrentPopup();
				}
				const bool bCurrent = Selected->Anchors.X == Presets[Index].Anchors.X && Selected->Anchors.Y == Presets[Index].Anchors.Y
					&& Selected->Anchors.Z == Presets[Index].Anchors.Z && Selected->Anchors.W == Presets[Index].Anchors.W;
				ImDrawList* PopupDL = ImGui::GetWindowDrawList();
				PopupDL->AddRectFilled(Min, { Min.x + Cell, Min.y + Cell },
					bCurrent ? ColorActiveButton : ImGui::IsItemHovered() ? IM_COL32(70, 70, 70, 255) : IM_COL32(45, 45, 45, 255), 3.0f * S);
				DrawAnchorGlyph(PopupDL, Min, Cell, Presets[Index].Anchors, IM_COL32_WHITE);
				ItemTooltip(Presets[Index].Label);
				ImGui::PopID();
			}
			EditorIcons::MenuNote("Changing anchors keeps the widget's current screen position.");
			ImGui::EndPopup();
		}
		ImGui::Separator();
	}

	FPropertyEditor::DrawObjectProperties(Selected);
}
