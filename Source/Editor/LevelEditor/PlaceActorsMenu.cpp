#include "EnginePCH.h"
#include "PlaceActorsMenu.h"

#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "UObject/UObjectHash.h"
#include "Core/Misc/Paths.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>

namespace
{
	constexpr int32 MaxRecent = 5;

	// ---------------- 놓을 수 있는 액터 목록 ----------------

	struct FPresetActor
	{
		const char* ClassName;
		const char* Label;
		EEditorIcon Icon;
		const char* MeshPath;   // Static Mesh Actor에 넣을 기본 메시 (없으면 nullptr)
	};

	struct FPlaceCategory
	{
		const char* Name;
		EEditorIcon Icon;
		std::initializer_list<FPresetActor> Actors;
	};

	// UE5 PLACE ACTORS 분류. 엔진에 없는 클래스는 건너뛰고, 다 건너뛰어 빈 분류는 메뉴에서 숨긴다.
	// (Quixel·Fab·Media Plate·Geometry·Volumes는 엔진에 해당하는 액터가 없어 넣지 않았다.)
	constexpr int32 CategoryCount = 5;

	const FPlaceCategory Categories[CategoryCount] = {
		{ "Basic", EEditorIcon::ClassActor, {
			{ "AActor",             "Actor",               EEditorIcon::ClassActor,     nullptr },
			{ "ACharacter",         "Character",           EEditorIcon::ClassCharacter, nullptr },
			{ "APawn",              "Pawn",                EEditorIcon::ClassPawn,      nullptr },
			{ "ADefaultPawn",       "Default Pawn",        EEditorIcon::ClassPawn,      nullptr },
			{ "APointLightActor",   "Point Light",         EEditorIcon::LightPoint,     nullptr },
			{ "AStaticMeshActor",   "Cube",                EEditorIcon::Local,          "Cube" },
			{ "AStaticMeshActor",   "Sphere",              EEditorIcon::ShapeSphere,    "Sphere" },
			{ "AStaticMeshActor",   "Cylinder",            EEditorIcon::ShapeCylinder,  "Cylinder" },
			{ "AStaticMeshActor",   "Cone",                EEditorIcon::ShapeCone,      "Cone" },
			{ "AStaticMeshActor",   "Plane",               EEditorIcon::ShapePlane,     "Plane" },
			{ "AStaticMeshActor",   "Static Mesh Actor",   EEditorIcon::Local,          nullptr },
			{ "ASkeletalMeshActor", "Skeletal Mesh Actor", EEditorIcon::SkeletalMesh,   nullptr },
			{ "ATextRenderActor",   "Text Render Actor",   EEditorIcon::Font,           nullptr },
		} },
		{ "Lights", EEditorIcon::LightPoint, {
			{ "ADirectionalLightActor", "Directional Light", EEditorIcon::LightDirectional, nullptr },
			{ "APointLightActor",       "Point Light",       EEditorIcon::LightPoint,       nullptr },
			{ "ASpotLightActor",        "Spot Light",        EEditorIcon::LightSpot,        nullptr },
			{ "ARectLightActor",        "Rect Light",        EEditorIcon::LightRect,        nullptr },
			{ "ASkyLightActor",         "Sky Light",         EEditorIcon::LightSky,         nullptr },
		} },
		{ "Shapes", EEditorIcon::Shapes, {
			{ "AStaticMeshActor", "Cube",     EEditorIcon::Local,         "Cube" },
			{ "AStaticMeshActor", "Sphere",   EEditorIcon::ShapeSphere,   "Sphere" },
			{ "AStaticMeshActor", "Cylinder", EEditorIcon::ShapeCylinder, "Cylinder" },
			{ "AStaticMeshActor", "Cone",     EEditorIcon::ShapeCone,     "Cone" },
			{ "AStaticMeshActor", "Plane",    EEditorIcon::ShapePlane,    "Plane" },
		} },
		{ "Cinematic", EEditorIcon::Cinematics, {
			{ "ACameraActor",        "Camera Actor",         EEditorIcon::Camera,     nullptr },
			{ "ALevelSequenceActor", "Level Sequence Actor", EEditorIcon::Cinematics, nullptr },
		} },
		{ "Visual Effects", EEditorIcon::VisualEffects, {
			{ "AParticleActor",             "Particle System",         EEditorIcon::Particle,      nullptr },
			{ "AExponentialHeightFogActor", "Exponential Height Fog",  EEditorIcon::ShowFog,       nullptr },
			{ "ADecalActor",                "Decal Actor",             EEditorIcon::ShowDecal,     nullptr },
			{ "AWindDirectionalSource",     "Wind Directional Source", EEditorIcon::VisualEffects, nullptr },
		} },
	};

