#include "EnginePCH.h"
#include "Editor/Sequencer/SequencerPanel.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Editor/HitoriEd/LevelUndoHistory.h"

#include "LevelSequence/LevelSequence.h"
#include "LevelSequence/LevelSequencePlayer.h"
#include "LevelSequence/LevelSequenceActor.h"
#include "MovieScene/MovieScene.h"
#include "MovieSceneTracks/MovieScene3DTransformTrack.h"
#include "MovieSceneTracks/MovieSceneFloatTrack.h"
#include "MovieSceneTracks/MovieSceneFadeTrack.h"
#include "MovieSceneTracks/MovieSceneCameraCutTrack.h"
#include "MovieSceneTracks/MovieSceneEventTrack.h"
#include "MovieSceneTracks/MovieSceneSlomoTrack.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
	// 크기는 DPI 배율 1 기준 px (그릴 때 EditorIcons::GetScale()을 곱한다)
	constexpr float ToolbarHeight = 38.0f;
	constexpr float OutlinerWidth = 290.0f;
	constexpr float RulerHeight = 32.0f;
	constexpr float RowHeight = 24.0f;
	constexpr float BottomBarHeight = 32.0f;
	constexpr float KeyRadius = 5.5f;
	constexpr float DetailsHeight = 190.0f;

	// UE5 시퀀서 색
	const ImU32 ColorToolbarBg = IM_COL32(36, 36, 36, 255);
	const ImU32 ColorOutlinerBg = IM_COL32(26, 26, 26, 255);
	const ImU32 ColorRulerBg = IM_COL32(21, 21, 21, 255);
	const ImU32 ColorTrackInside = IM_COL32(38, 38, 38, 255);    // 재생 범위 안
	const ImU32 ColorTrackOutside = IM_COL32(28, 28, 28, 255);   // 재생 범위 밖
	const ImU32 ColorGrid = IM_COL32(22, 22, 22, 255);
	const ImU32 ColorSplitter = IM_COL32(14, 14, 14, 255);
	const ImU32 ColorRowLine = IM_COL32(32, 32, 32, 255);
	const ImU32 ColorRowSelected = IM_COL32(32, 74, 122, 255);
	const ImU32 ColorBindingRow = IM_COL32(34, 34, 34, 255);
	const ImU32 ColorActiveButton = IM_COL32(15, 110, 230, 255);   // 켜진 토글 (Snap 등)
	const ImU32 ColorRangeStart = IM_COL32(40, 190, 70, 255);
	const ImU32 ColorRangeEnd = IM_COL32(220, 50, 40, 255);
	const ImU32 ColorRulerTick = IM_COL32(120, 120, 120, 255);
	const ImU32 ColorRulerText = IM_COL32(170, 170, 170, 255);

	const ImU32 ColorKey = IM_COL32(205, 205, 205, 255);
	const ImU32 ColorKeySelected = IM_COL32(255, 200, 80, 255);
	const ImU32 ColorPlayhead = IM_COL32(230, 60, 50, 255);

	ImU32 IconGray() { return ImGui::GetColorU32(ImGuiCol_Text, 0.82f); }

	void DrawDiamond(ImDrawList* Draw, ImVec2 Center, float Radius, ImU32 Color)
	{
		Draw->AddQuadFilled({ Center.x, Center.y - Radius }, { Center.x + Radius, Center.y },
			{ Center.x, Center.y + Radius }, { Center.x - Radius, Center.y }, Color);
	}

	// 테두리가 있는 키 다이아몬드 (트랙 영역)
	void DrawKeyDiamond(ImDrawList* Draw, ImVec2 Center, float Radius, ImU32 Color)
	{
		DrawDiamond(Draw, Center, Radius + 1.0f, IM_COL32(10, 10, 10, 255));
		DrawDiamond(Draw, Center, Radius, Color);
	}

	ULevel* GetActiveLevel(UWorld* World)
	{
		if (!World) return nullptr;
		return World->GetCurrentLevel() ? World->GetCurrentLevel() : World->GetPersistentLevel();
	}

	// 시퀀스 바인딩 콤보 (Camera Cut 대상)
	bool BindingCombo(const char* Label, UMovieScene* MovieScene, FGuid& InOutGuid)
	{
		bool bChanged = false;
		const FMovieScenePossessable* Current = MovieScene ? MovieScene->FindPossessable(InOutGuid) : nullptr;
		if (ImGui::BeginCombo(Label, Current ? Current->Name.c_str() : "(none)"))
		{
			if (MovieScene)
			{
				for (const FMovieScenePossessable& Possessable : MovieScene->GetPossessables())
				{
					ImGui::PushID(Possessable.Guid.ToString().c_str());
					if (ImGui::Selectable(Possessable.Name.c_str(), Possessable.Guid == InOutGuid))
					{
						InOutGuid = Possessable.Guid;
						bChanged = true;
					}
					ImGui::PopID();
				}
			}
			ImGui::EndCombo();
		}
		return bChanged;
	}

	struct FGlobalTrackType
	{
		const char* Label;
		EEditorIcon Icon;
		UClass* (*GetClass)();
	};

	const FGlobalTrackType GlobalTrackTypes[] = {
		{ "Fade",           EEditorIcon::ShowVignette,      []() { return UMovieSceneFadeTrack::StaticClass(); } },
		{ "Camera Overlay", EEditorIcon::ShowCameraOverlay, []() { return UMovieSceneCameraOverlayTrack::StaticClass(); } },
		{ "Event",          EEditorIcon::SeqKey,            []() { return UMovieSceneEventTrack::StaticClass(); } },
		{ "Play Rate",      EEditorIcon::PlaybackOptions,   []() { return UMovieSceneSlomoTrack::StaticClass(); } },
	};

	const int32 DisplayRates[] = { 15, 24, 25, 30, 48, 50, 60, 120 };

	// 버튼 바로 아래에 팝업을 연다
	void OpenPopupBelowItem(const char* Id)
	{
		ImGui::OpenPopup(Id);
		ImGui::SetNextWindowPos({ ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f });
	}

	void ItemTooltip(const char* Text)
	{
		if (Text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", Text);
	}

	// 아래 막대의 프레임 숫자 칸 (테두리 없이 숫자 + 밑줄)
	bool FrameField(const char* Id, int32& InOutFrame, const ImVec2& Min, float Width, float Height, ImU32 Underline, bool bEditable = true)
	{
		const float S = EditorIcons::GetScale();
		const float FrameY = Min.y + (Height - ImGui::GetFrameHeight()) * 0.5f;
		bool bChanged = false;
		if (bEditable)
		{
			ImGui::SetCursorScreenPos({ Min.x, FrameY });
			ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(255, 255, 255, 18));
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(255, 255, 255, 30));
			ImGui::SetNextItemWidth(Width);
			bChanged = ImGui::DragInt(Id, &InOutFrame, 0.25f, 0, 0, "%04d");
			ImGui::PopStyleColor(3);
		}
		else
		{
			char Text[16];
			std::snprintf(Text, sizeof(Text), "%04d", InOutFrame);
			const ImVec2 TextSize = ImGui::CalcTextSize(Text);
			ImGui::GetWindowDrawList()->AddText({ Min.x + (Width - TextSize.x) * 0.5f, FrameY + (ImGui::GetFrameHeight() - TextSize.y) * 0.5f },
				ImGui::GetColorU32(ImGuiCol_Text), Text);
		}
		const float LineY = FrameY + ImGui::GetFrameHeight() + 1.0f * S;
		ImGui::GetWindowDrawList()->AddLine({ Min.x + 2.0f * S, LineY }, { Min.x + Width - 2.0f * S, LineY }, Underline, 1.0f * S);
		return bChanged;
	}
}

FSequencerPanel::FSequencerPanel()
{
	Player = NewObject<ULevelSequencePlayer>();
	NewSequence();
}

FSequencerPanel::~FSequencerPanel()
{
	Shutdown();
}

void FSequencerPanel::Shutdown()
{
	// 종료 중이라 액터를 되돌리거나 카메라를 돌려줄 필요가 없다
	if (Player) Player->DiscardStateAndStop();
	delete Player;
	Player = nullptr;
	delete Sequence;
	Sequence = nullptr;
	ClearSelection();
	World = nullptr;
}

bool FSequencerPanel::Init()
{
	SetOpen(false);
	return true;
}

UMovieScene* FSequencerPanel::GetMovieScene() const
{
	return Sequence ? Sequence->GetMovieScene() : nullptr;
}

void FSequencerPanel::NewSequence()
{
	if (Player) Player->Stop();
	delete Sequence;
	Sequence = NewObject<ULevelSequence>();
	Sequence->Initialize();
	ClearSelection();
	ResetPlayer();
	bFitPending = true;
}

void FSequencerPanel::ResetPlayer()
{
	if (!Player) return;
	const FMovieSceneSequencePlaybackSettings Settings = Player->GetPlaybackSettings();
	Player->Initialize(Sequence, World, Settings);
	// 에디터 미리보기는 Stop하면 항상 원래 값으로 (UE 시퀀서를 닫을 때와 같은 결과)
	Player->bRestoreStateByDefault = true;
}

void FSequencerPanel::SetWorld(UWorld* InWorld, bool bIsEditorWorld)
{
	if (World == InWorld) return;
	// 에디터 월드는 계속 살아 있으니 되돌린다. PIE 월드는 이미 지워졌을 수 있어 건드리지 않는다
	if (Player)
	{
		if (World && bWorldIsEditor) Player->Stop();
		else Player->DiscardStateAndStop();
	}
	World = InWorld;
	bWorldIsEditor = bIsEditorWorld;
	ResetPlayer();
}

void FSequencerPanel::ClearSelection()
{
	SelectedTrack = nullptr;
	SelectedBinding.Invalidate();
	SelectedChannel = SelectedKey = -1;
}

UObject* FSequencerPanel::ResolveBinding(const FGuid& Guid) const
{
	return Sequence ? Sequence->LocateBoundObject(Guid, World) : nullptr;
}

UMovieSceneSection* FSequencerPanel::GetKeySection(UMovieSceneTrack* Track) const
{
	if (!Track || Track->GetAllSections().IsEmpty()) return nullptr;
	return Track->GetAllSections()[0];
}

