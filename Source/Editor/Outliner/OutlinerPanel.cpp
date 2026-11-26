#include "EnginePCH.h"
#include "Editor/Outliner/OutlinerPanel.h"
#include "Editor/HitoriEd/Editor.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Editor/EditorUI/ComponentTypeInfo.h"
#include "Editor/HitoriEd/LevelUndoHistory.h"
#include "Editor/HitoriEd/EditorActorUtils.h"
#include "Editor/HitoriEd/EditorFileUtils.h"

#include "Core/EngineLog.h"
#include "Engine/Level.h"
#include "Engine/World.h"

#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

namespace
{
    constexpr const char* ActorPayload = "OUTLINER_ACTOR";
    constexpr const char* FolderPayload = "OUTLINER_FOLDER";
    constexpr ImU32 FolderColor = IM_COL32(201, 169, 110, 255);   // 콘텐츠 드로어 폴더와 같은 색
    constexpr ImU32 WorldColor = IM_COL32(215, 215, 215, 255);

    // UE5 Outliner의 Type 열처럼 클래스 이름에서 접두사 A를 뗀다 ("AStaticMeshActor" -> "StaticMeshActor").
    const char* GetActorTypeName(const AActor* Actor)
    {
        const UClass* Class = Actor ? Actor->GetClass() : nullptr;
        if (!Class)
            return "";
        const char* Name = Class->Name.c_str();
        if (Name[0] == 'A' && std::isupper(static_cast<unsigned char>(Name[1])))
            return Name + 1;
        return Name;
    }

    bool IsActorVisible(const AActor* Actor)
    {
        const USceneComponent* Root = Actor ? Actor->GetRootComponent() : nullptr;
        return !Root || Root->IsVisible();
    }

    void SetActorVisible(AActor* Actor, bool bVisible)
    {
        if (USceneComponent* Root = Actor ? Actor->GetRootComponent() : nullptr)
            Root->SetVisibility(bVisible, true);
    }

    // 표 한 줄의 높이. 아이콘(15px)과 글자가 위아래 여유를 두고 들어가도록 UE5 줄 높이에 맞춘다.
    float GetOutlinerRowHeight()
    {
        return ImGui::GetTextLineHeight() + 9.0f * EditorIcons::GetScale();
    }

    // ---------------- 폴더 경로 ----------------
    // 폴더 경로는 "Lighting/Sky"처럼 /로 나눈다. 빈 문자열이 맨 위(월드 바로 아래)다.

    FString ParentFolder(const FString& Folder)
    {
        const size_t Slash = Folder.find_last_of('/');
        return Slash == FString::npos ? FString() : Folder.substr(0, Slash);
    }

    FString FolderLeaf(const FString& Folder)
    {
        const size_t Slash = Folder.find_last_of('/');
        return Slash == FString::npos ? Folder : Folder.substr(Slash + 1);
    }

    FString JoinFolder(const FString& Parent, const FString& Leaf)
    {
        return Parent.empty() ? Leaf : Parent + "/" + Leaf;
    }

    // Path가 Folder 자신이거나 그 안쪽인가
    bool IsSameOrInside(const FString& Path, const FString& Folder)
    {
        if (Folder.empty())
            return true;
        return Path == Folder || (Path.size() > Folder.size() && Path.compare(0, Folder.size(), Folder) == 0 &&
            Path[Folder.size()] == '/');
    }

    FString TrimSpaces(const FString& Text)
    {
        const size_t First = Text.find_first_not_of(" \t\r\n");
        if (First == FString::npos)
            return FString();
        const size_t Last = Text.find_last_not_of(" \t\r\n");
        return Text.substr(First, Last - First + 1);
    }

    // 레벨에 적힌 폴더 목록을 읽고 쓴다. 한 줄에 경로 하나다.
    std::set<FString> ReadStoredFolders(const ULevel* Level)
    {
        std::set<FString> Result;
        if (!Level)
            return Result;
        size_t Start = 0;
        const FString& Text = Level->EditorFolders;
        while (Start <= Text.size())
        {
            const size_t End = Text.find('\n', Start);
            const FString Line = TrimSpaces(Text.substr(Start, End == FString::npos ? FString::npos : End - Start));
            if (!Line.empty())
                Result.insert(Line);
            if (End == FString::npos)
                break;
            Start = End + 1;
        }
        return Result;
    }

    void WriteStoredFolders(ULevel* Level, const std::set<FString>& InFolders)
    {
        if (!Level)
            return;
        FString Text;
        for (const FString& Folder : InFolders)
        {
            if (Folder.empty()) continue;
            if (!Text.empty()) Text += '\n';
            Text += Folder;
        }
        Level->EditorFolders = Text;
    }

    // ---------------- 메뉴 모양 ----------------

    // UE5 메뉴의 분류 제목: 흐린 대문자 글자 오른쪽으로 얇은 선을 긋는다.
    void MenuSectionLine(const char* Title)
    {
        const float S = EditorIcons::GetScale();
        char Upper[64];
        size_t I = 0;
        for (; Title[I] && I + 1 < sizeof(Upper); ++I)
            Upper[I] = static_cast<char>(std::toupper(static_cast<unsigned char>(Title[I])));
        Upper[I] = '\0';

        ImGui::Dummy(ImVec2(0.0f, 2.0f * S));
        ImGui::TextDisabled("%s", Upper);
        const ImVec2 Min = ImGui::GetItemRectMin();
        const ImVec2 Max = ImGui::GetItemRectMax();
        const float LineY = (Min.y + Max.y) * 0.5f;
        const float Right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
        if (Right > Max.x + 8.0f * S)
            ImGui::GetWindowDrawList()->AddLine(ImVec2(Max.x + 8.0f * S, LineY), ImVec2(Right, LineY),
                ImGui::GetColorU32(ImGuiCol_Separator));
    }

    // 아이콘 자리를 비워 둔 라벨. 메뉴 항목 글자 앞에 아이콘을 그릴 공간이다.
    FString PadForIcon(const char* Label)
    {
        const float S = EditorIcons::GetScale();
        const float Need = 20.0f * S;
        const float Space = ImMax(1.0f, ImGui::CalcTextSize(" ").x);
        FString Padded(static_cast<size_t>(Need / Space) + 1, ' ');
        return Padded + Label;
    }