	// 게임이 시작할 때 GameMode가 만드는 액터들. 레벨에 놓는 것이 아니라서 All에서 뺀다 (UE도 NotPlaceable).
	const char* const NotPlaceableClasses[] = {
		"AGameModeBase", "AGameStateBase", "APlayerState", "APlayerController", "APlayerCameraManager", "AHUD",
		"AParticleEventManager",
	};

	bool IsPlaceable(const UClass* Class)
	{
		if (!Class || !Class->Constructor || !Class->IsChildOf(AActor::StaticClass()))
			return false;
		for (const char* Name : NotPlaceableClasses)
		{
			const UClass* Base = FindClass(Name);
			if (Base && Class->IsChildOf(Base))
				return false;
		}
		return true;
	}

	// "ADirectionalLightActor" -> "Directional Light Actor". Blueprint 클래스는 파일 이름 그대로 둔다.
	FString MakeDisplayName(const UClass* Class)
	{
		if (FBlueprint::IsBlueprintClass(Class))
			return Class->Name;
		const FString& Name = Class->Name;
		size_t Start = 0;
		if (Name.size() > 1 && (Name[0] == 'A' || Name[0] == 'U') && std::isupper(static_cast<unsigned char>(Name[1])))
			Start = 1;

		FString Result;
		for (size_t Index = Start; Index < Name.size(); ++Index)
		{
			const unsigned char C = static_cast<unsigned char>(Name[Index]);
			if (Index > Start && std::isupper(C))
			{
				const unsigned char Prev = static_cast<unsigned char>(Name[Index - 1]);
				const bool bNextLower = Index + 1 < Name.size() && std::islower(static_cast<unsigned char>(Name[Index + 1]));
				if (std::islower(Prev) || std::isdigit(Prev) || (std::isupper(Prev) && bNextLower))
					Result += ' ';
			}
			Result += static_cast<char>(C);
		}
		return Result;
	}

	// 가장 가까운 아는 부모의 아이콘
	EEditorIcon GetActorClassIcon(const UClass* Class)
	{
		if (FBlueprint::IsBlueprintClass(Class))
			return EEditorIcon::Blueprint;

		struct FClassIcon { const char* ClassName; EEditorIcon Icon; };
		static const FClassIcon Icons[] = {
			{ "ADirectionalLightActor", EEditorIcon::LightDirectional },
			{ "APointLightActor", EEditorIcon::LightPoint },
			{ "ASpotLightActor", EEditorIcon::LightSpot },
			{ "ARectLightActor", EEditorIcon::LightRect },
			{ "ASkyLightActor", EEditorIcon::LightSky },
			{ "ALightActorBase", EEditorIcon::LightPoint },
			{ "ACharacter", EEditorIcon::ClassCharacter },
			{ "APawn", EEditorIcon::ClassPawn },
			{ "ACameraActor", EEditorIcon::Camera },
			{ "ALevelSequenceActor", EEditorIcon::Cinematics },
			{ "AParticleActor", EEditorIcon::Particle },
			{ "ADecalActor", EEditorIcon::ShowDecal },
			{ "AExponentialHeightFogActor", EEditorIcon::ShowFog },
			{ "AWindDirectionalSource", EEditorIcon::VisualEffects },
			{ "AStaticMeshActor", EEditorIcon::Local },
			{ "ASkeletalMeshActor", EEditorIcon::SkeletalMesh },
			{ "ATextRenderActor", EEditorIcon::Font },
		};
		for (const UClass* It = Class; It; It = It->Super)
		{
			for (const FClassIcon& Entry : Icons)
			{
				if (It->Name == Entry.ClassName)
					return Entry.Icon;
			}
		}
		return EEditorIcon::ClassActor;
	}