float FSequencerPanel::FrameToSeconds(FFrameNumber Frame) const
{
	const UMovieScene* MovieScene = GetMovieScene();
	if (!MovieScene) return 0.0f;
	return static_cast<float>(MovieScene->GetTickResolution().AsSeconds(FFrameTime(Frame - MovieScene->GetPlaybackRange().GetLowerBoundValue())));
}

FFrameNumber FSequencerPanel::SecondsToFrame(float Seconds) const
{
	const UMovieScene* MovieScene = GetMovieScene();
	if (!MovieScene) return FFrameNumber(0);
	return MovieScene->GetPlaybackRange().GetLowerBoundValue() +
		FFrameNumber(static_cast<int32>(std::lround(Seconds * MovieScene->GetTickResolution().AsDecimal())));
}

float FSequencerPanel::SnapSeconds(float Seconds) const
{
	const UMovieScene* MovieScene = GetMovieScene();
	if (!bSnap || !MovieScene) return Seconds;
	const double Rate = MovieScene->GetDisplayRate().AsDecimal();
	return static_cast<float>(std::round(Seconds * Rate) / Rate);
}

float FSequencerPanel::GetLengthSeconds() const
{
	const UMovieScene* MovieScene = GetMovieScene();
	return MovieScene ? static_cast<float>(MovieScene->GetTickResolution().AsSeconds(FFrameTime(FFrameNumber(MovieScene->GetPlaybackRange().Size())))) : 0.0f;
}

float FSequencerPanel::GetPlayheadSeconds() const
{
	return Player ? static_cast<float>(Player->GetCurrentTimeSeconds()) : 0.0f;
}

void FSequencerPanel::Tick(float DeltaTime)
{
	if (!Player || !World) return;
	// UE 시퀀스 플레이어처럼 월드 시간 배율을 따른다 (Play Rate 트랙, HitStop 포함)
	if (Player->IsPlaying()) Player->Update(DeltaTime * World->GetEffectiveTimeDilation());

	// 에디터 월드: Camera Cut 카메라를 뷰포트가 따라간다 (PIE는 플레이어가 게임 뷰포트를 바꾼다)
	if (bWorldIsEditor && PilotCamera && Player->GetPlaybackStatus() != EMovieScenePlayerStatus::Stopped)
		if (UCameraComponent* Camera = Player->GetActiveCameraComponent()) PilotCamera(Camera);
}

double FSequencerPanel::GetDisplayRate() const
{
	const UMovieScene* MovieScene = GetMovieScene();
	const double Rate = MovieScene ? MovieScene->GetDisplayRate().AsDecimal() : 30.0;
	return Rate > 0.0 ? Rate : 30.0;
}

int32 FSequencerPanel::SecondsToDisplayFrame(float Seconds) const
{
	return static_cast<int32>(std::floor(Seconds * GetDisplayRate() + 1e-3));
}

// ---------------------------------------------------------------------------
// 명령
// ---------------------------------------------------------------------------

void FSequencerPanel::SaveSequence()
{
	if (Sequence && !Sequence->SaveToFile(AssetPath))
		HTR_LOG(Warning, "Sequence save failed: {}", AssetPath);
}

void FSequencerPanel::LoadSequence()
{
	Player->Stop();
	ULevelSequence* Loaded = ULevelSequence::LoadLevelSequence(AssetPath);
	if (!Loaded) return;
	delete Sequence;
	Sequence = Loaded;
	ClearSelection();
	ResetPlayer();
	bFitPending = true;
}

// 저장한 시퀀스를 재생하는 ALevelSequenceActor를 에디터 월드에 놓는다 (PIE 시작 때 자동 재생)
void FSequencerPanel::PlaceSequenceActor()
{
	if (!bWorldIsEditor || !World) return;
	Sequence->SaveToFile(AssetPath);
	if (ALevelSequenceActor* Actor = World->SpawnActor<ALevelSequenceActor>())
		Actor->LevelSequenceAsset = AssetPath;
	FLevelUndoHistory::MarkDirty();
}

// UE "Actor To Sequencer": Possessable 바인딩 + 기본 Transform 트랙
void FSequencerPanel::AddActorBinding(AActor& Actor)
{
	UMovieScene* MovieScene = GetMovieScene();
	if (!MovieScene) return;
	const FGuid Guid = MovieScene->AddPossessable(Actor.GetName(), Actor.GetClass());
	Sequence->BindPossessableObject(Guid, Actor, World);
	MovieScene->AddTrack<UMovieScene3DTransformTrack>(Guid);
	ClearSelection();
	SelectedBinding = Guid;
}

void FSequencerPanel::AddGlobalTrack(UClass* TrackClass)
{
	UMovieScene* MovieScene = GetMovieScene();
	if (!MovieScene || !TrackClass) return;
	ClearSelection();
	SelectedTrack = MovieScene->AddTrack(TrackClass);
}

FMovieSceneFloatChannel* FSequencerPanel::GetSelectedChannel() const
{
	UMovieSceneSection* Section = GetKeySection(SelectedTrack);
	return Section && SelectedChannel >= 0 ? Section->GetChannel(SelectedChannel) : nullptr;
}

void FSequencerPanel::DeleteSelectedKey()
{
	FMovieSceneFloatChannel* Channel = GetSelectedChannel();
	if (!Channel || SelectedKey < 0 || SelectedKey >= Channel->GetNumKeys()) return;
	Channel->RemoveKey(SelectedKey);
	SelectedKey = -1;
	Player->Refresh();
}

void FSequencerPanel::DeleteSelectedTrack()
{
	UMovieScene* MovieScene = GetMovieScene();
	if (!MovieScene || !SelectedTrack) return;
	Player->Stop();
	MovieScene->RemoveTrack(SelectedTrack);
	ClearSelection();
}

void FSequencerPanel::SetAllExpanded(bool bExpanded)
{
	UMovieScene* MovieScene = GetMovieScene();
	if (!MovieScene) return;
	if (UMovieSceneTrack* Cut = MovieScene->GetCameraCutTrack()) Cut->bExpanded = bExpanded;
	for (UMovieSceneTrack* Track : MovieScene->GetTracks()) Track->bExpanded = bExpanded;
	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
		for (UMovieSceneTrack* Track : Binding.GetTracks()) Track->bExpanded = bExpanded;
}

void FSequencerPanel::JumpToSeconds(float Seconds)
{
	Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(std::clamp(Seconds, 0.0f, GetLengthSeconds()), EUpdatePositionMethod::Scrub));
}

void FSequencerPanel::StepFrames(int32 Delta)
{
	const double Rate = GetDisplayRate();
	const double Frame = std::round(GetPlayheadSeconds() * Rate) + Delta;
	JumpToSeconds(static_cast<float>(Frame / Rate));
}

// 모든 트랙의 키(이벤트 포함) 중 재생 헤드 바로 앞/뒤로
void FSequencerPanel::JumpToAdjacentKey(bool bNext)
{
	UMovieScene* MovieScene = GetMovieScene();
	if (!MovieScene) return;
	const float Now = GetPlayheadSeconds();
	const float Epsilon = static_cast<float>(0.5 / GetDisplayRate());
	float Best = bNext ? FLT_MAX : -FLT_MAX;

	auto Consider = [&](FFrameNumber Frame)
	{
		const float T = FrameToSeconds(Frame);
		if (bNext ? (T > Now + Epsilon && T < Best) : (T < Now - Epsilon && T > Best)) Best = T;
	};
	auto VisitTrack = [&](UMovieSceneTrack* Track)
	{
		if (!Track) return;
		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			for (int32 Index = 0; Index < Section->GetNumChannels(); ++Index)
				if (FMovieSceneFloatChannel* Channel = Section->GetChannel(Index))
					for (const FFrameNumber& Time : Channel->GetTimes()) Consider(Time);
			if (const UMovieSceneEventTriggerSection* Events = Cast<UMovieSceneEventTriggerSection>(Section))
				for (const FFrameNumber& Time : Events->EventChannel.Times) Consider(Time);
		}
	};

	VisitTrack(MovieScene->GetCameraCutTrack());
	for (UMovieSceneTrack* Track : MovieScene->GetTracks()) VisitTrack(Track);
	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
		for (UMovieSceneTrack* Track : Binding.GetTracks()) VisitTrack(Track);

	if (Best != FLT_MAX && Best != -FLT_MAX) JumpToSeconds(Best);
}

void FSequencerPanel::TogglePlay()
{
	if (Player->IsPlaying())
	{
		Player->Pause();
		return;
	}
	// 끝에 멈춰 있으면 처음부터 (UE와 같음)
	if (GetPlayheadSeconds() >= GetLengthSeconds() - 0.5f / static_cast<float>(GetDisplayRate())) JumpToSeconds(0.0f);
	Player->Play();
}

void FSequencerPanel::ZoomToFit(float TimelineWidth)
{
	const float Length = std::max(GetLengthSeconds(), 0.1f);
	const float Lead = std::max(0.5f, Length * 0.03f);
	PixelsPerSecond = std::clamp(TimelineWidth / (Length + Lead * 2.0f), 10.0f, 2000.0f);
	ViewStart = -Lead;
}

void FSequencerPanel::ClampView()
{
	const float Length = GetLengthSeconds();
	ViewStart = std::clamp(ViewStart, -std::max(5.0f, Length * 0.5f), Length + 5.0f);
}

void FSequencerPanel::KeySelectedTrack()
{
	UMovieSceneSection* Section = GetKeySection(SelectedTrack);
	if (!Section) return;
	UObject* BoundObject = SelectedBinding.IsValid() ? ResolveBinding(SelectedBinding) : nullptr;
	Section->KeyCurrentValues(SecondsToFrame(SnapSeconds(GetPlayheadSeconds())), *Player, BoundObject);
	Player->Refresh();
}

// ---------------------------------------------------------------------------
// 그리기
// ---------------------------------------------------------------------------

