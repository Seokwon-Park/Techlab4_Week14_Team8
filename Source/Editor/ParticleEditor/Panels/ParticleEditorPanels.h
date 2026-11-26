#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"

class FParticleSystemEditor;
class UParticleEmitter;
class UParticleLODLevel;
class UParticleModule;
class UClass;

// 에디터 타입별 패널 베이스는 별칭만 둔다 (FUIEditorPanel과 같은 방식)
using FParticleEditorPanel = TAssetEditorPanel<FParticleSystemEditor>;
inline constexpr const char* ParticlePanelIdPrefix = "ParticleEditorPanel";

class FParticleViewportPanel : public FParticleEditorPanel
{
public:
	explicit FParticleViewportPanel(FParticleSystemEditor& InEditor);
protected:
	void DrawContent() override;
	ImGuiWindowFlags GetWindowFlags() const override { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }
};

class FParticleEmittersPanel : public FParticleEditorPanel
{
public:
	explicit FParticleEmittersPanel(FParticleSystemEditor& InEditor);
protected:
	void DrawContent() override;
private:
	void DrawEmitterColumn(int32 EmitterIndex);
	void DrawEmitterHeader(int32 EmitterIndex, UParticleEmitter* Emitter, UParticleLODLevel* LOD);
	void DrawModuleRow(int32 EmitterIndex, UParticleLODLevel* LOD, UParticleModule* Module, int32 ModuleIndex);
	void DrawAddModuleMenu(int32 EmitterIndex, UParticleLODLevel* LOD);
};

class FParticleDetailsPanel : public FParticleEditorPanel
{
public:
	explicit FParticleDetailsPanel(FParticleSystemEditor& InEditor);
protected:
	void DrawContent() override;
};

class FParticleCurveEditorPanel : public FParticleEditorPanel
{
public:
	explicit FParticleCurveEditorPanel(FParticleSystemEditor& InEditor);
protected:
	void DrawContent() override;
	ImGuiWindowFlags GetWindowFlags() const override { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }
private:
	struct FKeyRef
	{
		int32 Track = -1;
		int32 Channel = 0;
		int32 Key = -1;
		bool operator==(const FKeyRef&) const = default;
	};

	struct FKeyDragOrigin
	{
		FKeyRef Ref;
		float Time = 0.0f;
		float Value = 0.0f;
	};

	enum class ETangentHandle : uint8 { None, Arrive, Leave };

	void DrawToolbar();
	void DrawTrackList();
	void DrawGraph();
	void DrawContextMenu();
	void FitHorizontal();
	void FitVertical();
	struct FParticleCurve* GetSelectedCurve() const;
	struct FParticleCurve* ResolveCurve(int32 Track, int32 Channel) const;
	bool IsKeySelected(int32 Track, int32 Channel, int32 Key) const;
	void SelectSingleKey(int32 Track, int32 Channel, int32 Key);
	void ToggleKeySelection(int32 Track, int32 Channel, int32 Key);
	void ClearKeySelection();
	void PruneInvalidSelection();
	void DeleteSelectedKeys();
	template <typename Fn>
	void ApplyToSelectedKeys(Fn&& Edit);

	// 보이는 범위 (시간, 값)
	float ViewMinT = -0.05f, ViewMaxT = 1.05f;
	float ViewMinV = -0.1f, ViewMaxV = 1.1f;

	// 주 선택 + 다중 선택
	int32 SelectedTrack = -1;
	int32 SelectedChannel = 0;
	int32 SelectedKey = -1;
	TArray<FKeyRef> SelectedKeys;

	// 키 다중 드래그
	bool bDraggingKey = false;
	float DragAnchorT = 0.0f;
	float DragAnchorV = 0.0f;
	TArray<FKeyDragOrigin> DragOrigins;

	// 탄젠트 핸들 드래그
	ETangentHandle DraggingTangent = ETangentHandle::None;
	FKeyRef TangentDragKey;

	// 마키(박스) 다중 선택 (Shift + 빈 곳 좌드래그)
	bool bBoxSelecting = false;
	ImVec2 BoxSelectStart{};
	ImVec2 BoxSelectEnd{};

	// 우클릭 컨텍스트 메뉴
	bool bRightClickPending = false;
	ImVec2 RightClickStart{};
	float ContextTime = 0.0f;
	float ContextValue = 0.0f;

	// 툴바 Pan / Zoom: 빈 곳을 왼쪽으로 끌 때 할 일
	bool bZoomMode = false;
	bool bDraggingView = false;
	float ViewDragAnchorX = 0.0f, ViewDragAnchorY = 0.0f;   // Zoom 고정점 (화면 좌표)
};