	// 분류 하나의 실제 항목 (엔진에 있는 클래스만)
	TArray<FPlaceableActor> GetCategoryActors(const FPlaceCategory& Category)
	{
		TArray<FPlaceableActor> Result;
		for (const FPresetActor& Preset : Category.Actors)
		{
			UClass* Class = FindClass(Preset.ClassName);
			if (!Class || !Class->Constructor)
				continue;
			FPlaceableActor Item;
			Item.Label = Preset.Label;
			Item.Icon = Preset.Icon;
			Item.Class = Class;
			Item.MeshPath = Preset.MeshPath ? Preset.MeshPath : "";
			Result.Add(std::move(Item));
		}
		return Result;
	}

	// All: 놓을 수 있는 모든 액터 클래스 (액터 Blueprint 포함), 이름순
	TArray<FPlaceableActor> GetAllActors()
	{
		TArray<UClass*> Classes;
		Classes.Add(AActor::StaticClass());
		GetDerivedClasses(AActor::StaticClass(), Classes);

		TArray<FPlaceableActor> Result;
		for (UClass* Class : Classes)
		{
			if (!IsPlaceable(Class))
				continue;
			FPlaceableActor Item;
			Item.Label = MakeDisplayName(Class);
			Item.Icon = GetActorClassIcon(Class);
			Item.Class = Class;
			Result.Add(std::move(Item));
		}
		std::sort(Result.begin(), Result.end(),
			[](const FPlaceableActor& A, const FPlaceableActor& B) { return A.Label < B.Label; });
		return Result;
	}

	// ---------------- 그리기 ----------------

	float RowHeight() { return ImGui::GetFontSize() + 10.0f * EditorIcons::GetScale(); }

	// UE5 메뉴 줄: 아이콘 + 이름 (체크 칸 없음). 눌리면 true.
	bool IconRow(const char* Label, EEditorIcon Icon, bool bEnabled = true, const char* RightText = nullptr)
	{
		const float S = EditorIcons::GetScale();
		const float H = RowHeight();
		const float IconX = 14.0f * S, TextX = 30.0f * S;
		const float RightWidth = RightText ? ImGui::CalcTextSize(RightText).x + 24.0f * S : 0.0f;

		// 줄마다 이름으로 ID를 나눈다 ("##Row"만 쓰면 같은 메뉴의 줄끼리 ID가 겹친다)
		ImGui::PushID(Label);
		ImGui::BeginDisabled(!bEnabled);
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const float Width = std::max(TextX + ImGui::CalcTextSize(Label).x + 20.0f * S + RightWidth, ImGui::GetContentRegionAvail().x);
		const bool bPressed = ImGui::Selectable("##Row", false, ImGuiSelectableFlags_None, ImVec2(Width, H));

		ImDrawList* DL = ImGui::GetWindowDrawList();
		const float MidY = Min.y + H * 0.5f;
		EditorIcons::Draw(DL, Icon, ImVec2(Min.x + IconX, MidY), 15.0f * S, ImGui::GetColorU32(ImGuiCol_Text, 0.85f));
		DL->AddText(ImVec2(Min.x + TextX, MidY - ImGui::GetFontSize() * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), Label);
		if (RightText)
		{
			DL->AddText(ImVec2(ImGui::GetItemRectMax().x - 10.0f * S - ImGui::CalcTextSize(RightText).x, MidY - ImGui::GetFontSize() * 0.5f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), RightText);
		}
		ImGui::EndDisabled();
		ImGui::PopID();
		return bPressed;
	}