    void DrawMenuIcon(EEditorIcon Icon, ImU32 Color, bool bEnabled)
    {
        if (Icon == EEditorIcon::None)
            return;
        const float S = EditorIcons::GetScale();
        const ImVec2 Min = ImGui::GetItemRectMin();
        const ImVec2 Max = ImGui::GetItemRectMax();
        const float Size = 14.0f * S;
        const float Alpha = bEnabled ? 1.0f : 0.4f;
        const ImU32 Final = (Color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(((Color >> IM_COL32_A_SHIFT) & 0xFF) * Alpha) << IM_COL32_A_SHIFT);
        EditorIcons::Draw(ImGui::GetWindowDrawList(), Icon, ImVec2(Min.x + Size * 0.5f + 2.0f * S, (Min.y + Max.y) * 0.5f), Size, Final);
    }

    bool IconMenuItem(const char* Label, EEditorIcon Icon, const char* Shortcut = nullptr, bool bEnabled = true,
        ImU32 Color = IM_COL32(215, 215, 215, 255))
    {
        const FString Padded = PadForIcon(Label);
        const bool bPressed = ImGui::MenuItem(Padded.c_str(), Shortcut, false, bEnabled);
        DrawMenuIcon(Icon, Color, bEnabled);
        return bPressed;
    }

    bool IconBeginMenu(const char* Label, EEditorIcon Icon, bool bEnabled = true, ImU32 Color = IM_COL32(215, 215, 215, 255))
    {
        const FString Padded = PadForIcon(Label);
        const bool bOpen = ImGui::BeginMenu(Padded.c_str(), bEnabled);
        DrawMenuIcon(Icon, Color, bEnabled);
        return bOpen;
    }
}

bool FOutlinerPanel::Init()
{
    return true;
}

void FOutlinerPanel::Tick(float DeltaTime)
{
}

bool FOutlinerPanel::IsEditable() const
{
    return World && World->GetWorldType() == EWorldType::Editor;
}

void FOutlinerPanel::OnRender()
{
    if (!World)
        return;

    ULevel* Level = World->GetCurrentLevel();
    if (!Level)
        return;

    EditorPanelUI::BeginPanel("Outliner", GetOpenFlag());

    DrawToolbar(Level);

    const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    const bool bTyping = ImGui::GetIO().WantTextInput;

    // UE처럼 F2로 이름을 바꾸고, Delete로 선택한 폴더를 지운다 (액터 삭제는 에디터가 Delete로 처리한다).
    if (bFocused && !bTyping && IsEditable())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
        {
            if (bFolderSelected) BeginRenameFolder(SelectedFolder);
            else if (GEditor)    BeginRenameActor(GEditor->GetPrimarySelectedActor());
        }
        if (bFolderSelected && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            const FString Folder = SelectedFolder;
            Defer([this, Level, Folder]() { DeleteFolder(Level, Folder); });
        }
    }

    // 엔진에서 액터를 고르면 폴더 선택은 풀린다.
    if (bFolderSelected && GEditor && GEditor->GetSelectedActors()->Num() > 0)
        bFolderSelected = false;

    BuildRows(Level);
    bRenameDrawnThisFrame = false;
    DrawTable(Level);
    if ((RenamingActor || bRenamingFolder) && !bRenameDrawnThisFrame)
        CancelRename();

    // UE5처럼 맨 아래에 액터 수와 선택 수를 보여 준다.
    const int32 SelectedCount = GEditor ? GEditor->GetSelectedActors()->Num() : 0;
    ImGui::TextDisabled("%u actors (%d selected)", Level->GetActorNum(), SelectedCount);

    ImGui::End();

    // 표를 다 그린 뒤 미뤄 둔 편집을 한다.
    std::vector<std::function<void()>> Ops;
    Ops.swap(PendingOps);
    for (std::function<void()>& Op : Ops)
        Op();
}

// ---------------- 줄 목록 ----------------

void FOutlinerPanel::GatherFolders(ULevel* Level, std::set<FString>& OutFolders) const
{
    OutFolders = ReadStoredFolders(Level);
    for (AActor* Actor : Level->GetActors())
    {
        if (!Actor) continue;
        for (FString Folder = Actor->GetFolderPath(); !Folder.empty(); Folder = ParentFolder(Folder))
            OutFolders.insert(Folder);
    }
}

void FOutlinerPanel::BuildRows(ULevel* Level)
{
    Rows.clear();
    ChildFolders.clear();
    ActorsInFolder.clear();
    AttachedChildren.clear();
    GatherFolders(Level, Folders);

    for (const FString& Folder : Folders)
        ChildFolders[ParentFolder(Folder)].push_back(Folder);
    for (AActor* Actor : Level->GetActors())
    {
        if (!Actor) continue;
        // 같은 레벨의 액터에 붙어 있으면 그 액터 아래에, 아니면 자기 폴더에 둔다.
        AActor* Parent = Actor->GetAttachParentActor();
        if (Parent && Parent->GetLevel() == Level)
            AttachedChildren[Parent].push_back(Actor);
        else
            ActorsInFolder[Actor->GetFolderPath()].push_back(Actor);
    }

    // 검색 중이면 맞는 액터와 이름이 맞는 폴더, 그리고 그 위의 폴더만 남기고 모두 펼쳐 보여 준다.
    const bool bFiltering = SearchBuffer[0] != '\0';
    std::set<const AActor*> VisibleActors;
    std::set<FString> VisibleFolders;
    if (bFiltering)
    {
        for (AActor* Actor : Level->GetActors())
        {
            if (!Actor) continue;
            if (EditorIcons::MatchesFilter(Actor->GetName().c_str(), SearchBuffer) ||
                EditorIcons::MatchesFilter(GetActorTypeName(Actor), SearchBuffer))
            {
                // 붙어 있는 액터가 맞으면 부모 액터들과 그 폴더도 보여야 찾아 들어갈 수 있다.
                for (AActor* It = Actor; It; It = It->GetAttachParentActor())
                {
                    VisibleActors.insert(It);
                    for (FString Folder = It->GetFolderPath(); !Folder.empty(); Folder = ParentFolder(Folder))
                        VisibleFolders.insert(Folder);
                }
            }
        }
        for (const FString& Folder : Folders)
        {
            if (!EditorIcons::MatchesFilter(FolderLeaf(Folder).c_str(), SearchBuffer)) continue;
            for (FString It = Folder; !It.empty(); It = ParentFolder(It))
                VisibleFolders.insert(It);
        }
    }

    Rows.push_back({ ERowType::World, 0, FString(), nullptr, !Level->GetActors().IsEmpty() || !Folders.empty() });
    if (bWorldCollapsed && !bFiltering)
        return;

    std::function<void(AActor*, int32)> AddActor = [&](AActor* Actor, int32 Depth)
    {
        if (bFiltering && !VisibleActors.contains(Actor)) return;
        auto Children = AttachedChildren.find(Actor);
        const bool bHasChildren = Children != AttachedChildren.end() && !Children->second.empty();
        Rows.push_back({ ERowType::Actor, Depth, FString(), Actor, bHasChildren });
        if (bHasChildren && (bFiltering || !CollapsedActors.contains(Actor)))
            for (AActor* Child : Children->second)
                AddActor(Child, Depth + 1);
    };

    std::function<void(const FString&, int32)> AddContents = [&](const FString& Parent, int32 Depth)
    {
        if (auto It = ChildFolders.find(Parent); It != ChildFolders.end())
        {
            for (const FString& Folder : It->second)
            {
                if (bFiltering && !VisibleFolders.contains(Folder)) continue;
                const bool bHasChildren = ChildFolders.contains(Folder) || ActorsInFolder.contains(Folder);
                Rows.push_back({ ERowType::Folder, Depth, Folder, nullptr, bHasChildren });
                if (bFiltering || !CollapsedFolders.contains(Folder))
                    AddContents(Folder, Depth + 1);
            }
        }
        if (auto It = ActorsInFolder.find(Parent); It != ActorsInFolder.end())
        {
            for (AActor* Actor : It->second)
                AddActor(Actor, Depth);
        }
    };
    AddContents(FString(), 1);
}