void FSequencerPanel::OnRender()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool bVisible = EditorPanelUI::BeginPanel("Sequencer", GetOpenFlag(), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();
	if (!bVisible)
	{
		ImGui::End();
		return;
	}

	const float S = EditorIcons::GetScale();
	DrawToolbar();

	const float DetailsH = bShowDetails ? DetailsHeight * S : 0.0f;
	ImGui::BeginChild("##SequencerBody", ImVec2(0.0f, -DetailsH), ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	DrawTimeline();
	ImGui::EndChild();

	if (bShowDetails)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * S, 6.0f * S));
		ImGui::BeginChild("##Details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
		DrawDetails();
		ImGui::EndChild();
		ImGui::PopStyleVar();
	}
	ImGui::End();
}

void FSequencerPanel::DrawToolbar()
{
	UMovieScene* MovieScene = GetMovieScene();
	FMovieSceneSequencePlaybackSettings& Settings = Player->GetPlaybackSettings();

	const float S = EditorIcons::GetScale();
	ImDrawList* Draw = ImGui::GetWindowDrawList();
	const ImVec2 P = ImGui::GetCursorScreenPos();
	const float Width = ImGui::GetContentRegionAvail().x;
	const float H = ToolbarHeight * S;
	const float BtnH = 28.0f * S;
	const float Y = P.y + (H - BtnH) * 0.5f;
	float X = P.x + 8.0f * S;

	Draw->AddRectFilled(P, { P.x + Width, P.y + H }, ColorToolbarBg);
	Draw->AddLine({ P.x, P.y + H - 1.0f }, { P.x + Width, P.y + H - 1.0f }, ColorSplitter);

	auto Separator = [&]()
	{
		X += 5.0f * S;
		Draw->AddLine({ X, Y + 4.0f * S }, { X, Y + BtnH - 4.0f * S }, IM_COL32(60, 60, 60, 255), 1.0f * S);
		X += 6.0f * S;
	};
	// bActive면 UE5처럼 파란 배경 (Snap 등 켜진 토글)
	auto IconButton = [&](const char* Id, EEditorIcon Icon, const char* Tip, bool bDropdown = false, bool bActive = false, const char* Label = nullptr) -> bool
	{
		const float W = EditorIcons::CalcButtonWidth(Icon != EEditorIcon::None, Label, bDropdown, BtnH);
		if (bActive) Draw->AddRectFilled({ X, Y }, { X + W, Y + BtnH }, ColorActiveButton, 3.0f * S);
		ImGui::SetCursorScreenPos({ X, Y });
		const bool bPressed = EditorIcons::Button(Id, Icon, bActive ? IM_COL32_WHITE : IconGray(), Label, bDropdown, false, BtnH);
		ItemTooltip(Tip);
		X += W + 2.0f * S;
		return bPressed;
	};

	// 재생 컨텍스트 (월드)
	if (IconButton("##SeqWorld", EEditorIcon::World, "Playback Context", true)) OpenPopupBelowItem("##SeqWorldMenu");
	if (ImGui::BeginPopup("##SeqWorldMenu"))
	{
		EditorIcons::MenuSection("Playback Context");
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f * S);
		ImGui::TextUnformatted(!World ? "No World" : bWorldIsEditor ? "Editor World" : "PIE World");
		ImGui::Separator();
		if (EditorIcons::MenuRow("Place Level Sequence Actor", EEditorIcon::Cinematics, false, bWorldIsEditor && World))
			PlaceSequenceActor();
		ImGui::EndPopup();
	}
	Separator();

	if (IconButton("##SeqSave", EEditorIcon::Save, "Save Sequence")) SaveSequence();
	if (IconButton("##SeqBrowse", EEditorIcon::BrowseContent, "Sequence Asset (New / Load)")) OpenPopupBelowItem("##SeqAssetMenu");
	if (ImGui::BeginPopup("##SeqAssetMenu"))
	{
		EditorIcons::MenuSection("Sequence Asset");
		ImGui::SetNextItemWidth(320.0f * S);
		ImGui::InputText("##AssetPath", AssetPath, sizeof(AssetPath));
		ImGui::Separator();
		if (EditorIcons::MenuRow("New Sequence", EEditorIcon::File)) NewSequence();
		if (EditorIcons::MenuRow("Load", EEditorIcon::Folder)) LoadSequence();
		if (EditorIcons::MenuRow("Save", EEditorIcon::Save)) SaveSequence();
		ImGui::EndPopup();
	}

	const bool bHasCut = MovieScene && MovieScene->GetCameraCutTrack();
	ImGui::BeginDisabled(bHasCut || !MovieScene);
	if (IconButton("##SeqCamera", EEditorIcon::Camera, "Add Camera Cut Track") && MovieScene)
	{
		ClearSelection();
		SelectedTrack = MovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass());
	}
	ImGui::EndDisabled();

	ImGui::BeginDisabled(!bWorldIsEditor || !World);
	if (IconButton("##SeqPlace", EEditorIcon::Cinematics, "Save and Place Level Sequence Actor")) PlaceSequenceActor();
	ImGui::EndDisabled();
	Separator();

	// Actions
	if (IconButton("##SeqActions", EEditorIcon::Wrench, "Actions", true)) OpenPopupBelowItem("##SeqActionsMenu");
	if (ImGui::BeginPopup("##SeqActionsMenu"))
	{
		EditorIcons::MenuSection("Keys");
		if (EditorIcons::MenuRow("Key Selected Track", EEditorIcon::SeqKeyAll, false, SelectedTrack != nullptr)) KeySelectedTrack();
		if (EditorIcons::MenuRow("Delete Selected Key", EEditorIcon::SeqKey, false, SelectedKey >= 0, false, "Delete")) DeleteSelectedKey();
		EditorIcons::MenuSection("Tracks");
		if (EditorIcons::MenuRow("Delete Selected Track", EEditorIcon::None, false, SelectedTrack != nullptr)) DeleteSelectedTrack();
		EditorIcons::MenuSection("Sequence");
		if (EditorIcons::MenuRow("New Sequence", EEditorIcon::File)) NewSequence();
		ImGui::EndPopup();
	}

	// View options
	if (IconButton("##SeqView", EEditorIcon::Eye, "View Options", true)) OpenPopupBelowItem("##SeqViewMenu");
	if (ImGui::BeginPopup("##SeqViewMenu"))
	{
		EditorIcons::MenuSection("View");
		if (EditorIcons::MenuRow("Details Panel", EEditorIcon::Details, bShowDetails, true, true)) bShowDetails = !bShowDetails;
		if (EditorIcons::MenuRow("Auto Scroll", EEditorIcon::AutoScroll, bAutoScroll, true, true)) bAutoScroll = !bAutoScroll;
		if (EditorIcons::MenuRow("Zoom to Fit", EEditorIcon::Viewport)) bFitPending = true;
		EditorIcons::MenuSection("Outliner");
		if (EditorIcons::MenuRow("Expand All", EEditorIcon::ChevronDown)) SetAllExpanded(true);
		if (EditorIcons::MenuRow("Collapse All", EEditorIcon::ChevronRight)) SetAllExpanded(false);
		ImGui::EndPopup();
	}

	// Playback options
	if (IconButton("##SeqPlayback", EEditorIcon::PlaybackOptions, "Playback Options", true)) OpenPopupBelowItem("##SeqPlaybackMenu");
	if (ImGui::BeginPopup("##SeqPlaybackMenu"))
	{
		EditorIcons::MenuSection("Playback");
		const bool bLoop = Settings.LoopCount.Value != 0;
		if (EditorIcons::MenuRow("Loop", EEditorIcon::Loop, bLoop, true, true)) Settings.LoopCount.Value = bLoop ? 0 : -1;
		if (EditorIcons::MenuRow("Hold at End", EEditorIcon::Pause, Settings.bPauseAtEnd, true, true)) Settings.bPauseAtEnd = !Settings.bPauseAtEnd;
		ImGui::Separator();
		ImGui::SetNextItemWidth(120.0f * S);
		ImGui::DragFloat("Play Rate", &Settings.PlayRate, 0.05f, 0.1f, 4.0f, "%.2fx");
		float Length = GetLengthSeconds();
		ImGui::SetNextItemWidth(120.0f * S);
		if (ImGui::DragFloat("Length", &Length, 0.1f, 0.1f, 600.0f, "%.2f s") && MovieScene)
			MovieScene->SetPlaybackRange(MovieScene->GetPlaybackRange().GetLowerBoundValue(),
				SecondsToFrame(std::max(Length, 0.05f)).Value - MovieScene->GetPlaybackRange().GetLowerBoundValue().Value);
		ImGui::EndPopup();
	}

	// 선택 키 보간
	FMovieSceneFloatChannel* KeyChannel = GetSelectedChannel();
	const bool bHasKey = KeyChannel && SelectedKey >= 0 && SelectedKey < KeyChannel->GetNumKeys();
	if (IconButton("##SeqKeyInterp", EEditorIcon::SeqKey, "Key Interpolation", true)) OpenPopupBelowItem("##SeqKeyMenu");
	if (ImGui::BeginPopup("##SeqKeyMenu"))
	{
		EditorIcons::MenuSection("Selected Key Interpolation");
		const char* InterpNames[] = { "Linear", "Constant", "Cubic" };
		for (int32 Index = 0; Index < IM_ARRAYSIZE(InterpNames); ++Index)
		{
			const bool bCurrent = bHasKey && static_cast<int32>(KeyChannel->GetValues()[SelectedKey].InterpMode) == Index;
			if (EditorIcons::MenuRow(InterpNames[Index], EEditorIcon::None, bCurrent, bHasKey, false, nullptr, true))
			{
				KeyChannel->GetValues()[SelectedKey].InterpMode = static_cast<ERichCurveInterpMode>(Index);
				KeyChannel->AutoSetTangents();
				Player->Refresh();
			}
		}
		if (!bHasKey) EditorIcons::MenuNote("Select a key in a channel row first.");
		ImGui::EndPopup();
	}

	ImGui::BeginDisabled(!SelectedTrack);
	if (IconButton("##SeqKeyAll", EEditorIcon::SeqKeyAll, "Key Selected Track at Playhead")) KeySelectedTrack();
	ImGui::EndDisabled();
	Separator();

	if (IconButton("##SeqSnap", EEditorIcon::Magnet, "Snap to Frames", false, bSnap)) bSnap = !bSnap;
	if (IconButton("##SeqAutoScroll", EEditorIcon::AutoScroll, "Auto Scroll (follow playhead)", false, bAutoScroll)) bAutoScroll = !bAutoScroll;

	char RateLabel[24];
	std::snprintf(RateLabel, sizeof(RateLabel), "%g fps", GetDisplayRate());
	if (IconButton("##SeqRate", EEditorIcon::None, "Display Rate", true, false, RateLabel)) OpenPopupBelowItem("##SeqRateMenu");
	if (ImGui::BeginPopup("##SeqRateMenu"))
	{
		EditorIcons::MenuSection("Display Rate");
		for (int32 Rate : DisplayRates)
		{
			char Label[16];
			std::snprintf(Label, sizeof(Label), "%d fps", Rate);
			if (EditorIcons::MenuRow(Label, EEditorIcon::None, std::abs(GetDisplayRate() - Rate) < 1e-3, MovieScene != nullptr, false, nullptr, true))
				MovieScene->SetDisplayRate(FFrameRate(Rate, 1));
		}
		ImGui::EndPopup();
	}
	Separator();

	if (IconButton("##SeqDetails", EEditorIcon::Curve, "Key / Curve Details Panel", false, bShowDetails)) bShowDetails = !bShowDetails;
	if (IconButton("##SeqFit", EEditorIcon::TrackArea, "Zoom to Fit")) bFitPending = true;

	// 오른쪽: 시퀀스 이름
	const char* NameBegin = AssetPath;
	for (const char* C = AssetPath; *C; ++C)
		if (*C == '/' || *C == '\\') NameBegin = C + 1;
	const char* NameEnd = std::strrchr(NameBegin, '.');
	if (!NameEnd) NameEnd = NameBegin + std::strlen(NameBegin);
	const ImVec2 NameSize = ImGui::CalcTextSize(NameBegin, NameEnd);
	const float NameX = P.x + Width - NameSize.x - 14.0f * S;
	if (NameX > X + 8.0f * S)
		Draw->AddText({ NameX, P.y + (H - NameSize.y) * 0.5f }, ImGui::GetColorU32(ImGuiCol_Text), NameBegin, NameEnd);

	ImGui::SetCursorScreenPos({ P.x, P.y + H });
}