	// 메뉴를 그리는 동안 키보드 탐색 커서(파란 테두리)를 숨긴다. 검색 칸에 포커스를 주면 이 커서가
	// 켜져서 마우스로 고른 줄 위에 파란 사각형이 따로 그려진다.
	struct FScopedHideNavCursor
	{
		FScopedHideNavCursor() { ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); }
		~FScopedHideNavCursor() { ImGui::PopStyleColor(); }
	};

	// 아이콘이 있는 하위 메뉴 줄. 이름 앞을 공백으로 비워 강조 칸이 줄 전체에 깔리게 하고 그 자리에 아이콘을 그린다.
	bool IconMenu(const char* Label, EEditorIcon Icon)
	{
		const float S = EditorIcons::GetScale();
		const float SpaceWidth = std::max(1.0f, ImGui::CalcTextSize(" ").x);
		std::string Text(static_cast<size_t>(std::ceil(20.0f * S / SpaceWidth)), ' ');
		Text += Label;
		Text += "##";
		Text += Label;

		// 하위 메뉴가 열리면 그리는 창이 바뀌므로 지금 창의 그리기 목록을 먼저 잡아 둔다.
		ImDrawList* DL = ImGui::GetWindowDrawList();
		// 줄 간격을 넓히면 강조 칸이 위로 그만큼 더 커져 바로 위 줄(섹션 제목 등)을 덮는다.
		// 그 차이만큼 줄을 먼저 내려서 강조 칸의 위쪽 끝이 보통 줄과 같은 자리에 오게 한다.
		const float RowSpacing = 10.0f * S;
		const float NormalSpacing = ImGui::GetStyle().ItemSpacing.y;
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0f, (RowSpacing - NormalSpacing) * 0.5f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, RowSpacing));
		const bool bOpen = ImGui::BeginMenu(Text.c_str());
		ImGui::PopStyleVar();
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		EditorIcons::Draw(DL, Icon, ImVec2(Min.x + 14.0f * S, (Min.y + Max.y) * 0.5f), 15.0f * S,
			ImGui::GetColorU32(ImGuiCol_Text, 0.85f));
		return bOpen;
	}

	// Blueprint 에셋 경로("Blueprints/BP_Door.bp", Content 기준)를 작업 폴더 기준 파일 경로로 바꾼다.
	FString ToBlueprintFile(const FString& AssetPath)
	{
		const std::filesystem::path Path = FPaths::FromUtf8(AssetPath);
		std::error_code Error;
		if (Path.is_absolute() || std::filesystem::exists(Path, Error))
			return AssetPath;
		return FPaths::ToUtf8((FPaths::ProjectContentDir() / Path).generic_wstring());
	}
}

// ---------------- Quick Add 메뉴 ----------------

void FPlaceActorsMenu::PlaceItem(const FPlaceableActor& Item)
{
	if (!Place || !Place(Item))
		return;

	// RECENT: 같은 항목은 맨 앞으로 옮긴다.
	for (int32 Index = 0; Index < Recent.Num(); ++Index)
	{
		if (Recent[Index].Class == Item.Class && Recent[Index].MeshPath == Item.MeshPath)
		{
			Recent.RemoveAt(Index);
			break;
		}
	}
	Recent.Insert(Item, 0);
	while (Recent.Num() > MaxRecent)
		Recent.RemoveAt(Recent.Num() - 1);
}

bool FPlaceActorsMenu::ItemRow(const FPlaceableActor& Item, bool bEnabled)
{
	ImGui::PushID(Item.Class);
	ImGui::PushID(Item.Label.c_str());
	const bool bPressed = IconRow(Item.Label.c_str(), Item.Icon, bEnabled);
	if (Item.Class && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip(bEnabled ? "Place %s in front of the active viewport camera (%s)"
			: "Stop the play session to place %s (%s)", Item.Label.c_str(), Item.Class->Name.c_str());
	}
	ImGui::PopID();
	ImGui::PopID();
	return bPressed && bEnabled;
}

