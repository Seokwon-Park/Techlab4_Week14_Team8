#include "EnginePCH.h"
#include "Editor/WorldSettings/WorldSettingsPanel.h"

#include <algorithm>
#include <cctype>

#include "Editor/EditorUI/EditorIcons.h"
#include "Editor/HitoriEd/LevelUndoHistory.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/MyPlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameplayInfoActors.h"
#include "Engine/Blueprint.h"
#include "Core/EngineLog.h"
#include "UObject/UObjectHash.h"

namespace
{
	// 검색 칸에 쓴 글자. 비어 있지 않으면 이름에 이 글자가 든 항목만 그린다.
	const char* GSearch = nullptr;

	bool Matches(const char* Label)
	{
		return EditorIcons::MatchesFilter(Label, GSearch);
	}

	// Details 패널과 같은 회색 분류 머리줄
	bool SectionHeader(const char* Label)
	{
		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_TableHeaderBg));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
		const bool bOpen = ImGui::CollapsingHeader(Label, ImGuiTreeNodeFlags_DefaultOpen);
		ImGui::PopStyleColor(3);
		return bOpen;
	}

	// [이름 | 값] 두 칸 표. 이름 칸 폭은 패널 폭의 40%다.
	bool BeginRows(const char* Id)
	{
		if (!ImGui::BeginTable(Id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
			return false;
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.4f);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.6f);
		return true;
	}

	// 한 줄을 시작하고 이름을 쓴다. 값 칸으로 옮겨 가로를 꽉 채우게 해 둔다.
	void RowLabel(const char* Label, const char* Tooltip)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Label);
		if (Tooltip && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", Tooltip);
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(-FLT_MIN);
	}

	// "ADefaultPawn" -> "DefaultPawn". UE 클래스 고르기처럼 접두사 A를 뗀다.
	const char* ShortClassName(const FString& Name)
	{
		if (Name.size() > 1 && Name[0] == 'A' && std::isupper(static_cast<unsigned char>(Name[1])))
			return Name.c_str() + 1;
		return Name.c_str();
	}
}

void FWorldSettingsPanel::Focus()
{
	SetOpen(true);
	bFocusRequested = true;
}

void FWorldSettingsPanel::OnRender()
{
	// 처음 열 때는 UE5처럼 Details 패널 옆 탭에 붙인다. 한 번 자리를 잡으면 imgui.ini가 기억한다.
	if (ImGuiWindow* Details = ImGui::FindWindowByName("Details"); Details && Details->DockId != 0)
		ImGui::SetNextWindowDockID(Details->DockId, ImGuiCond_FirstUseEver);
	if (bFocusRequested)
	{
		ImGui::SetNextWindowFocus();
		bFocusRequested = false;
	}

	EditorPanelUI::BeginPanel(GetPanelName(), GetOpenFlag());
	if (World)
	{
		EditorIcons::SearchBox("##WorldSettingsSearch", Search, sizeof(Search));
		if (World->GetWorldType() != EWorldType::Editor)
			ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Playing: changes apply to this session only");

		GSearch = Search;
		DrawGameModeSection();
		DrawPhysicsSection();
		DrawTimeSection();
		GSearch = nullptr;

		// 값을 바꾸면 실행 취소 기록이 다음 확정 때 월드를 비교해 한 단계로 남긴다.
		FLevelUndoHistory::NotePanelInteraction();
	}
	ImGui::End();
}