void FSequencerPanel::DrawOutlinerHeader(const ImVec2& Min, float Width, float Height)
{
	const float S = EditorIcons::GetScale();
	ImDrawList* Draw = ImGui::GetWindowDrawList();
	const float BtnH = 24.0f * S;
	const float Y = Min.y + (Height - BtnH) * 0.5f;
	float X = Min.x + 8.0f * S;

	// + Add (테두리 있는 버튼)
	const float AddW = EditorIcons::CalcButtonWidth(true, "Add", false, BtnH);
	Draw->AddRectFilled({ X, Y }, { X + AddW, Y + BtnH }, IM_COL32(48, 48, 48, 255), 3.0f * S);
	ImGui::SetCursorScreenPos({ X, Y });
	if (EditorIcons::Button("##SeqAdd", EEditorIcon::Plus, EditorIcons::PlayGreen, "Add", false, false, BtnH))
		OpenPopupBelowItem("##SeqAddMenu");
	X += AddW + 6.0f * S;

	if (ImGui::BeginPopup("##SeqAddMenu"))
	{
		UMovieScene* MovieScene = GetMovieScene();
		EditorIcons::MenuSection("Actor To Sequencer");
		AActor* ActorToAdd = nullptr;
		int32 NumActors = 0;
		if (ULevel* Level = GetActiveLevel(World))
		{
			for (AActor* Actor : Level->GetActors())
			{
				if (!Actor || Cast<ALevelSequenceActor>(Actor)) continue;
				++NumActors;
				ImGui::PushID(Actor);
				if (EditorIcons::MenuRow(Actor->GetName().c_str(), EEditorIcon::ClassActor)) ActorToAdd = Actor;
				ImGui::PopID();
			}
		}
		if (NumActors == 0) EditorIcons::MenuNote("No actors in the current level.");
		if (ActorToAdd) AddActorBinding(*ActorToAdd);

		ImGui::Separator();
		EditorIcons::MenuSection("Tracks");
		if (EditorIcons::MenuRow("Camera Cut", EEditorIcon::Camera, false, MovieScene && !MovieScene->GetCameraCutTrack()))
		{
			ClearSelection();
			SelectedTrack = MovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass());
		}
		for (const FGlobalTrackType& Type : GlobalTrackTypes)
			if (EditorIcons::MenuRow(Type.Label, Type.Icon, false, MovieScene != nullptr)) AddGlobalTrack(Type.GetClass());
		ImGui::EndPopup();
	}

	// 필터
	const float FilterW = EditorIcons::CalcButtonWidth(true, nullptr, true, BtnH);
	ImGui::SetCursorScreenPos({ X, Y });
	if (EditorIcons::Button("##SeqFilter", EEditorIcon::Filter, IconGray(), nullptr, true, false, BtnH))
		OpenPopupBelowItem("##SeqFilterMenu");
	ItemTooltip("Filters");
	X += FilterW + 6.0f * S;
	if (ImGui::BeginPopup("##SeqFilterMenu"))
	{
		EditorIcons::MenuSection("Show");
		if (EditorIcons::MenuRow("Global Tracks", EEditorIcon::Level, bShowGlobalTracks, true, true)) bShowGlobalTracks = !bShowGlobalTracks;
		if (EditorIcons::MenuRow("Actor Bindings", EEditorIcon::ClassActor, bShowBindings, true, true)) bShowBindings = !bShowBindings;
		ImGui::EndPopup();
	}

	// 검색
	ImGui::SetCursorScreenPos({ X, Min.y + (Height - ImGui::GetFrameHeight() - 2.0f * S) * 0.5f });
	EditorIcons::SearchBox("##SeqSearch", SearchBuffer, sizeof(SearchBuffer), "Search...", Min.x + Width - 8.0f * S - X);
}

void FSequencerPanel::BuildRows(TArray<FRow>& OutRows) const
{
	const UMovieScene* MovieScene = GetMovieScene();
	if (!MovieScene) return;
	auto AddTrackRows = [&OutRows, this](UMovieSceneTrack* Track, const FGuid& Binding)
	{
		FRow Header;
		Header.Type = ERowType::Track;
		Header.Track = Track;
		Header.Binding = Binding;
		OutRows.Add(Header);
		UMovieSceneSection* Section = GetKeySection(Track);
		if (!Track->bExpanded || !Section) return;
		for (int32 Channel = 0; Channel < Section->GetNumChannels(); ++Channel)
		{
			FRow Row = Header;
			Row.Type = ERowType::Channel;
			Row.Channel = Channel;
			OutRows.Add(Row);
		}
	};

	if (bShowGlobalTracks)
	{
		if (UMovieSceneTrack* Cut = MovieScene->GetCameraCutTrack())
			if (EditorIcons::MatchesFilter(Cut->GetDisplayName(), SearchBuffer)) AddTrackRows(Cut, FGuid());
		for (UMovieSceneTrack* Track : MovieScene->GetTracks())
			if (EditorIcons::MatchesFilter(Track->GetDisplayName(), SearchBuffer)) AddTrackRows(Track, FGuid());
	}
	if (!bShowBindings) return;
	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
	{
		// 바인딩 이름이 맞으면 트랙 전부, 아니면 이름이 맞는 트랙만
		const FMovieScenePossessable* Possessable = GetMovieScene()->FindPossessable(Binding.GetObjectGuid());
		const bool bNameMatches = EditorIcons::MatchesFilter(Possessable ? Possessable->Name.c_str() : "", SearchBuffer);
		const int32 BindingRow = OutRows.Num();
		FRow Row;
		Row.Type = ERowType::Binding;
		Row.Binding = Binding.GetObjectGuid();
		OutRows.Add(Row);
		bool bAnyTrack = false;
		for (UMovieSceneTrack* Track : Binding.GetTracks())
		{
			if (!bNameMatches && !EditorIcons::MatchesFilter(Track->GetDisplayName(), SearchBuffer)) continue;
			AddTrackRows(Track, Binding.GetObjectGuid());
			bAnyTrack = true;
		}
		if (!bNameMatches && !bAnyTrack) OutRows.RemoveAt(BindingRow);
	}
}