// ---------------- 툴바 ----------------

void FOutlinerPanel::DrawToolbar(ULevel* Level)
{
    const float S = EditorIcons::GetScale();
    const float ButtonH = ImGui::GetFrameHeight() + 2.0f * S;
    const float ButtonW = EditorIcons::CalcButtonWidth(true, nullptr, false, ButtonH);

    // [검색 칸 .............] [새 폴더]
    EditorIcons::SearchBox("##OutlinerSearch", SearchBuffer, sizeof(SearchBuffer), "Search...",
        ImGui::GetContentRegionAvail().x - ButtonW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SameLine();
    ImGui::BeginDisabled(!IsEditable());
    if (EditorIcons::Button("##CreateFolder", EEditorIcon::Folder, FolderColor, nullptr, false, false, ButtonH))
        Defer([this, Level]() { CreateFolderForSelection(Level); });
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Create a new folder containing the current actor selection");
    // 폴더 아이콘 위에 작은 초록 +를 얹어 "새 폴더"임을 보인다.
    {
        const ImVec2 Max = ImGui::GetItemRectMax();
        const float PlusSize = 9.0f * S;
        EditorIcons::Draw(ImGui::GetWindowDrawList(), EEditorIcon::Plus,
            ImVec2(Max.x - PlusSize * 0.5f - 3.0f * S, Max.y - PlusSize * 0.5f - 3.0f * S), PlusSize, EditorIcons::PlayGreen);
    }
}

// ---------------- 표 ----------------

void FOutlinerPanel::DrawTable(ULevel* Level)
{
    const float S = EditorIcons::GetScale();
    const float FooterHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const ImGuiTableFlags TableFlags =
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_SizingStretchProp;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f * S, 0.0f));
    if (ImGui::BeginTable("##OutlinerTable", 3, TableFlags, ImVec2(0.0f, -FooterHeight)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##Visibility", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 20.0f * S);
        ImGui::TableSetupColumn("Item Label", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableHeadersRow();

        const float RowHeight = GetOutlinerRowHeight();
        ImGuiListClipper Clipper;
        Clipper.Begin(static_cast<int>(Rows.size()), RowHeight);
        // 이름을 바꾸는 줄은 화면 밖이어도 그려야 입력 칸이 살아 있다.
        for (int32 Index = 0; Index < static_cast<int32>(Rows.size()); ++Index)
        {
            const FRow& Row = Rows[Index];
            if ((Row.Type == ERowType::Actor && Row.Actor == RenamingActor && RenamingActor) ||
                (Row.Type == ERowType::Folder && bRenamingFolder && Row.Folder == RenamingFolder))
                Clipper.IncludeItemByIndex(Index);
        }
        while (Clipper.Step())
        {
            for (int32 Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; ++Index)
                DrawRow(Rows[Index], Level);
        }

        // 줄이 없는 빈 곳을 오른쪽 클릭하면 폴더 만들기 같은 메뉴를 띄운다.
        if (ImGui::BeginPopupContextWindow("##OutlinerBackground", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            DrawBackgroundContextMenu(Level);
            ImGui::EndPopup();
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

// 표의 한 줄. 줄 전체가 선택 영역이고, 왼쪽 눈 아이콘과 펼침 화살표는 그 위에 겹쳐 따로 눌린다.
void FOutlinerPanel::DrawRow(const FRow& Row, ULevel* Level)
{
    const float S = EditorIcons::GetScale();
    const float RowHeight = GetOutlinerRowHeight();
    const bool bEditable = IsEditable();

    switch (Row.Type)
    {
    case ERowType::World:  ImGui::PushID("##WorldRow"); break;
    case ERowType::Folder: ImGui::PushID(Row.Folder.c_str()); break;
    case ERowType::Actor:  ImGui::PushID(Row.Actor); break;
    }

    bool bSelected = false;
    bool bVisible = true;
    if (Row.Type == ERowType::Actor)
    {
        bSelected = GEditor && GEditor->GetSelectedActors()->IsSelected(Row.Actor);
        bVisible = IsActorVisible(Row.Actor);
    }
    else if (Row.Type == ERowType::Folder)
    {
        bSelected = bFolderSelected && SelectedFolder == Row.Folder;
    }

    ImGui::TableNextRow(ImGuiTableRowFlags_None, RowHeight);
    ImGui::TableSetColumnIndex(0);
    const ImVec2 RowStart = ImGui::GetCursorPos();
    // 줄 전체를 덮는 Selectable은 강조와 마우스 영역을 ItemSpacing.y의 절반만큼 위아래로 넓힌다.
    // 표의 칸 여백이 0이라 그만큼 이웃 줄과 겹쳐 보이고 마우스도 옆 줄을 집으므로, 이 줄에서만 간격을 0으로 둔다.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
    ImGui::Selectable("##Row", bSelected,
        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, RowHeight));
    ImGui::PopStyleVar();

    // ---- 클릭과 더블클릭 ----
    const bool bLeftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool bRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const bool bDoubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (Row.Type == ERowType::Actor)
    {
        // 왼쪽 클릭은 Ctrl/Shift로 여러 개를 고른다. 오른쪽 클릭은 UE처럼, 고르지 않은 액터일 때만 그 액터 하나를 고른다
        // (여러 개를 고른 채 오른쪽 클릭하면 선택을 그대로 둔다).
        if (bLeftClicked && GEditor)
        {
            HandleActorRowClick(Row.Actor);
        }
        else if (bRightClicked && GEditor && !bSelected)
        {
            GEditor->ClickSelectActor(Row.Actor, false, false);
            SelectionAnchor = Row.Actor;
        }
        if (bLeftClicked || bRightClicked)
            bFolderSelected = false;
        if (bDoubleClicked && bEditable)
            BeginRenameActor(Row.Actor);
    }
    else if (Row.Type == ERowType::Folder)
    {
        if (bLeftClicked || bRightClicked)
        {
            bFolderSelected = true;
            SelectedFolder = Row.Folder;
            if (GEditor && GEditor->GetSelectedActors()->Num() > 0)
                GEditor->SelectNone(true);
        }
        if (bDoubleClicked)
        {
            if (CollapsedFolders.contains(Row.Folder)) CollapsedFolders.erase(Row.Folder);
            else                                       CollapsedFolders.insert(Row.Folder);
        }
    }
    else if (bDoubleClicked)
    {
        bWorldCollapsed = !bWorldCollapsed;
    }

    // ---- 끌어서 놓기: 액터와 폴더를 다른 폴더(또는 월드 줄 = 맨 위)로 옮긴다 ----
    if (bEditable && Row.Type != ERowType::World && ImGui::BeginDragDropSource())
    {
        if (Row.Type == ERowType::Actor)
        {
            AActor* Dragged = Row.Actor;
            ImGui::SetDragDropPayload(ActorPayload, &Dragged, sizeof(AActor*));
            ImGui::TextUnformatted(Row.Actor->GetName().c_str());
        }
        else
        {
            ImGui::SetDragDropPayload(FolderPayload, Row.Folder.c_str(), Row.Folder.size() + 1);
            ImGui::Text("Folder %s", FolderLeaf(Row.Folder).c_str());
        }
        ImGui::EndDragDropSource();
    }
    if (bEditable && Row.Type != ERowType::Actor)
        AcceptDrop(Level, Row.Type == ERowType::Folder ? Row.Folder : FString());
    else if (bEditable)
        AcceptDropOnActor(Row.Actor);

    // ---- 오른쪽 클릭 메뉴 ----
    if (ImGui::BeginPopupContextItem("##RowContext"))
    {
        switch (Row.Type)
        {
        case ERowType::World:  DrawBackgroundContextMenu(Level); break;
        case ERowType::Folder: DrawFolderContextMenu(Row.Folder, Level); break;
        case ERowType::Actor:  DrawActorContextMenu(Row.Actor, Level); break;
        }
        ImGui::EndPopup();
    }

    // ---- 눈 아이콘: 액터, 또는 폴더·월드 안의 모든 액터를 보이거나 숨긴다 ----
    std::vector<AActor*> Targets;
    if (Row.Type == ERowType::Actor)
    {
        Targets.push_back(Row.Actor);
    }
    else
    {
        for (AActor* Actor : Level->GetActors())
            if (Actor && (Row.Type == ERowType::World || IsSameOrInside(Actor->GetFolderPath(), Row.Folder)))
                Targets.push_back(Actor);
        bVisible = Targets.empty() || std::any_of(Targets.begin(), Targets.end(), IsActorVisible);
    }

    ImGui::SetCursorPos(RowStart);
    const ImU32 EyeColor = ImGui::GetColorU32(ImGuiCol_Text, bVisible ? 0.75f : 0.35f);
    if (EditorIcons::Button("##Visibility", bVisible ? EEditorIcon::Eye : EEditorIcon::EyeClosed, EyeColor,
        nullptr, false, false, RowHeight))
    {
        for (AActor* Actor : Targets)
            SetActorVisible(Actor, !bVisible);
        if (!Targets.empty())
            FLevelUndoHistory::MarkDirty();
    }
    ImGui::SetItemTooltip(bVisible ? "Hide in the viewport" : "Show in the viewport");

    // ---- 이름 칸 ----
    ImGui::TableSetColumnIndex(1);
    DrawRowLabel(Row, RowHeight, !bVisible);

    // ---- 종류 칸 ----
    ImGui::TableSetColumnIndex(2);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (RowHeight - ImGui::GetTextLineHeight()) * 0.5f);
    switch (Row.Type)
    {
    case ERowType::World:  ImGui::TextDisabled("World"); break;
    case ERowType::Folder: ImGui::TextDisabled("Folder"); break;
    case ERowType::Actor:  ImGui::TextDisabled("%s", GetActorTypeName(Row.Actor)); break;
    }

    ImGui::PopID();
}

// 들여쓰기, 펼침 화살표, 아이콘, 이름(또는 이름 입력 칸)을 그린다.
void FOutlinerPanel::DrawRowLabel(const FRow& Row, float RowHeight, bool bDimmed)
{
    const float S = EditorIcons::GetScale();
    const float Indent = 14.0f * S;
    const float ArrowW = 14.0f * S;
    const float IconSize = 15.0f * S;

    const ImVec2 CellStart = ImGui::GetCursorScreenPos();
    const float MidY = CellStart.y + RowHeight * 0.5f;
    float X = CellStart.x + Row.Depth * Indent;
    ImDrawList* DL = ImGui::GetWindowDrawList();

    // 펼침 화살표. 줄 선택과 따로 눌린다.
    if (Row.bHasChildren)
    {
        const bool bOpen = SearchBuffer[0] != '\0' ||
            (Row.Type == ERowType::World ? !bWorldCollapsed :
             Row.Type == ERowType::Actor ? !CollapsedActors.contains(Row.Actor) : !CollapsedFolders.contains(Row.Folder));
        ImGui::SetCursorScreenPos(ImVec2(X, CellStart.y));
        if (ImGui::InvisibleButton("##Expand", ImVec2(ArrowW, RowHeight)))
        {
            if (Row.Type == ERowType::World)
                bWorldCollapsed = !bWorldCollapsed;
            else if (Row.Type == ERowType::Actor)
            {
                if (CollapsedActors.contains(Row.Actor)) CollapsedActors.erase(Row.Actor);
                else                                     CollapsedActors.insert(Row.Actor);
            }
            else if (CollapsedFolders.contains(Row.Folder))
                CollapsedFolders.erase(Row.Folder);
            else
                CollapsedFolders.insert(Row.Folder);
        }
        const ImU32 ArrowColor = ImGui::GetColorU32(ImGuiCol_Text, ImGui::IsItemHovered() ? 1.0f : 0.6f);
        EditorIcons::Draw(DL, bOpen ? EEditorIcon::ChevronDown : EEditorIcon::ChevronRight,
            ImVec2(X + ArrowW * 0.5f, MidY), 9.0f * S, ArrowColor);
    }
    X += ArrowW;

    // 아이콘
    EEditorIcon Icon = EEditorIcon::World;
    ImU32 IconColor = WorldColor;
    if (Row.Type == ERowType::World)
    {
        Icon = EEditorIcon::Level;
    }
    else if (Row.Type == ERowType::Folder)
    {
        Icon = EEditorIcon::Folder;
        IconColor = FolderColor;
    }
    else
    {
        ComponentTypeInfo::GetActorIcon(Row.Actor, Icon, IconColor);
    }
    const float Alpha = bDimmed ? 0.45f : 1.0f;
    EditorIcons::Draw(DL, Icon, ImVec2(X + IconSize * 0.5f, MidY), IconSize,
        (IconColor & ~IM_COL32_A_MASK) | (static_cast<ImU32>(((IconColor >> IM_COL32_A_SHIFT) & 0xFF) * Alpha) << IM_COL32_A_SHIFT));
    X += IconSize + 6.0f * S;

    // 이름을 바꾸는 중이면 입력 칸을 그린다.
    const bool bRenamingThis = (Row.Type == ERowType::Actor && RenamingActor && Row.Actor == RenamingActor) ||
        (Row.Type == ERowType::Folder && bRenamingFolder && Row.Folder == RenamingFolder);
    if (bRenamingThis)
    {
        bRenameDrawnThisFrame = true;
        ImGui::SetCursorScreenPos(ImVec2(X, CellStart.y + (RowHeight - ImGui::GetFrameHeight()) * 0.5f));
        if (bFocusRename)
        {
            ImGui::SetKeyboardFocusHere();
            bFocusRename = false;
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool bConfirmed = ImGui::InputText("##Rename", RenameBuffer, sizeof(RenameBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        // 다른 곳을 누르면 확정한다. Esc는 ImGui가 원래 이름으로 되돌린 뒤 비활성으로 만들므로 같은 이름이 되어 아무 일도 없다.
        if (bConfirmed || ImGui::IsItemDeactivated())
        {
            const FString NewName = RenameBuffer;
            if (Row.Type == ERowType::Actor)
            {
                AActor* Actor = Row.Actor;
                Defer([Actor, NewName]() { FEditorActorUtils::RenameActor(Actor, NewName); });
            }
            else
            {
                ULevel* Level = World ? World->GetCurrentLevel() : nullptr;
                const FString Folder = Row.Folder;
                Defer([this, Level, Folder, NewName]() { RenameFolder(Level, Folder, NewName); });
            }
            CancelRename();
        }
        return;
    }

    FString Label;
    if (Row.Type == ERowType::World)
    {
        // UE5처럼 "씬 이름 (Editor)". 저장하지 않은 새 씬은 Untitled다.
        const FString& Path = FEditorFileUtils::CurrentScenePath;
        Label = Path.empty() ? FString("Untitled") : std::filesystem::path(Path).stem().string();
        Label += IsEditable() ? " (Editor)" : " (PIE)";
    }
    else if (Row.Type == ERowType::Folder)
    {
        Label = FolderLeaf(Row.Folder);
    }
    else
    {
        Label = Row.Actor->GetName();
    }
    ImU32 TextColor = ImGui::GetColorU32(bDimmed ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    // UE처럼 플레이 중에 생겨난 액터(GameMode, Pawn, PlayerController 등)는 노란 글씨로 보여 준다.
    if (Row.Type == ERowType::Actor && Row.Actor->WasSpawnedDuringPlay())
        TextColor = bDimmed ? IM_COL32(242, 196, 64, 115) : IM_COL32(242, 196, 64, 255);
    DL->AddText(ImVec2(X, MidY - ImGui::GetFontSize() * 0.5f), TextColor, Label.c_str());
    // 표 칸이 줄 높이를 알도록 자리를 잡아 둔다.
    ImGui::SetCursorScreenPos(CellStart);
    ImGui::Dummy(ImVec2(0.0f, RowHeight));
}

void FOutlinerPanel::AcceptDrop(ULevel* Level, const FString& TargetFolder)
{
    if (!ImGui::BeginDragDropTarget())
        return;

    if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(ActorPayload))
    {
        AActor* Actor = *static_cast<AActor* const*>(Payload->Data);
        Defer([this, Level, Actor, TargetFolder]() { MoveActorToFolder(Level, Actor, TargetFolder); });
    }
    if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(FolderPayload))
    {
        const FString Folder(static_cast<const char*>(Payload->Data));
        Defer([this, Level, Folder, TargetFolder]() { MoveFolder(Level, Folder, TargetFolder); });
    }
    ImGui::EndDragDropTarget();
}

void FOutlinerPanel::AcceptDropOnActor(AActor* TargetActor)
{
    if (!TargetActor || !ImGui::BeginDragDropTarget())
        return;
    // 자기 자신이나 자기 자식 위에서는 놓을 자리를 표시하지 않는다.
    if (const ImGuiPayload* Peek = ImGui::GetDragDropPayload(); Peek && Peek->IsDataType(ActorPayload))
    {
        const AActor* Dragged = *static_cast<AActor* const*>(Peek->Data);
        if (FEditorActorUtils::IsSelfOrAttachedDescendant(TargetActor, Dragged))
        {
            ImGui::EndDragDropTarget();
            return;
        }
    }
    if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(ActorPayload))
    {
        AActor* Child = *static_cast<AActor* const*>(Payload->Data);
        Defer([this, Child, TargetActor]()
        {
            if (FEditorActorUtils::AttachActor(Child, TargetActor))
                CollapsedActors.erase(TargetActor);
        });
    }
    ImGui::EndDragDropTarget();
}

// ---------------- 오른쪽 클릭 메뉴 ----------------

void FOutlinerPanel::DrawActorContextMenu(AActor* Actor, ULevel* Level)
{
    const bool bEditable = IsEditable();

    MenuSectionLine("View Options");
    if (IconMenuItem("Frame Selected", EEditorIcon::Camera, "F") && GEditor)
        GEditor->FocusSelection();

    MenuSectionLine("Actor Options");
    if (IconBeginMenu("Edit", EEditorIcon::Settings))
    {
        if (ImGui::MenuItem("Cut", "Ctrl+X", false, bEditable) && GEditor)
        {
            GEditor->CopySelectedActor();
            Defer([this, Actor]() { if (DeleteCallback) DeleteCallback(Actor); });
        }
        if (ImGui::MenuItem("Copy", "Ctrl+C") && GEditor)
            GEditor->CopySelectedActor();
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, GEditor && GEditor->CanPasteActor()))
            Defer([]() { if (GEditor) GEditor->PasteActor(); });
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, bEditable))
            Defer([]() { if (GEditor) GEditor->DuplicateSelectedActor(); });
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Delete", false, bEditable))
            Defer([this, Actor]() { if (DeleteCallback) DeleteCallback(Actor); });
        if (ImGui::MenuItem("Rename", "F2", false, bEditable))
            BeginRenameActor(Actor);
        ImGui::EndMenu();
    }
    if (IconBeginMenu("Visibility", EEditorIcon::Eye))
    {
        const bool bVisible = IsActorVisible(Actor);
        if (ImGui::MenuItem(bVisible ? "Hide Selected" : "Show Selected", "H"))
        {
            SetActorVisible(Actor, !bVisible);
            FLevelUndoHistory::MarkDirty();
        }
        if (ImGui::MenuItem("Show All Actors"))
        {
            for (AActor* Other : Level->GetActors())
                SetActorVisible(Other, true);
            FLevelUndoHistory::MarkDirty();
        }
        ImGui::EndMenu();
    }

    if (IconBeginMenu("Attach To", EEditorIcon::Translate, bEditable))
    {
        DrawAttachToMenu(Level, Actor);
        ImGui::EndMenu();
    }
    else
    {
        AttachSearch[0] = '\0';
    }
    if (IconMenuItem("Detach", EEditorIcon::Reset, nullptr, bEditable && Actor->GetAttachParentActor() != nullptr))
        Defer([Actor]() { FEditorActorUtils::DetachActor(Actor); });

    MenuSectionLine("Outliner");
    if (IconBeginMenu("Move To", EEditorIcon::Folder, bEditable, FolderColor))
    {
        DrawMoveToMenu(Level, Actor, nullptr);
        ImGui::EndMenu();
    }
}