void FPlaceActorsMenu::DrawSearchResults()
{
	const bool bCanPlace = !CanPlace || CanPlace();

	// 분류의 항목(Cube처럼 메시가 정해진 것 포함)과 All의 모든 클래스를 이름으로 거른다. 같은 이름은 한 번만.
	TArray<FPlaceableActor> Matches;
	auto AddIfMatch = [&Matches, this](const FPlaceableActor& Item)
	{
		if (!EditorIcons::MatchesFilter(Item.Label.c_str(), Search) && !EditorIcons::MatchesFilter(Item.Class->Name.c_str(), Search))
			return;
		for (const FPlaceableActor& Existing : Matches)
		{
			if (Existing.Label == Item.Label)
				return;
		}
		Matches.Add(Item);
	};
	for (int32 Index = 0; Index < CategoryCount; ++Index)
	{
		for (const FPlaceableActor& Item : GetCategoryActors(Categories[Index]))
			AddIfMatch(Item);
	}
	for (const FPlaceableActor& Item : GetAllActors())
		AddIfMatch(Item);

	if (Matches.IsEmpty())
	{
		ImGui::TextDisabled("  No results");
		return;
	}
	for (const FPlaceableActor& Item : Matches)
	{
		if (ItemRow(Item, bCanPlace))
			PlaceItem(Item);
	}
}