void FSequencerPanel::DrawTimeline()
{
	UMovieScene* MovieScene = GetMovieScene();
	if (!MovieScene) return;

	const float S = EditorIcons::GetScale();
	ImDrawList* Draw = ImGui::GetWindowDrawList();
	const ImGuiIO& IO = ImGui::GetIO();
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	const float LabelW = OutlinerWidth * S;
	const float RulerH = RulerHeight * S;
	const float RowH = RowHeight * S;
	const float BottomH = BottomBarHeight * S;
	const float KeyR = KeyRadius * S;

	const float TimelineX = Origin.x + LabelW;
	const float EndX = Origin.x + std::max(Avail.x, LabelW + 80.0f * S);
	const float RowsTop = Origin.y + RulerH;
	const float BottomY = Origin.y + std::max(Avail.y, RulerH + BottomH + RowH) - BottomH;
	const float TimelineW = EndX - TimelineX;
	const float Length = GetLengthSeconds();
	const double Rate = GetDisplayRate();

	if (bFitPending && TimelineW > 1.0f)
	{
		ZoomToFit(TimelineW);
		bFitPending = false;
	}

	TArray<FRow> Rows;
	BuildRows(Rows);
	const float MaxScroll = std::max(0.0f, Rows.Num() * RowH - (BottomY - RowsTop));

	// 휠: Ctrl 줌(마우스 아래 시간 고정) · Shift 팬 · 그냥 휠은 트랙 목록 세로 스크롤
	const bool bMouseInBody = IO.MousePos.x >= Origin.x && IO.MousePos.x < EndX && IO.MousePos.y >= Origin.y && IO.MousePos.y < BottomY;
	if (ImGui::IsWindowHovered() && bMouseInBody && IO.MouseWheel != 0.0f)
	{
		if (IO.KeyCtrl)
		{
			const float MouseTime = XToTime(IO.MousePos.x, TimelineX);
			PixelsPerSecond = std::clamp(PixelsPerSecond * (IO.MouseWheel > 0.0f ? 1.15f : 1.0f / 1.15f), 10.0f, 2000.0f);
			ViewStart = MouseTime - (IO.MousePos.x - TimelineX) / PixelsPerSecond;
		}
		else if (IO.KeyShift)
		{
			ViewStart -= IO.MouseWheel * 40.0f * S / PixelsPerSecond;
		}
		else
		{
			RowScroll -= IO.MouseWheel * RowH * 2.0f;
		}
	}
	RowScroll = std::clamp(RowScroll, 0.0f, MaxScroll);

	// 재생 중 화면 따라가기
	if (bAutoScroll && Player->IsPlaying())
	{
		const float PlayX = TimeToX(GetPlayheadSeconds(), TimelineX);
		if (PlayX > EndX - 24.0f * S || PlayX < TimelineX)
			ViewStart = GetPlayheadSeconds() - TimelineW * 0.1f / PixelsPerSecond;
	}
	ClampView();

	// 배경
	Draw->AddRectFilled(Origin, { TimelineX, BottomY }, ColorOutlinerBg);
	Draw->AddRectFilled({ TimelineX, Origin.y }, { EndX, RowsTop }, ColorRulerBg);
	Draw->AddRectFilled({ TimelineX, RowsTop }, { EndX, BottomY }, ColorTrackOutside);
	const float RangeX0 = std::clamp(TimeToX(0.0f, TimelineX), TimelineX, EndX);
	const float RangeX1 = std::clamp(TimeToX(Length, TimelineX), TimelineX, EndX);
	if (RangeX1 > RangeX0) Draw->AddRectFilled({ RangeX0, RowsTop }, { RangeX1, BottomY }, ColorTrackInside);

	// 눈금자 (프레임 단위, UE처럼 0000 네 자리)
	const float PxPerFrame = static_cast<float>(PixelsPerSecond / Rate);
	const int32 MajorSteps[] = { 1, 2, 5, 10, 15, 30, 60, 150, 300, 600, 1500, 3000, 6000, 15000 };
	int32 Major = MajorSteps[IM_ARRAYSIZE(MajorSteps) - 1];
	for (int32 Candidate : MajorSteps)
		if (Candidate * PxPerFrame >= 64.0f * S) { Major = Candidate; break; }
	int32 Minor = (Major % 5 == 0) ? Major / 5 : (Major % 3 == 0 ? Major / 3 : 1);
	if (Minor * PxPerFrame < 5.0f * S) Minor = Major;

	const ImU32 TextColor = ColorRulerText;
	const float FontH = ImGui::GetFontSize();
	Draw->PushClipRect({ TimelineX, Origin.y }, { EndX, BottomY }, true);
	for (int64 Frame = static_cast<int64>(std::floor(ViewStart * Rate / Minor)) * Minor;; Frame += Minor)
	{
		const float X = TimelineX + static_cast<float>((Frame / Rate - ViewStart) * PixelsPerSecond);
		if (X >= EndX) break;
		if (X < TimelineX - 1.0f) continue;
		if (Frame % Major == 0)
		{
			Draw->AddLine({ X, RowsTop - 12.0f * S }, { X, RowsTop }, ColorRulerTick);
			Draw->AddLine({ X, RowsTop }, { X, BottomY }, ColorGrid);
			char Label[16];
			std::snprintf(Label, sizeof(Label), "%04d", static_cast<int32>(Frame));
			Draw->AddText({ X + 4.0f * S, RowsTop - FontH - 2.0f * S }, TextColor, Label);
		}
		else
		{
			Draw->AddLine({ X, RowsTop - 5.0f * S }, { X, RowsTop }, IM_COL32(80, 80, 80, 255));
		}
	}
	Draw->PopClipRect();
	Draw->AddLine({ TimelineX, RowsTop - 0.5f }, { EndX, RowsTop - 0.5f }, ColorSplitter);

	ImGui::SetCursorScreenPos({ TimelineX, Origin.y });
	ImGui::InvisibleButton("##Ruler", { TimelineW, RulerH });
	if (ImGui::IsItemActive())
		JumpToSeconds(SnapSeconds(std::clamp(XToTime(IO.MousePos.x, TimelineX), 0.0f, Length)));

	// 왼쪽 위: + Add · 필터 · 검색
	Draw->AddRectFilled(Origin, { TimelineX, RowsTop }, ColorOutlinerBg);
	DrawOutlinerHeader(Origin, LabelW, RulerH);

	UMovieSceneTrack* PendingDeleteTrack = nullptr;
	FGuid PendingDeleteBinding;
	FGuid PendingTrackBinding;
	UClass* PendingTrackClass = nullptr;

	ImGui::PushClipRect({ Origin.x, RowsTop }, { EndX, BottomY }, true);
	for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
	{
		const FRow& Row = Rows[RowIndex];
		const float Y = RowsTop + RowIndex * RowH - RowScroll;
		if (Y + RowH < RowsTop || Y > BottomY) continue;
		const float MidY = Y + RowH * 0.5f;
		const float TextY = MidY - FontH * 0.5f;
		const bool bRowSelected = Row.Type == ERowType::Binding
			? (!SelectedTrack && SelectedBinding.IsValid() && Row.Binding == SelectedBinding)
			: (Row.Track == SelectedTrack && Row.Channel == SelectedChannel);

		const ImU32 LabelBg = bRowSelected ? ColorRowSelected : Row.Type == ERowType::Binding ? ColorBindingRow : ColorOutlinerBg;
		Draw->AddRectFilled({ Origin.x, Y }, { TimelineX, Y + RowH }, LabelBg);
		if (bRowSelected) Draw->AddRectFilled({ TimelineX, Y }, { EndX, Y + RowH }, IM_COL32(60, 120, 200, 28));
		Draw->AddLine({ Origin.x, Y + RowH - 0.5f }, { EndX, Y + RowH - 0.5f }, ColorRowLine);

		ImGui::PushID(RowIndex);
		ImGui::SetCursorScreenPos({ Origin.x, Y });
		const bool bLabelClicked = ImGui::InvisibleButton("##Label", { LabelW, RowH });
		const bool bLabelHovered = ImGui::IsItemHovered();
		if (bLabelHovered && !bRowSelected) Draw->AddRectFilled({ Origin.x, Y }, { TimelineX, Y + RowH }, IM_COL32(255, 255, 255, 10));

		if (Row.Type == ERowType::Binding)
		{
			const FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Row.Binding);
			const bool bResolved = ResolveBinding(Row.Binding) != nullptr;
			EditorIcons::Draw(Draw, EEditorIcon::ClassActor, { Origin.x + 18.0f * S, MidY }, 14.0f * S, IconGray());
			char Label[160];
			std::snprintf(Label, sizeof(Label), "%s%s", Possessable ? Possessable->Name.c_str() : "?", bResolved ? "" : "  (missing)");
			Draw->AddText({ Origin.x + 32.0f * S, TextY }, bResolved ? ImGui::GetColorU32(ImGuiCol_Text) : IM_COL32(230, 110, 90, 255), Label);
			if (bLabelClicked) { ClearSelection(); SelectedBinding = Row.Binding; }
			if (ImGui::BeginPopupContextItem("##BindingMenu"))
			{
				if (ImGui::MenuItem("Add Transform Track")) { PendingTrackBinding = Row.Binding; PendingTrackClass = UMovieScene3DTransformTrack::StaticClass(); }
				ImGui::SetNextItemWidth(140.0f * S);
				ImGui::InputText("##Property", PropertyNameBuffer, sizeof(PropertyNameBuffer));
				ImGui::SameLine();
				if (ImGui::MenuItem("Add Float Track")) { PendingTrackBinding = Row.Binding; PendingTrackClass = UMovieSceneFloatTrack::StaticClass(); }
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Binding")) PendingDeleteBinding = Row.Binding;
				ImGui::EndPopup();
			}
			ImGui::PopID();
			continue;
		}

		UMovieSceneTrack* Track = Row.Track;
		UMovieSceneSection* KeySection = GetKeySection(Track);
		const float Indent = (Row.Binding.IsValid() ? 18.0f : 0.0f) * S;
		const bool bHasChannels = KeySection && KeySection->GetNumChannels() > 0;
		if (bLabelClicked)
		{
			SelectedTrack = Track;
			SelectedBinding = Row.Binding;
			SelectedChannel = Row.Channel;
			SelectedKey = -1;
			if (Row.Type == ERowType::Track && bHasChannels && IO.MousePos.x < Origin.x + Indent + 26.0f * S) Track->bExpanded = !Track->bExpanded;
		}
		const ImU32 LabelColor = Track->bIsEvalDisabled ? IM_COL32(120, 120, 120, 255) : ImGui::GetColorU32(ImGuiCol_Text);
		if (Row.Type == ERowType::Track)
		{
			if (bHasChannels)
				EditorIcons::Draw(Draw, Track->bExpanded ? EEditorIcon::ChevronDown : EEditorIcon::ChevronRight,
					{ Origin.x + Indent + 14.0f * S, MidY }, 9.0f * S, IconGray());
			char Label[160];
			std::snprintf(Label, sizeof(Label), "%s%s", Track->GetDisplayName(), Track->bIsEvalDisabled ? "  (Muted)" : "");
			Draw->AddText({ Origin.x + Indent + 26.0f * S, TextY }, LabelColor, Label);
			if (ImGui::BeginPopupContextItem("##TrackMenu"))
			{
				ImGui::MenuItem("Mute", nullptr, &Track->bIsEvalDisabled);
				if (ImGui::MenuItem("Delete Track")) PendingDeleteTrack = Track;
				ImGui::EndPopup();
			}
		}
		else
		{
			Draw->AddText({ Origin.x + Indent + 44.0f * S, TextY },
				Track->bIsEvalDisabled ? IM_COL32(110, 110, 110, 255) : ImGui::GetColorU32(ImGuiCol_Text, 0.75f),
				KeySection ? KeySection->GetChannelName(Row.Channel) : "");
		}

		Draw->PushClipRect({ TimelineX, std::max(Y, RowsTop) }, { EndX, std::min(Y + RowH, BottomY) }, true);

		if (Row.Type == ERowType::Track)
		{
			// 섹션 막대 (열린 쪽은 재생 범위 끝까지)
			int32 SectionIndex = 0;
			for (UMovieSceneSection* Section : Track->GetAllSections())
			{
				const float Start = Section->HasStartFrame() ? FrameToSeconds(Section->GetInclusiveStartFrame()) : 0.0f;
				const float End = Section->HasEndFrame() ? FrameToSeconds(Section->GetExclusiveEndFrame()) : Length;
				const ImVec2 Min{ TimeToX(Start, TimelineX) + 1.0f, Y + 2.0f * S };
				const ImVec2 Max{ TimeToX(End, TimelineX) - 1.0f, Y + RowH - 2.0f * S };
				const ImU32 Fill = (SectionIndex++ % 2) ? IM_COL32(110, 82, 50, 220) : IM_COL32(52, 96, 112, 220);
				Draw->AddRectFilled(Min, Max, Section->IsActive() ? Fill : IM_COL32(60, 60, 60, 200), 3.0f * S);
				Draw->AddRect(Min, Max, IM_COL32(0, 0, 0, 120), 3.0f * S);
				if (const UMovieSceneCameraCutSection* Cut = Cast<UMovieSceneCameraCutSection>(Section))
				{
					const FMovieScenePossessable* Camera = MovieScene->FindPossessable(Cut->GetCameraBindingID().GetGuid());
					EditorIcons::Draw(Draw, EEditorIcon::Camera, { Min.x + 11.0f * S, MidY }, 12.0f * S, IM_COL32(235, 235, 235, 255));
					Draw->AddText({ Min.x + 22.0f * S, TextY }, IM_COL32(235, 235, 235, 255), Camera ? Camera->Name.c_str() : "(no camera)");
				}
				if (const UMovieSceneEventTriggerSection* Events = Cast<UMovieSceneEventTriggerSection>(Section))
				{
					for (const FFrameNumber& Time : Events->EventChannel.Times)
					{
						const float X = TimeToX(FrameToSeconds(Time), TimelineX);
						Draw->AddRectFilled({ X - 2.0f * S, Y + 4.0f * S }, { X + 2.0f * S, Y + RowH - 4.0f * S }, IM_COL32(226, 194, 125, 255));
					}
				}
				// 접힌/펼친 트랙 줄에도 채널 키를 요약해서 보여 준다
				for (int32 Index = 0; Index < Section->GetNumChannels(); ++Index)
					if (FMovieSceneFloatChannel* Channel = Section->GetChannel(Index))
						for (const FFrameNumber& Time : Channel->GetTimes())
							DrawKeyDiamond(Draw, { TimeToX(FrameToSeconds(Time), TimelineX), MidY }, KeyR * 0.8f, IM_COL32(170, 170, 170, 255));
			}
		}
		else if (KeySection)
		{
			FMovieSceneFloatChannel* Channel = KeySection->GetChannel(Row.Channel);
			const int32 NumKeys = Channel ? Channel->GetNumKeys() : 0;
			for (int32 KeyIndex = 0; KeyIndex < NumKeys; ++KeyIndex)
			{
				const float KeySeconds = FrameToSeconds(Channel->GetTimes()[KeyIndex]);
				const float X = TimeToX(KeySeconds, TimelineX);
				if (X < TimelineX - KeyR || X > EndX + KeyR) continue;
				const ImVec2 Center{ X, MidY };

				ImGui::PushID(KeyIndex);
				ImGui::SetCursorScreenPos({ X - KeyR, Center.y - KeyR });
				ImGui::InvisibleButton("##Key", { KeyR * 2.0f, KeyR * 2.0f });
				if (ImGui::IsItemActivated())
				{
					SelectedTrack = Track;
					SelectedBinding = Row.Binding;
					SelectedChannel = Row.Channel;
					SelectedKey = KeyIndex;
				}
				if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
				{
					// 이웃 키를 넘지 못하게 막아 정렬과 인덱스를 유지한다
					const TArray<FFrameNumber>& Times = Channel->GetTimes();
					const int32 MinFrame = KeyIndex > 0 ? Times[KeyIndex - 1].Value + 1 : SecondsToFrame(0.0f).Value;
					const int32 MaxFrame = KeyIndex + 1 < NumKeys ? Times[KeyIndex + 1].Value - 1 : SecondsToFrame(Length).Value;
					const int32 NewFrame = std::clamp(SecondsToFrame(SnapSeconds(XToTime(IO.MousePos.x, TimelineX))).Value, MinFrame, std::max(MinFrame, MaxFrame));
					SelectedKey = Channel->SetKeyTime(KeyIndex, FFrameNumber(NewFrame));
					Player->Refresh();
				}
				const bool bKeySelected = bRowSelected && SelectedKey == KeyIndex;
				const bool bKeyHovered = ImGui::IsItemHovered();
				DrawKeyDiamond(Draw, Center, KeyR, bKeySelected ? ColorKeySelected : bKeyHovered ? IM_COL32_WHITE : ColorKey);
				ImGui::PopID();
			}

			// 빈 곳 더블클릭: 현재 평가 값으로 키 추가
			const bool bInRow = IO.MousePos.x >= TimelineX && IO.MousePos.x < EndX && IO.MousePos.y >= std::max(Y, RowsTop) && IO.MousePos.y < std::min(Y + RowH, BottomY);
			if (Channel && bInRow && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				const FFrameNumber Frame = SecondsToFrame(SnapSeconds(std::clamp(XToTime(IO.MousePos.x, TimelineX), 0.0f, Length)));
				float Value = 0.0f;
				Channel->Evaluate(FFrameTime(Frame), Value);
				SelectedTrack = Track;
				SelectedBinding = Row.Binding;
				SelectedChannel = Row.Channel;
				SelectedKey = Channel->UpdateOrAddKey(Frame, Value);
				Player->Refresh();
			}
		}

		Draw->PopClipRect();
		ImGui::PopID();
	}
	ImGui::PopClipRect();

	if (Rows.IsEmpty())
	{
		const char* Hint = "Click  + Add  to bind a level actor or add a track.";
		Draw->AddText({ Origin.x + 12.0f * S, RowsTop + 10.0f * S }, ImGui::GetColorU32(ImGuiCol_TextDisabled), Hint);
	}

	// 재생 범위 경계 (초록 시작 · 빨강 끝)와 재생 헤드
	Draw->PushClipRect({ TimelineX, Origin.y }, { EndX, BottomY }, true);
	const float StartX = TimeToX(0.0f, TimelineX);
	const float LengthX = TimeToX(Length, TimelineX);
	Draw->AddLine({ StartX, RowsTop }, { StartX, BottomY }, ColorRangeStart, 1.5f * S);
	Draw->AddLine({ LengthX, Origin.y }, { LengthX, BottomY }, ColorRangeEnd, 1.5f * S);

	const float PlayX = TimeToX(GetPlayheadSeconds(), TimelineX);
	Draw->AddLine({ PlayX, RowsTop }, { PlayX, BottomY }, IM_COL32(225, 225, 225, 255), 1.0f * S);
	const float TabHalf = 6.0f * S;
	const float TabTop = Origin.y + 2.0f * S;
	const float TabBottom = RowsTop - 9.0f * S;
	Draw->AddRectFilled({ PlayX - TabHalf, TabTop }, { PlayX + TabHalf, TabBottom }, ColorPlayhead, 2.0f * S, ImDrawFlags_RoundCornersTop);
	Draw->AddTriangleFilled({ PlayX - TabHalf, TabBottom }, { PlayX + TabHalf, TabBottom }, { PlayX, RowsTop - 2.0f * S }, ColorPlayhead);
	char PlayLabel[16];
	std::snprintf(PlayLabel, sizeof(PlayLabel), "%04d", SecondsToDisplayFrame(GetPlayheadSeconds()));
	Draw->AddText({ PlayX + TabHalf + 4.0f * S, TabTop }, IM_COL32_WHITE, PlayLabel);
	Draw->PopClipRect();

	// 패널 경계선
	Draw->AddLine({ TimelineX, Origin.y }, { TimelineX, BottomY + BottomH }, ColorSplitter, 1.0f * S);

	// 아래 막대: 트랜스포트 · 범위
	Draw->AddRectFilled({ Origin.x, BottomY }, { EndX, BottomY + BottomH }, ColorOutlinerBg);
	Draw->AddLine({ Origin.x, BottomY }, { EndX, BottomY }, ColorSplitter, 1.0f * S);
	DrawTransportBar({ Origin.x, BottomY }, LabelW, BottomH);
	DrawRangeBar({ TimelineX, BottomY }, TimelineW, BottomH);

	// 선택한 키 Delete
	if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Delete) && SelectedKey >= 0)
		DeleteSelectedKey();

	if (PendingTrackClass)
	{
		UMovieSceneTrack* Track = MovieScene->AddTrack(PendingTrackClass, PendingTrackBinding);
		if (UMovieScenePropertyTrack* Property = Cast<UMovieScenePropertyTrack>(Track))
			Property->SetPropertyNameAndPath(PropertyNameBuffer);
	}
	if (PendingDeleteTrack)
	{
		Player->Stop();
		MovieScene->RemoveTrack(PendingDeleteTrack);
		ClearSelection();
	}
	if (PendingDeleteBinding.IsValid())
	{
		Player->Stop();
		MovieScene->RemovePossessable(PendingDeleteBinding);
		Sequence->UnbindPossessableObjects(PendingDeleteBinding);
		ClearSelection();
	}

	ImGui::SetCursorScreenPos({ Origin.x, BottomY + BottomH });
	ImGui::Dummy({ 1.0f, 0.0f });
}