void FOutlinerPanel::DrawAttachToMenu(ULevel* Level, AActor* Actor)
{
    const float S = EditorIcons::GetScale();
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    EditorIcons::SearchBox("##AttachSearch", AttachSearch, sizeof(AttachSearch), "Search actors", 220.0f * S);

    // 액터가 수천 개일 수 있어 앞쪽 몇 개만 보여 주고, 나머지는 검색으로 찾게 한다.
    constexpr int32 MaxShown = 40;
    int32 Shown = 0;
    int32 Hidden = 0;
    AActor* const CurrentParent = Actor->GetAttachParentActor();
    for (AActor* Candidate : Level->GetActors())
    {
        // 자기 자신과 자기에게 붙은 자식에는 붙일 수 없다 (순환).
        if (!Candidate || FEditorActorUtils::IsSelfOrAttachedDescendant(Candidate, Actor))
            continue;
        if (!EditorIcons::MatchesFilter(Candidate->GetName().c_str(), AttachSearch))
            continue;
        if (Shown >= MaxShown)
        {
            ++Hidden;
            continue;
        }
        ++Shown;

        EEditorIcon Icon;
        ImU32 Color;
        ComponentTypeInfo::GetActorIcon(Candidate, Icon, Color);
        ImGui::PushID(Candidate);
        if (IconMenuItem(Candidate->GetName().c_str(), Icon, Candidate == CurrentParent ? "current" : nullptr,
            Candidate != CurrentParent, Color))
        {
            Defer([this, Actor, Candidate]()
            {
                if (FEditorActorUtils::AttachActor(Actor, Candidate))
                    CollapsedActors.erase(Candidate);
            });
        }
        ImGui::PopID();
    }
    if (Shown == 0)
        ImGui::TextDisabled("No actors match");
    else if (Hidden > 0)
        ImGui::TextDisabled("%d more... type to search", Hidden);
}