void FPlaceActorsMenu::Draw()
{
	const FScopedHideNavCursor HideNavCursor;
	const float S = EditorIcons::GetScale();

	// 메뉴 폭을 UE 메뉴와 비슷하게 잡는다
	ImGui::Dummy(ImVec2(260.0f * S, 0.0f));

	// 검색: 메뉴가 열릴 때 비우고 바로 입력을 받는다
	if (ImGui::IsWindowAppearing())
	{
		Search[0] = '\0';
		ImGui::SetKeyboardFocusHere();
	}
	EditorIcons::SearchBox("##QuickAddSearch", Search, sizeof(Search), "Start typing to search");
	ImGui::Spacing();

	if (Search[0] != '\0')
	{
		DrawSearchResults();
		return;
	}

	const bool bCanPlace = !CanPlace || CanPlace();

	// ---- GET CONTENT ----
	EditorIcons::MenuSection("Get Content");
	ImGui::PushID("GetContent");
	if (ImportContent && IconMenu("Import Content", EEditorIcon::Import))
	{
		if (IconRow("Static Mesh...", EEditorIcon::Local))
			ImportContent(false);
		if (IconRow("Skeletal Mesh...", EEditorIcon::SkeletalMesh))
			ImportContent(true);
		ImGui::EndMenu();
	}
	if (IconRow("Content Browser", EEditorIcon::BrowseContent, OpenContentBrowser != nullptr) && OpenContentBrowser)
		OpenContentBrowser();
	ImGui::SetItemTooltip("Open the Content Drawer");
	ImGui::PopID();

	// ---- PLACE ACTORS ----
	ImGui::Spacing();
	EditorIcons::MenuSection("Place Actors");
	for (int32 Index = 0; Index < CategoryCount; ++Index)
	{
		const FPlaceCategory& Category = Categories[Index];
		const TArray<FPlaceableActor> Items = GetCategoryActors(Category);
		if (Items.IsEmpty())
			continue;   // 엔진에 이 분류의 액터가 하나도 없다
		if (IconMenu(Category.Name, Category.Icon))
		{
			for (const FPlaceableActor& Item : Items)
			{
				if (ItemRow(Item, bCanPlace))
					PlaceItem(Item);
			}
			ImGui::EndMenu();
		}
	}
	if (IconMenu("All", EEditorIcon::AllClasses))
	{
		const TArray<FPlaceableActor> Items = GetAllActors();
		// 클래스가 많으면 하위 메뉴가 화면을 넘지 않게 스크롤 칸 안에 둔다.
		const float ListHeight = std::min(static_cast<float>(Items.Num()) * (RowHeight() + ImGui::GetStyle().ItemSpacing.y),
			420.0f * S);
		ImGui::BeginChild("##AllActors", ImVec2(240.0f * S, ListHeight));
		for (const FPlaceableActor& Item : Items)
		{
			if (ItemRow(Item, bCanPlace))
			{
				PlaceItem(Item);
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndChild();
		ImGui::EndMenu();
	}

	// ---- RECENT ----
	if (!Recent.IsEmpty())
	{
		ImGui::Spacing();
		EditorIcons::MenuSection("Recent");
		ImGui::PushID("Recent");
		// 그리는 중에 Recent가 바뀌지 않게 복사본을 돈다.
		const TArray<FPlaceableActor> RecentCopy = Recent;
		for (const FPlaceableActor& Item : RecentCopy)
		{
			if (ItemRow(Item, bCanPlace))
				PlaceItem(Item);
		}
		ImGui::PopID();
	}

	if (!bCanPlace)
	{
		ImGui::Spacing();
		ImGui::Separator();
		EditorIcons::MenuNote("Stop the play session to place actors in the level.");
	}
}

// ---------------- Blueprints 메뉴 ----------------

void FBlueprintsToolbarMenu::Draw()
{
	const FScopedHideNavCursor HideNavCursor;
	const float S = EditorIcons::GetScale();
	ImGui::Dummy(ImVec2(280.0f * S, 0.0f));

	if (ImGui::IsWindowAppearing())
		Search[0] = '\0';

	EditorIcons::MenuSection("New");
	if (IconRow("New Empty Blueprint Class...", EEditorIcon::Blueprint, NewBlueprint != nullptr) && NewBlueprint)
		NewBlueprint();
	ImGui::SetItemTooltip("Pick a parent class and create a new Blueprint in the Content Drawer's current folder");

	// 이미 있는 Blueprint 클래스 (액터·컴포넌트). 누르면 Blueprint 에디터로 연다.
	TArray<UClass*> Classes;
	GetDerivedClasses(AActor::StaticClass(), Classes);
	GetDerivedClasses(UActorComponent::StaticClass(), Classes);
	TArray<UClass*> Blueprints;
	for (UClass* Class : Classes)
	{
		if (FBlueprint::IsBlueprintClass(Class) && !FBlueprint::GetAssetPath(Class).empty())
			Blueprints.Add(Class);
	}
	std::sort(Blueprints.begin(), Blueprints.end(), [](const UClass* A, const UClass* B) { return A->Name < B->Name; });

	ImGui::Spacing();
	EditorIcons::MenuSection("Open Blueprint Class");
	if (Blueprints.IsEmpty())
	{
		EditorIcons::MenuNote("There are no Blueprint classes in this project yet.");
		return;
	}

	EditorIcons::SearchBox("##BlueprintMenuSearch", Search, sizeof(Search), "Search Blueprints");
	ImGui::Spacing();

	const float ListHeight = std::min(static_cast<float>(Blueprints.Num()) * (RowHeight() + ImGui::GetStyle().ItemSpacing.y),
		300.0f * S);
	ImGui::BeginChild("##BlueprintList", ImVec2(0.0f, ListHeight));
	bool bAny = false;
	for (UClass* Class : Blueprints)
	{
		if (!EditorIcons::MatchesFilter(Class->Name.c_str(), Search))
			continue;
		bAny = true;
		const UClass* Parent = FBlueprint::GetNativeClass(Class);
		ImGui::PushID(Class);
		const bool bPressed = IconRow(Class->Name.c_str(),
			FBlueprint::IsComponentBlueprint(Class) ? EEditorIcon::ClassComponent : EEditorIcon::Blueprint,
			OpenBlueprint != nullptr, Parent ? MakeDisplayName(Parent).c_str() : nullptr);
		ImGui::SetItemTooltip("%s", FBlueprint::GetAssetPath(Class).c_str());
		ImGui::PopID();
		if (bPressed && OpenBlueprint)
		{
			OpenBlueprint(ToBlueprintFile(FBlueprint::GetAssetPath(Class)));
			ImGui::CloseCurrentPopup();
		}
	}
	if (!bAny)
		ImGui::TextDisabled("  No results");
	ImGui::EndChild();
}
