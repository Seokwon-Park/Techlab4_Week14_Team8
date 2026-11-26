#pragma once

#include <format>
#include "Editor/EditorUI/EditorPanel.h"
#include "Core/EngineLog.h"

#include <functional>

struct FEditorSettings;

struct FLogData
{
	FString message;
	ImVec4 Color;
	ELogVerbosity Verbosity = ELogVerbosity::Info;
};

using FConsoleHandler = std::function<bool(const TArray<FString>& Args, const FString& Line)>;

struct FConsoleCommand
{
	FString Name;
	FString Usage;
	FConsoleHandler Handler;
};

struct FConsoleSuggestion
{
	FString Badge;
	FString DisplayText;
	FString InsertText;
	FString Hint;
	bool bIsCVar = false;
};

class FOutputLogPanel : public IEditorPanel, public ILogSink
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	void RenderContents();
	const char* GetPanelName() const override { return "Output Log"; }

	// Content Drawer와 동일하게 상태 표시줄 위 드로어로 토글하며, "Dock in Layout"으로 도킹 창 전환 가능.
	void ToggleDrawer();
	void OpenDrawer();
	void CloseDrawer() { bDrawerOpen = false; }
	bool IsDrawerOpen() const { return bDrawerOpen; }
	bool IsDocked() const { return bDocked; }
	void SetDrawerRect(const ImVec2& Pos, const ImVec2& Size, ImGuiID ViewportId);

	void ClearLog();
	// ImGui 컨텍스트가 만들어진 뒤 엔진이 부른다.
	void LoadLogFont();

	void ExecCommand(const FString& command_line);
	void RenderCommandInput(const char* InputId, char* Buffer, size_t BufferSize, float Width, bool bUseLogFont = true, bool bReclaimFocusOnSubmit = true);

	int TextEditCallback(ImGuiInputTextCallbackData* data);
	void SetEditorSettings(FEditorSettings* InSettings) { EditorSettings = InSettings; }

private:
	bool BeginWindow();
	void RebuildLogTextCache();
	void RegisterCommands();
	void RegisterSuggestion(const char* Badge, const char* DisplayText, const char* InsertText, const char* Hint = "", bool bIsCVar = false);
	void UpdateSuggestions(const char* Query);
	void RenderAutocompletePopup(const ImVec2& InputMin, const ImVec2& InputMax);
	void AddToHistory(const FString& CommandLine);
	bool PassVerbosity(ELogVerbosity Verbosity) const;
	void SaveFilteredLog();

	char                  InputBuf[256];
	TArray<FLogData>       Items;
	TArray<FConsoleCommand> ConsoleCommands;
	TArray<FString>        Commands;
	TArray<FConsoleSuggestion> AllSuggestions;
	TArray<int32>         FilteredSuggestionIndices;
	TArray<FString>       History;
	int                   HistoryPos;
	int32                 SelectedSuggestion = 0;
	bool                  bSuppressSuggestions = false;
	bool                  bScrollSuggestionToSelection = false;
	bool                  bSuggestionPopupHovered = false;
	bool                  bApplySuggestionCompletion = false;
	bool                  bReclaimInputFocusNextFrame = false;
	bool                  bMoveCursorToEndOnFocus = false;
	FString               PendingSuggestionText;
	FString               LastSuggestionQuery;
	FString               ActiveSuggestionInputId;
	FString               CurrentInputRenderId;
	ImGuiTextFilter       Filter;
	bool                  AutoScroll;
	bool                  ScrollToBottom;

	// Verbosity 필터
	bool bShowMessages = true;
	bool bShowWarnings = true;
	bool bShowErrors = true;
	bool bShowCommands = true;

	// 드로어 / 도킹 상태 (Content Drawer와 동일 방식)
	bool bDocked = false;
	bool bDrawerOpen = false;
	bool bJustOpened = false;
	bool bPendingDock = false;
	ImVec2 DrawerPos = ImVec2(0.0f, 0.0f);
	ImVec2 DrawerSize = ImVec2(0.0f, 0.0f);
	ImGuiID DrawerViewportId = 0;

	// 드래그 선택 / Ctrl+C / Ctrl+A 용 캐시 버퍼 (필터링된 로그 본문)
	FString CachedLogText;
	bool bLogTextDirty = true;
	bool bSelectAllLogNextFrame = false;

	// 로그 본문용 고정폭 글꼴 (Consolas + 한글 나눔스퀘어). 없으면 기본 글꼴 사용
	ImFont* LogFont = nullptr;

	// Inherited via ILogSink
	void OnLog(ELogVerbosity, const FString& Message) override;

	FEditorSettings* EditorSettings = nullptr;
};