void FOutlinerPanel::DrawFolderContextMenu(const FString& Folder, ULevel* Level)
{
    const bool bEditable = IsEditable();

    MenuSectionLine("Hierarchy");
    if (IconMenuItem("Create Sub Folder", EEditorIcon::Folder, nullptr, bEditable, FolderColor))
    {
        Defer([this, Level, Folder]()
        {
            CollapsedFolders.erase(Folder);
            BeginRenameFolder(CreateFolder(Level, Folder));
        });
    }
    if (IconBeginMenu("Edit", EEditorIcon::Settings, bEditable))
    {
        if (ImGui::MenuItem("Rename", "F2"))
            BeginRenameFolder(Folder);
        if (ImGui::MenuItem("Delete", "Delete"))
            Defer([this, Level, Folder]() { DeleteFolder(Level, Folder); });
        ImGui::EndMenu();
    }

    MenuSectionLine("Outliner");
    if (IconBeginMenu("Move To", EEditorIcon::Folder, bEditable, FolderColor))
    {
        DrawMoveToMenu(Level, nullptr, &Folder);
        ImGui::EndMenu();
    }
}

void FOutlinerPanel::DrawBackgroundContextMenu(ULevel* Level)
{
    const bool bEditable = IsEditable();

    MenuSectionLine("Hierarchy");
    if (IconMenuItem("Create Folder", EEditorIcon::Folder, nullptr, bEditable, FolderColor))
        Defer([this, Level]() { BeginRenameFolder(CreateFolder(Level, FString())); });
    if (IconMenuItem("World Settings", EEditorIcon::World, nullptr, OpenWorldSettingsCallback != nullptr) && OpenWorldSettingsCallback)
        OpenWorldSettingsCallback();
    if (IconMenuItem("Cleanup Folders", EEditorIcon::Reset, nullptr, bEditable))
        Defer([this, Level]() { CleanupFolders(Level); });
    if (IconBeginMenu("Edit", EEditorIcon::Settings))
    {
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, GEditor && GEditor->CanPasteActor()))
            Defer([]() { if (GEditor) GEditor->PasteActor(); });
        ImGui::EndMenu();
    }
}