bool FWorldSettingsPanel::DrawClassPicker(const char* Id, UClass* BaseClass, FString& InOutClassName)
{
	TArray<UClass*> Classes;
	Classes.Add(BaseClass);
	GetDerivedClasses(BaseClass, Classes);
	std::sort(Classes.begin(), Classes.end(), [](const UClass* A, const UClass* B) { return A->Name < B->Name; });

	// 저장된 이름의 클래스가 없어졌으면 그 사실을 보여 준다.
	FString Preview = "None";
	if (!InOutClassName.empty())
		Preview = FindClass(InOutClassName) ? FString(ShortClassName(InOutClassName))
			: FString(ShortClassName(InOutClassName)) + " (Missing)";

	bool bChanged = false;
	if (ImGui::BeginCombo(Id, Preview.c_str(), ImGuiComboFlags_HeightLarge))
	{
		if (ImGui::Selectable("None", InOutClassName.empty()))
		{
			InOutClassName.clear();
			bChanged = true;
		}
		ImGui::Separator();
		for (UClass* Class : Classes)
		{
			if (!Class) continue;
			ImGui::PushID(Class);
			if (ImGui::Selectable(ShortClassName(Class->Name), InOutClassName == Class->Name))
			{
				InOutClassName = Class->Name;
				bChanged = true;
			}
			ImGui::SetItemTooltip("%s", Class->Name.c_str());
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	if (bChanged)
		FLevelUndoHistory::MarkDirty();
	return bChanged;
}

void FWorldSettingsPanel::DrawGameModeClassRow(UClass* GameModeClass, const char* Label, const char* PropertyName,
	const FString& ClassName, UClass* BaseClass, const char* Tooltip)
{
	RowLabel(Label, Tooltip);
	if (FBlueprint::IsBlueprintClass(GameModeClass))
	{
		FString Value = ClassName;
		if (DrawClassPicker((FString("##") + PropertyName).c_str(), BaseClass, Value))
		{
			FString Error;
			if (FBlueprint::SetDefaultStringProperty(GameModeClass, PropertyName, Value, Error))
				HTR_LOG(Info, "{}: {} = {}", GameModeClass->Name, PropertyName, Value.empty() ? FString("None") : Value);
			else
				HTR_LOG(Warning, "Failed to change {} of {} ({})", PropertyName, GameModeClass->Name, Error);
		}
		ImGui::SetItemTooltip("Saved to %s", FBlueprint::GetAssetPath(GameModeClass).c_str());
		return;
	}

	ImGui::BeginDisabled();
	const FString Preview = ClassName.empty() ? FString("None") : FString(ShortClassName(ClassName));
	if (ImGui::BeginCombo((FString("##") + PropertyName).c_str(), Preview.c_str()))
		ImGui::EndCombo();
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Set in the C++ GameMode class (%s).\nUse a GameMode Blueprint to change it here.",
		ClassName.empty() ? "None" : ClassName.c_str());
}

void FWorldSettingsPanel::DrawGameModeSection()
{
	// 검색 중이면 이 분류의 항목 이름 중 하나라도 맞을 때만 보여 준다.
	static const char* const Labels[] = { "Game Mode", "GameMode Override", "Selected GameMode", "Default Pawn Class",
		"HUD Class", "Player Controller Class", "Game State Class", "Player State Class", "Spectator Class" };
	if (!std::any_of(std::begin(Labels), std::end(Labels), [](const char* Label) { return Matches(Label); }))
		return;
	if (!SectionHeader("Game Mode") || !BeginRows("##GameModeRows"))
		return;

	const float S = EditorIcons::GetScale();

	// GameMode Override: 이 월드에서 쓸 GameMode. None이면 기본 AGameModeBase다.
	{
		RowLabel("GameMode Override", "GameMode class used when playing this level.\nNone uses the default AGameModeBase.");
		FString Override = World->GetGameModeOverride();
		const bool bOverridden = !Override.empty();
		UClass* OverrideClass = bOverridden ? FindClass(Override) : nullptr;
		const bool bBlueprint = OverrideClass && FBlueprint::IsBlueprintClass(OverrideClass) && OpenAssetCallback;
		// UE5처럼 기본값이 아니면 오른쪽에 되돌리기 화살표를, Blueprint면 그 앞에 편집 버튼을 둔다.
		const float ButtonW = ImGui::GetFrameHeight();
		const float ButtonsW = (bOverridden ? ButtonW + 4.0f * S : 0.0f) + (bBlueprint ? ButtonW + 4.0f * S : 0.0f);
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ButtonsW);
		if (DrawClassPicker("##GameModeOverride", AGameModeBase::StaticClass(), Override))
			World->SetGameModeOverride(Override);
		if (bBlueprint)
		{
			ImGui::SameLine(0.0f, 4.0f * S);
			if (EditorIcons::Button("##EditGameMode", EEditorIcon::Script, IM_COL32(60, 140, 230, 255), nullptr, false, false, ButtonW))
				OpenAssetCallback("Assets/" + FBlueprint::GetAssetPath(OverrideClass));
			ImGui::SetItemTooltip("Edit %s", OverrideClass->Name.c_str());
		}
		const float ResetW = ButtonW;
		if (bOverridden)
		{
			ImGui::SameLine(0.0f, 4.0f * S);
			if (EditorIcons::Button("##ResetGameMode", EEditorIcon::Reset, ImGui::GetColorU32(ImGuiCol_Text, 0.8f),
				nullptr, false, false, ResetW))
			{
				World->SetGameModeOverride(FString());
				FLevelUndoHistory::MarkDirty();
			}
			ImGui::SetItemTooltip("Reset to default (None)");
		}
	}

	// Selected GameMode: 고른 GameMode 클래스가 정해 둔 클래스들. 클래스 기본 객체(CDO)에서 읽는다.
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	const bool bOpen = ImGui::TreeNodeEx("Selected GameMode", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns);
	if (bOpen)
	{
		UClass* GameModeClass = World->ResolveGameModeClass();
		const AGameModeBase* Defaults = GameModeClass ? Cast<AGameModeBase>(GameModeClass->GetDefaultObject()) : nullptr;
		if (Defaults)
		{
			DrawGameModeClassRow(GameModeClass, "Default Pawn Class", "DefaultPawnClass", Defaults->DefaultPawnClass, APawn::StaticClass(),
				"Pawn spawned and possessed when play starts, unless the level already has a Pawn placed.");
			DrawGameModeClassRow(GameModeClass, "HUD Class", "HUDClass", Defaults->HUDClass, AHUD::StaticClass(),
				"HUD spawned for the player.");
			DrawGameModeClassRow(GameModeClass, "Player Controller Class", "PlayerControllerClass", Defaults->PlayerControllerClass,
				APlayerController::StaticClass(), "Controller spawned for the player.");
			DrawGameModeClassRow(GameModeClass, "Game State Class", "GameStateClass", Defaults->GameStateClass,
				AGameStateBase::StaticClass(), "Game state shared by everyone in the match.");
			DrawGameModeClassRow(GameModeClass, "Player State Class", "PlayerStateClass", Defaults->PlayerStateClass,
				APlayerState::StaticClass(), "Per-player state.");
			DrawGameModeClassRow(GameModeClass, "Spectator Class", "SpectatorClass", Defaults->SpectatorClass,
				ASpectatorPawn::StaticClass(), "Pawn used while spectating.");
		}
		ImGui::TreePop();
	}
	ImGui::EndTable();
}