void FSequencerPanel::DrawTransportBar(const ImVec2& Min, float Width, float Height)
{
	const float S = EditorIcons::GetScale();
	const float BtnH = 24.0f * S;
	const float Y = Min.y + (Height - BtnH) * 0.5f;
	float X = Min.x + 8.0f * S;
	ImDrawList* Draw = ImGui::GetWindowDrawList();

	auto Transport = [&](const char* Id, EEditorIcon Icon, const char* Tip, bool bActive = false) -> bool
	{
		if (bActive) Draw->AddRectFilled({ X, Y }, { X + BtnH, Y + BtnH }, ColorActiveButton, 3.0f * S);
		ImGui::SetCursorScreenPos({ X, Y });
		const bool bPressed = EditorIcons::Button(Id, Icon, bActive ? IM_COL32_WHITE : IconGray(), nullptr, false, false, BtnH);
		ItemTooltip(Tip);
		X += BtnH + 1.0f * S;
		return bPressed;
	};

	if (Transport("##ToStart", EEditorIcon::ToStart, "Jump to Start")) JumpToSeconds(0.0f);
	if (Transport("##PrevKey", EEditorIcon::PrevKey, "Previous Key")) JumpToAdjacentKey(false);
	if (Transport("##StepBack", EEditorIcon::StepBack, "Step Back One Frame")) StepFrames(-1);
	if (Transport("##Play", Player->IsPlaying() ? EEditorIcon::Pause : EEditorIcon::Play, Player->IsPlaying() ? "Pause" : "Play")) TogglePlay();
	if (Transport("##Stop", EEditorIcon::Stop, "Stop (restore state)")) Player->Stop();
	if (Transport("##StepFwd", EEditorIcon::StepForward, "Step Forward One Frame")) StepFrames(1);
	if (Transport("##NextKey", EEditorIcon::NextKey, "Next Key")) JumpToAdjacentKey(true);
	if (Transport("##ToEnd", EEditorIcon::ToEnd, "Jump to End")) JumpToSeconds(GetLengthSeconds());
	FMovieSceneSequencePlaybackSettings& Settings = Player->GetPlaybackSettings();
	const bool bLoop = Settings.LoopCount.Value != 0;
	if (Transport("##Loop", EEditorIcon::Loop, "Loop Playback", bLoop)) Settings.LoopCount.Value = bLoop ? 0 : -1;

	// 현재 프레임
	const float FieldX = X + 6.0f * S;
	const float FieldW = Min.x + Width - 8.0f * S - FieldX;
	if (FieldW > 30.0f * S)
	{
		int32 Frame = SecondsToDisplayFrame(GetPlayheadSeconds());
		if (FrameField("##CurrentFrame", Frame, { FieldX, Min.y }, FieldW, Height, IM_COL32(0, 0, 0, 0)))
			JumpToSeconds(static_cast<float>(Frame / GetDisplayRate()));
	}
}