void FOutlinerPanel::DrawMoveToMenu(ULevel* Level, AActor* MovingActor, const FString* MovingFolder)
{
    // 지금 있는 곳과, 폴더를 옮길 때 자기 자신과 그 안쪽은 고를 수 없다.
    const FString Current = MovingActor ? MovingActor->GetFolderPath() : ParentFolder(*MovingFolder);
    auto CanMoveInto = [&](const FString& Target)
    {
        if (Target == Current) return false;
        return !(MovingFolder && IsSameOrInside(Target, *MovingFolder));
    };
    auto MoveInto = [this, Level, MovingActor, MovingFolder](const FString& Target)
    {
        if (MovingActor)
        {
            AActor* Actor = MovingActor;
            Defer([this, Level, Actor, Target]() { MoveActorToFolder(Level, Actor, Target); });
        }
        else
        {
            const FString Folder = *MovingFolder;
            Defer([this, Level, Folder, Target]() { MoveFolder(Level, Folder, Target); });
        }
    };

    if (IconMenuItem("None", EEditorIcon::Level, nullptr, CanMoveInto(FString())))
        MoveInto(FString());

    if (!Folders.empty())
    {
        MenuSectionLine("Folders");
        for (const FString& Folder : Folders)
        {
            int32 Depth = 0;
            for (char C : Folder) Depth += (C == '/');
            ImGui::PushID(Folder.c_str());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Depth * 12.0f * EditorIcons::GetScale());
            if (IconMenuItem(FolderLeaf(Folder).c_str(), EEditorIcon::Folder, nullptr, CanMoveInto(Folder), FolderColor))
                MoveInto(Folder);
            ImGui::PopID();
        }
    }

    ImGui::Separator();
    if (IconMenuItem("Create New Folder", EEditorIcon::Plus, nullptr, true, EditorIcons::PlayGreen))
    {
        // 새 폴더는 옮길 대상이 있던 곳에 만든다. 폴더를 옮길 때는 그 폴더의 부모에 만든다.
        const FString Parent = Current;
        AActor* Actor = MovingActor;
        const FString MovingPath = MovingFolder ? *MovingFolder : FString();
        Defer([this, Level, Parent, Actor, MovingPath]()
        {
            const FString NewFolder = CreateFolder(Level, Parent);
            if (Actor) MoveActorToFolder(Level, Actor, NewFolder);
            else       MoveFolder(Level, MovingPath, NewFolder);
            BeginRenameFolder(NewFolder);
        });
    }
}