void FWorldSettingsPanel::DrawPhysicsSection()
{
	const bool bShowOverride = Matches("Override World Gravity");
	const bool bShowGravity = Matches("World Gravity Z");
	if (!bShowOverride && !bShowGravity && !Matches("Physics"))
		return;
	if (!SectionHeader("Physics") || !BeginRows("##PhysicsRows"))
		return;

	const bool bAll = Matches("Physics");
	if (bShowOverride || bAll)
	{
		RowLabel("Override World Gravity",
			"When on, every movement component uses World Gravity Z instead of its own Gravity value.");
		bool bOverride = World->IsWorldGravityOverridden();
		if (ImGui::Checkbox("##OverrideWorldGravity", &bOverride))
			World->SetWorldGravityOverridden(bOverride);
	}
	if (bShowGravity || bAll)
	{
		RowLabel("World Gravity Z", "Gravity along Z in world units per second squared. Negative pulls down.");
		ImGui::BeginDisabled(!World->IsWorldGravityOverridden());
		float GravityZ = World->GetWorldGravityZ();
		if (ImGui::DragFloat("##WorldGravityZ", &GravityZ, 0.05f, -1000.0f, 1000.0f, "%.2f"))
			World->SetWorldGravityZ(GravityZ);
		ImGui::EndDisabled();
	}
	ImGui::EndTable();
}

void FWorldSettingsPanel::DrawTimeSection()
{
	if (!Matches("Time Dilation") && !Matches("Time"))
		return;
	if (!SectionHeader("Time") || !BeginRows("##TimeRows"))
		return;

	RowLabel("Time Dilation", "Speed of time for the whole world. 1 is normal, 0.5 is half speed.");
	float Dilation = World->GetGlobalTimeDilation();
	if (ImGui::DragFloat("##TimeDilation", &Dilation, 0.01f, UWorld::GetMinTimeDilation(), UWorld::GetMaxTimeDilation(), "%.3f",
		ImGuiSliderFlags_AlwaysClamp))
		World->SetGlobalTimeDilation(Dilation);
	ImGui::EndTable();
}