void FSequencerPanel::DrawRangeBar(const ImVec2& Min, float Width, float Height)
{
	UMovieScene* MovieScene = GetMovieScene();
	const float S = EditorIcons::GetScale();
	const ImGuiIO& IO = ImGui::GetIO();
	ImDrawList* Draw = ImGui::GetWindowDrawList();
	const double Rate = GetDisplayRate();
	const float Length = GetLengthSeconds();
	const float FieldW = 50.0f * S;
	const float Pad = 6.0f * S;
	const float ViewEnd = ViewStart + Width / PixelsPerSecond;
	const ImU32 Gray = IM_COL32(90, 90, 90, 255);

	// [보기 시작][재생 시작] ====스크롤==== [재생 끝][보기 끝]
	float X = Min.x + Pad;
	int32 ViewStartFrame = static_cast<int32>(std::lround(ViewStart * Rate));
	if (FrameField("##ViewStart", ViewStartFrame, { X, Min.y }, FieldW, Height, Gray))
	{
		ViewStart = static_cast<float>(ViewStartFrame / Rate);
		ClampView();
	}
	X += FieldW + Pad;
	int32 PlayStartFrame = 0;
	FrameField("##PlayStart", PlayStartFrame, { X, Min.y }, FieldW, Height, ColorRangeStart, false);
	X += FieldW + Pad;

	const float RightX = Min.x + Width - Pad - FieldW * 2.0f - Pad * 2.0f;
	if (RightX - X > 30.0f * S)
	{
		// 스크롤 막대: 전체 = 재생 범위(+여유)와 현재 보기를 합친 구간
		const float TrackX0 = X + Pad;
		const float TrackX1 = RightX;
		const float TrackW = TrackX1 - TrackX0;
		const float BarH = 10.0f * S;
		const float BarY = Min.y + (Height - BarH) * 0.5f;
		const float Lead = std::max(0.5f, Length * 0.03f);
		const float Total0 = std::min(ViewStart, -Lead);
		const float Total1 = std::max(ViewEnd, Length + Lead);
		const float Span = std::max(Total1 - Total0, 1e-3f);
		const float Thumb0 = TrackX0 + (ViewStart - Total0) / Span * TrackW;
		const float Thumb1 = TrackX0 + (ViewEnd - Total0) / Span * TrackW;

		Draw->AddRectFilled({ TrackX0, BarY }, { TrackX1, BarY + BarH }, IM_COL32(16, 16, 16, 255), BarH * 0.5f);
		ImGui::SetCursorScreenPos({ TrackX0, BarY - 3.0f * S });
		ImGui::InvisibleButton("##ViewScroll", { TrackW, BarH + 6.0f * S });
		const bool bActive = ImGui::IsItemActive();
		const bool bHovered = ImGui::IsItemHovered();
		if (bActive && IO.MouseDelta.x != 0.0f)
		{
			ViewStart += IO.MouseDelta.x * Span / TrackW;
			ClampView();
		}
		const ImU32 ThumbColor = bActive ? IM_COL32(150, 150, 150, 255) : bHovered ? IM_COL32(125, 125, 125, 255) : IM_COL32(95, 95, 95, 255);
		Draw->AddRectFilled({ std::max(Thumb0, TrackX0), BarY + 1.0f * S }, { std::min(Thumb1, TrackX1), BarY + BarH - 1.0f * S }, ThumbColor, BarH * 0.5f);
	}

	X = Min.x + Width - Pad - FieldW * 2.0f - Pad;
	int32 PlayEndFrame = static_cast<int32>(std::lround(Length * Rate));
	if (FrameField("##PlayEnd", PlayEndFrame, { X, Min.y }, FieldW, Height, ColorRangeEnd) && MovieScene)
	{
		const float NewLength = static_cast<float>(std::max(PlayEndFrame, 1) / Rate);
		MovieScene->SetPlaybackRange(MovieScene->GetPlaybackRange().GetLowerBoundValue(),
			SecondsToFrame(NewLength).Value - MovieScene->GetPlaybackRange().GetLowerBoundValue().Value);
	}
	X += FieldW + Pad;
	int32 ViewEndFrame = static_cast<int32>(std::lround(ViewEnd * Rate));
	if (FrameField("##ViewEnd", ViewEndFrame, { X, Min.y }, FieldW, Height, Gray))
	{
		const float NewEnd = static_cast<float>(ViewEndFrame / Rate);
		if (NewEnd > ViewStart + 1.0f / static_cast<float>(Rate))
			PixelsPerSecond = std::clamp(Width / (NewEnd - ViewStart), 10.0f, 2000.0f);
	}
}

void FSequencerPanel::DrawDetails()
{
	UMovieScene* MovieScene = GetMovieScene();
	if (!MovieScene || !SelectedTrack)
	{
		ImGui::TextDisabled("+ Actor binds a level actor. Right-click a binding to add tracks. Ctrl+Wheel zoom, Shift+Wheel pan.");
		ImGui::TextDisabled("Double-click a channel row to add a key. Key records the bound actor's current values at the playhead.");
		return;
	}
	if (SelectedTrack == MovieScene->GetCameraCutTrack())
	{
		DrawCameraCutDetails();
		return;
	}

	UMovieSceneSection* Section = GetKeySection(SelectedTrack);
	if (!Section) return;
	if (Cast<UMovieSceneEventTriggerSection>(Section))
	{
		DrawEventDetails(*Section);
		return;
	}

	FMovieSceneFloatChannel* Channel = SelectedChannel >= 0 ? Section->GetChannel(SelectedChannel) : nullptr;
	if (!Channel)
	{
		DrawSectionDetails(*Section);
		return;
	}
	DrawChannelDetails(*Section, *Channel);
	ImGui::SameLine();
	DrawCurvePreview(*Channel);
}

void FSequencerPanel::DrawSectionDetails(UMovieSceneSection& Section)
{
	const char* CompletionNames[] = { "Keep State", "Restore State", "Project Default" };
	int32 Completion = static_cast<int32>(Section.EvalOptions.CompletionMode);
	ImGui::SetNextItemWidth(160.0f);
	if (ImGui::Combo("When Finished", &Completion, CompletionNames, IM_ARRAYSIZE(CompletionNames)))
		Section.EvalOptions.CompletionMode = static_cast<EMovieSceneCompletionMode>(Completion);
	bool bActive = Section.IsActive();
	if (ImGui::Checkbox("Active", &bActive)) { Section.SetIsActive(bActive); Player->Refresh(); }

	if (UMovieSceneFadeSection* Fade = Cast<UMovieSceneFadeSection>(&Section))
	{
		if (ImGui::ColorEdit3("Fade Color", Fade->FadeColor.RGBA)) Player->Refresh();
	}
	if (UMovieSceneCameraOverlaySection* Overlay = Cast<UMovieSceneCameraOverlaySection>(&Section))
	{
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::DragFloat("Letterbox Aspect", &Overlay->LetterboxAspect, 0.01f, 1.0f, 4.0f, "%.2f")) Player->Refresh();
	}
	if (UMovieScenePropertyTrack* Property = Cast<UMovieScenePropertyTrack>(SelectedTrack))
		ImGui::Text("Property: %s", Property->GetPropertyBinding().PropertyName.c_str());
	ImGui::TextDisabled("Select a channel row to edit keys");
}