// ---------------- 폴더 편집 ----------------

FString FOutlinerPanel::CreateFolder(ULevel* Level, const FString& Parent)
{
    if (!Level || !IsEditable())
        return FString();

    std::set<FString> Existing;
    GatherFolders(Level, Existing);
    FString Path = JoinFolder(Parent, "NewFolder");
    for (int32 Suffix = 1; Existing.contains(Path); ++Suffix)
        Path = JoinFolder(Parent, "NewFolder" + std::to_string(Suffix));

    std::set<FString> Stored = ReadStoredFolders(Level);
    Stored.insert(Path);
    WriteStoredFolders(Level, Stored);
    if (!Parent.empty())
        CollapsedFolders.erase(Parent);
    FLevelUndoHistory::MarkDirty();
    return Path;
}

void FOutlinerPanel::HandleActorRowClick(AActor* Actor)
{
    const ImGuiIO& IO = ImGui::GetIO();
    if (IO.KeyShift && !IO.KeyCtrl && SelectionAnchor)
    {
        // 기준 줄과 이 줄 사이의 액터 줄을 모두 고른다. 기준 줄이 지금 보이지 않으면 이 액터만 더한다.
        int32 AnchorIndex = -1, ClickIndex = -1;
        for (int32 Index = 0; Index < static_cast<int32>(Rows.size()); ++Index)
        {
            if (Rows[Index].Type != ERowType::Actor) continue;
            if (Rows[Index].Actor == SelectionAnchor) AnchorIndex = Index;
            if (Rows[Index].Actor == Actor) ClickIndex = Index;
        }
        if (AnchorIndex >= 0 && ClickIndex >= 0)
        {
            const int32 First = std::min(AnchorIndex, ClickIndex);
            const int32 Last = std::max(AnchorIndex, ClickIndex);
            GEditor->SelectNone(false);
            for (int32 Index = First; Index <= Last; ++Index)
                if (Rows[Index].Type == ERowType::Actor && Rows[Index].Actor != Actor)
                    GEditor->SelectActor(Rows[Index].Actor, true, false);
            GEditor->SelectActor(Actor, true, false);   // 누른 줄이 주 선택
            GEditor->NoteSelectionChange();
            return;
        }
        GEditor->ClickSelectActor(Actor, false, true);
        return;
    }

    GEditor->ClickSelectActor(Actor, IO.KeyCtrl, false);
    SelectionAnchor = Actor;
}

void FOutlinerPanel::CreateFolderForSelection(ULevel* Level)
{
    // 고른 액터를 모두 새 폴더로 옮긴다. 새 폴더는 주 선택 액터가 있던 폴더 아래에 만든다.
    AActor* Selected = GEditor ? GEditor->GetPrimarySelectedActor() : nullptr;
    if (Selected && Selected->GetLevel() == Level)
    {
        const FString NewFolder = CreateFolder(Level, Selected->GetFolderPath());
        TArray<AActor*> Actors;
        GEditor->GetSelectedActors()->GetSelectedObjects(Actors);
        for (AActor* Each : Actors)
            if (Each && Each->GetLevel() == Level)
                MoveActorToFolder(Level, Each, NewFolder);
        BeginRenameFolder(NewFolder);
        return;
    }
    BeginRenameFolder(CreateFolder(Level, bFolderSelected ? SelectedFolder : FString()));
}

bool FOutlinerPanel::RenameFolder(ULevel* Level, const FString& Folder, const FString& NewLeafName)
{
    if (!Level || !IsEditable() || Folder.empty())
        return false;

    const FString Leaf = TrimSpaces(NewLeafName);
    if (Leaf.empty() || Leaf.find('/') != FString::npos)
    {
        HTR_LOG(Warning, "Rename: folder name cannot be empty or contain '/'");
        return false;
    }
    const FString NewPath = JoinFolder(ParentFolder(Folder), Leaf);
    if (NewPath == Folder)
        return true;

    std::set<FString> Existing;
    GatherFolders(Level, Existing);
    if (Existing.contains(NewPath))
    {
        HTR_LOG(Warning, "Rename: a folder named '{}' already exists here", Leaf);
        return false;
    }

    RemapFolderPrefix(Level, Folder, NewPath);
    return true;
}

void FOutlinerPanel::DeleteFolder(ULevel* Level, const FString& Folder)
{
    if (!Level || !IsEditable() || Folder.empty())
        return;
    // 안의 것은 한 단계 위로 올리고 폴더만 없앤다 (UE와 같다).
    RemapFolderPrefix(Level, Folder, ParentFolder(Folder));
    if (bFolderSelected && SelectedFolder == Folder)
        bFolderSelected = false;
}

void FOutlinerPanel::MoveFolder(ULevel* Level, const FString& Folder, const FString& NewParent)
{
    if (!Level || !IsEditable() || Folder.empty())
        return;
    if (IsSameOrInside(NewParent, Folder) && !NewParent.empty())
        return;   // 자기 자신이나 자기 안쪽으로는 못 옮긴다
    const FString NewPath = JoinFolder(NewParent, FolderLeaf(Folder));
    if (NewPath == Folder)
        return;
    // 같은 이름의 폴더가 이미 있으면 그 폴더와 합쳐진다.
    RemapFolderPrefix(Level, Folder, NewPath);
    if (!NewParent.empty())
        CollapsedFolders.erase(NewParent);
}

void FOutlinerPanel::MoveActorToFolder(ULevel* Level, AActor* Actor, const FString& Folder)
{
    if (!Level || !Actor || !IsEditable())
        return;
    // 폴더로 옮기면 붙어 있던 부모에서 뗀다 (UE와 같다). 위치는 그대로 남는다.
    FEditorActorUtils::DetachActor(Actor);
    if (Actor->GetFolderPath() == Folder)
        return;
    // 폴더는 액터가 빠져나가도 남도록 목록에 적어 둔다.
    if (!Folder.empty())
    {
        std::set<FString> Stored = ReadStoredFolders(Level);
        for (FString It = Folder; !It.empty(); It = ParentFolder(It))
            Stored.insert(It);
        WriteStoredFolders(Level, Stored);
        CollapsedFolders.erase(Folder);
    }
    {
        // 원래 있던 폴더도 비더라도 남긴다.
        std::set<FString> Stored = ReadStoredFolders(Level);
        for (FString It = Actor->GetFolderPath(); !It.empty(); It = ParentFolder(It))
            Stored.insert(It);
        WriteStoredFolders(Level, Stored);
    }
    Actor->SetFolderPath(Folder);
    FLevelUndoHistory::MarkDirty();
}

void FOutlinerPanel::CleanupFolders(ULevel* Level)
{
    if (!Level || !IsEditable())
        return;
    std::set<FString> Kept;
    for (AActor* Actor : Level->GetActors())
    {
        if (!Actor) continue;
        for (FString It = Actor->GetFolderPath(); !It.empty(); It = ParentFolder(It))
            Kept.insert(It);
    }
    const std::set<FString> Stored = ReadStoredFolders(Level);
    if (Stored != Kept)
    {
        WriteStoredFolders(Level, Kept);
        FLevelUndoHistory::MarkDirty();
    }
    if (bFolderSelected && !Kept.contains(SelectedFolder))
        bFolderSelected = false;
}

void FOutlinerPanel::RemapFolderPrefix(ULevel* Level, const FString& OldPrefix, const FString& NewPrefix)
{
    auto Remap = [&](const FString& Path) -> FString
    {
        if (!IsSameOrInside(Path, OldPrefix))
            return Path;
        const FString Rest = Path.size() > OldPrefix.size() ? Path.substr(OldPrefix.size() + 1) : FString();
        if (Rest.empty()) return NewPrefix;
        return NewPrefix.empty() ? Rest : NewPrefix + "/" + Rest;
    };

    for (AActor* Actor : Level->GetActors())
        if (Actor && IsSameOrInside(Actor->GetFolderPath(), OldPrefix) && !Actor->GetFolderPath().empty())
            Actor->SetFolderPath(Remap(Actor->GetFolderPath()));

    std::set<FString> Stored;
    for (const FString& Folder : ReadStoredFolders(Level))
    {
        const FString Mapped = Remap(Folder);
        if (!Mapped.empty()) Stored.insert(Mapped);
    }
    WriteStoredFolders(Level, Stored);

    std::set<FString> Collapsed;
    for (const FString& Folder : CollapsedFolders)
        Collapsed.insert(Remap(Folder));
    CollapsedFolders.swap(Collapsed);

    if (bFolderSelected)
    {
        SelectedFolder = Remap(SelectedFolder);
        bFolderSelected = !SelectedFolder.empty();
    }
    FLevelUndoHistory::MarkDirty();
}

// ---------------- 이름 바꾸기 ----------------

void FOutlinerPanel::BeginRenameActor(AActor* Actor)
{
    if (!Actor || !IsEditable())
        return;
    CancelRename();
    RenamingActor = Actor;
    strncpy_s(RenameBuffer, sizeof(RenameBuffer), Actor->GetName().c_str(), _TRUNCATE);
    bFocusRename = true;
}

void FOutlinerPanel::BeginRenameFolder(const FString& Folder)
{
    if (Folder.empty() || !IsEditable())
        return;
    CancelRename();
    bRenamingFolder = true;
    RenamingFolder = Folder;
    bFolderSelected = true;
    SelectedFolder = Folder;
    strncpy_s(RenameBuffer, sizeof(RenameBuffer), FolderLeaf(Folder).c_str(), _TRUNCATE);
    bFocusRename = true;
}

void FOutlinerPanel::CancelRename()
{
    RenamingActor = nullptr;
    bRenamingFolder = false;
    RenamingFolder.clear();
    bFocusRename = false;
}