void FSequencerPanel::DrawChannelDetails(UMovieSceneSection& Section, FMovieSceneFloatChannel& Channel)
{
	ImGui::BeginGroup();
	if (SelectedKey < 0 || SelectedKey >= Channel.GetNumKeys())
	{
		ImGui::TextDisabled("No key selected");
		ImGui::EndGroup();
		return;
	}

	const double TickRate = GetMovieScene()->GetTickResolution().AsDecimal();
	FMovieSceneFloatValue& Key = Channel.GetValues()[SelectedKey];
	bool bChanged = false;

	float Seconds = FrameToSeconds(Channel.GetTimes()[SelectedKey]);
	ImGui::SetNextItemWidth(120.0f);
	if (ImGui::DragFloat("Time", &Seconds, 0.01f, 0.0f, GetLengthSeconds(), "%.3f s"))
	{
		SelectedKey = Channel.SetKeyTime(SelectedKey, SecondsToFrame(Seconds));
		Player->Refresh();
		ImGui::EndGroup();
		return;
	}
	ImGui::SetNextItemWidth(120.0f);
	bChanged |= ImGui::DragFloat("Value", &Key.Value, 0.05f);

	const char* InterpNames[] = { "Linear", "Constant", "Cubic" };
	int32 Interp = static_cast<int32>(Key.InterpMode);
	ImGui::SetNextItemWidth(120.0f);
	if (ImGui::Combo("Interp", &Interp, InterpNames, IM_ARRAYSIZE(InterpNames)))
	{
		Key.InterpMode = static_cast<ERichCurveInterpMode>(Interp);
		bChanged = true;
	}
	const char* TangentNames[] = { "Auto", "User", "Break" };
	int32 TangentMode = static_cast<int32>(Key.TangentMode);
	ImGui::SetNextItemWidth(120.0f);
	if (ImGui::Combo("Tangent", &TangentMode, TangentNames, IM_ARRAYSIZE(TangentNames)))
	{
		Key.TangentMode = static_cast<ERichCurveTangentMode>(TangentMode);
		bChanged = true;
	}

	// 저장은 값/틱(UE와 같음). 편집은 값/초
	ImGui::BeginDisabled(Key.TangentMode == RCTM_Auto);
	float Arrive = static_cast<float>(Key.Tangent.ArriveTangent * TickRate);
	float Leave = static_cast<float>(Key.Tangent.LeaveTangent * TickRate);
	ImGui::SetNextItemWidth(120.0f);
	if (ImGui::DragFloat("Arrive /s", &Arrive, 0.05f))
	{
		Key.Tangent.ArriveTangent = static_cast<float>(Arrive / TickRate);
		if (Key.TangentMode == RCTM_User) Key.Tangent.LeaveTangent = Key.Tangent.ArriveTangent;
		bChanged = true;
	}
	ImGui::SetNextItemWidth(120.0f);
	if (ImGui::DragFloat("Leave /s", &Leave, 0.05f))
	{
		Key.Tangent.LeaveTangent = static_cast<float>(Leave / TickRate);
		if (Key.TangentMode == RCTM_User) Key.Tangent.ArriveTangent = Key.Tangent.LeaveTangent;
		bChanged = true;
	}
	ImGui::EndDisabled();

	const bool bDelete = ImGui::Button("Delete Key");
	ImGui::EndGroup();

	if (bDelete)
	{
		Channel.RemoveKey(SelectedKey);
		SelectedKey = -1;
		Player->Refresh();
	}
	else if (bChanged)
	{
		Channel.AutoSetTangents();
		Player->Refresh();
	}
}

void FSequencerPanel::DrawCurvePreview(const FMovieSceneFloatChannel& Channel)
{
	const ImVec2 Size{ std::max(ImGui::GetContentRegionAvail().x, 100.0f), 160.0f };
	const ImVec2 P = ImGui::GetCursorScreenPos();
	ImDrawList* Draw = ImGui::GetWindowDrawList();
	Draw->AddRectFilled(P, { P.x + Size.x, P.y + Size.y }, IM_COL32(22, 24, 26, 255));
	ImGui::Dummy(Size);
	const float Length = GetLengthSeconds();
	if (Channel.GetNumKeys() == 0 || Length <= 0.0f) return;

	constexpr int32 Samples = 160;
	float Values[Samples + 1];
	float MinValue = Channel.GetValues()[0].Value;
	float MaxValue = MinValue;
	for (int32 Index = 0; Index <= Samples; ++Index)
	{
		Values[Index] = 0.0f;
		Channel.Evaluate(FFrameTime(SecondsToFrame(Length * Index / Samples)), Values[Index]);
		MinValue = std::min(MinValue, Values[Index]);
		MaxValue = std::max(MaxValue, Values[Index]);
	}
	const float Range = std::max(MaxValue - MinValue, 1e-3f);
	auto ToScreen = [&](float Seconds, float Value)
	{
		return ImVec2{ P.x + Size.x * Seconds / Length, P.y + Size.y - 8.0f - (Size.y - 16.0f) * (Value - MinValue) / Range };
	};
	for (int32 Index = 0; Index < Samples; ++Index)
		Draw->AddLine(ToScreen(Length * Index / Samples, Values[Index]), ToScreen(Length * (Index + 1) / Samples, Values[Index + 1]),
			IM_COL32(120, 200, 230, 255), 1.5f);
	for (int32 Index = 0; Index < Channel.GetNumKeys(); ++Index)
		DrawDiamond(Draw, ToScreen(FrameToSeconds(Channel.GetTimes()[Index]), Channel.GetValues()[Index].Value), 4.0f,
			Index == SelectedKey ? ColorKeySelected : ColorKey);
	char RangeText[48];
	std::snprintf(RangeText, sizeof(RangeText), "%.2f .. %.2f", MinValue, MaxValue);
	Draw->AddText({ P.x + 4.0f, P.y + 2.0f }, IM_COL32(150, 150, 150, 255), RangeText);
	const float PlayX = P.x + Size.x * GetPlayheadSeconds() / Length;
	Draw->AddLine({ PlayX, P.y }, { PlayX, P.y + Size.y }, ColorPlayhead);
}

void FSequencerPanel::DrawCameraCutDetails()
{
	UMovieScene* MovieScene = GetMovieScene();
	UMovieSceneCameraCutTrack* Track = Cast<UMovieSceneCameraCutTrack>(MovieScene->GetCameraCutTrack());
	if (!Track) return;

	ImGui::SetNextItemWidth(180.0f);
	BindingCombo("Camera", MovieScene, NewCutCamera);
	ImGui::SameLine();
	ImGui::BeginDisabled(!NewCutCamera.IsValid());
	if (ImGui::Button("Add Cut at Playhead"))
	{
		Track->AddNewCameraCut(FMovieSceneObjectBindingID(NewCutCamera), SecondsToFrame(SnapSeconds(GetPlayheadSeconds())));
		Player->Refresh();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Checkbox("Can Blend", &Track->bCanBlend);

	UMovieSceneSection* RemoveSection = nullptr;
	int32 Index = 0;
	for (UMovieSceneSection* Section : Track->GetAllSections())
	{
		UMovieSceneCameraCutSection* Cut = static_cast<UMovieSceneCameraCutSection*>(Section);
		ImGui::PushID(Index++);
		float Start = FrameToSeconds(Cut->GetInclusiveStartFrame());
		ImGui::SetNextItemWidth(90.0f);
		ImGui::DragFloat("##Start", &Start, 0.01f, 0.0f, GetLengthSeconds(), "%.2f s");
		if (ImGui::IsItemEdited()) Cut->SetStartFrame(SecondsToFrame(Start));
		if (ImGui::IsItemDeactivatedAfterEdit()) { Track->RearrangeAllSections(); Player->Refresh(); }
		ImGui::SameLine();
		FGuid Camera = Cut->GetCameraBindingID().GetGuid();
		ImGui::SetNextItemWidth(180.0f);
		if (BindingCombo("##Camera", MovieScene, Camera))
		{
			Cut->SetCameraBindingID(FMovieSceneObjectBindingID(Camera));
			Player->Refresh();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("X")) RemoveSection = Section;
		ImGui::PopID();
	}
	if (RemoveSection)
	{
		Track->RemoveSection(RemoveSection);
		Track->RearrangeAllSections();
		Player->Refresh();
	}
}

void FSequencerPanel::DrawEventDetails(UMovieSceneSection& Section)
{
	UMovieSceneEventTriggerSection& Events = static_cast<UMovieSceneEventTriggerSection&>(Section);
	const char* KindNames[] = { "HitStop", "Log" };
	int32 Kind = static_cast<int32>(NewEvent.Kind);
	ImGui::SetNextItemWidth(100.0f);
	if (ImGui::Combo("Kind", &Kind, KindNames, IM_ARRAYSIZE(KindNames))) NewEvent.Kind = static_cast<EMovieSceneEventKind>(Kind);
	ImGui::SameLine();
	if (NewEvent.Kind == EMovieSceneEventKind::HitStop)
	{
		ImGui::SetNextItemWidth(80.0f);
		ImGui::DragFloat("Duration", &NewEvent.Duration, 0.01f, 0.01f, 2.0f, "%.2f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::DragFloat("Dilation", &NewEvent.Dilation, 0.01f, 0.0001f, 1.0f, "%.3f");
	}
	else
	{
		char Buffer[128];
		std::snprintf(Buffer, sizeof(Buffer), "%s", NewEvent.Message.c_str());
		ImGui::SetNextItemWidth(200.0f);
		if (ImGui::InputText("Message", Buffer, sizeof(Buffer))) NewEvent.Message = Buffer;
	}
	ImGui::SameLine();
	if (ImGui::Button("Add at Playhead"))
		Events.EventChannel.AddKey(SecondsToFrame(SnapSeconds(GetPlayheadSeconds())), NewEvent);

	int32 RemoveIndex = -1;
	for (int32 Index = 0; Index < Events.EventChannel.GetNumKeys(); ++Index)
	{
		const FMovieSceneEvent& Event = Events.EventChannel.Events[Index];
		ImGui::PushID(Index);
		float Seconds = FrameToSeconds(Events.EventChannel.Times[Index]);
		ImGui::SetNextItemWidth(90.0f);
		ImGui::DragFloat("##Time", &Seconds, 0.01f, 0.0f, GetLengthSeconds(), "%.2f s");
		if (ImGui::IsItemDeactivatedAfterEdit()) Events.EventChannel.SetKeyTime(Index, SecondsToFrame(SnapSeconds(Seconds)));
		ImGui::SameLine();
		if (Event.Kind == EMovieSceneEventKind::HitStop) ImGui::Text("HitStop %.2fs x%.3f", Event.Duration, Event.Dilation);
		else ImGui::Text("Log \"%s\"", Event.Message.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("X")) RemoveIndex = Index;
		ImGui::PopID();
	}
	if (RemoveIndex >= 0) Events.EventChannel.RemoveKey(RemoveIndex);
}
