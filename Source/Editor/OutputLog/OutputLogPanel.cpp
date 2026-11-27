#include "EnginePCH.h"
#include "Editor/OutputLog/OutputLogPanel.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/EditorUI/EditorIcons.h"

#include "Core/StatOverlay.h"
#include "Core/Misc/Paths.h"
#include "Render/SceneRenderer.h"
#include "Render/ShowFlags.h"

#include "Editor/Settings/SettingsPanel.h"

#include "Components/SkinnedMeshComponent.h"

#include "Launch/Windows/WindowsPlatformCrashContext.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
	FString TrimLeftCopy(const char* Text)
	{
		if (!Text)
			return {};
		while (*Text && std::isspace(static_cast<unsigned char>(*Text)))
			++Text;
		return FString(Text);
	}

	bool StartsWithIgnoreCase(const FString& Text, const FString& Prefix)
	{
		if (Prefix.size() > Text.size())
			return false;
		for (size_t i = 0; i < Prefix.size(); ++i)
		{
			if (std::tolower(static_cast<unsigned char>(Text[i])) != std::tolower(static_cast<unsigned char>(Prefix[i])))
				return false;
		}
		return true;
	}

	bool ContainsIgnoreCase(const FString& Text, const FString& Needle)
	{
		if (Needle.empty())
			return true;
		if (Needle.size() > Text.size())
			return false;

		for (size_t i = 0; i <= Text.size() - Needle.size(); ++i)
		{
			bool bMatch = true;
			for (size_t j = 0; j < Needle.size(); ++j)
			{
				if (std::tolower(static_cast<unsigned char>(Text[i + j])) != std::tolower(static_cast<unsigned char>(Needle[j])))
				{
					bMatch = false;
					break;
				}
			}
			if (bMatch)
				return true;
		}
		return false;
	}

	TArray<FString> TokenizeCommand(const FString& Line)
	{
		TArray<FString> Tokens;
		std::istringstream Stream(Line);
		for (FString Token; Stream >> Token;)
			Tokens.Add(Token);
		return Tokens;
	}

	template <typename T>
	bool ParseNumber(const FString& Text, T& Out)
	{
		const auto [End, Error] = std::from_chars(Text.data(), Text.data() + Text.size(), Out);
		return Error == std::errc{} && End == Text.data() + Text.size();
	}

	bool ParseToggle(const TArray<FString>& Args, bool bCurrent, bool& bOut)
	{
		if (Args.Num() == 1) { bOut = !bCurrent; return true; }
		if (Args.Num() == 2 && (Args[1] == "0" || Args[1] == "1")) { bOut = (Args[1] == "1"); return true; }
		return false;
	}
}

static int TextEditCallbackStub(ImGuiInputTextCallbackData* data)
{
	FOutputLogPanel* OutputLog = (FOutputLogPanel*)data->UserData;
	return OutputLog->TextEditCallback(data);
}

static int Strnicmp(const char* s1, const char* s2, int n)
{
	int d = 0;
	while (n > 0 && (d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; n--; }
	return d;
}

static bool EqualsIgnoreCase(const FString& A, const char* B)
{
	return Strnicmp(A.c_str(), B, static_cast<int>(A.size()) + 1) == 0;
}

bool FOutputLogPanel::Init()
{
	ClearLog();
	RegisterCommands();

	HistoryPos = -1;
	InputBuf[0] = '\0';

	AutoScroll = true;
	ScrollToBottom = false;

	return true;
}

void FOutputLogPanel::LoadLogFont()
{
	if (LogFont || !ImGui::GetCurrentContext())
		return;

	ImGuiIO& IO = ImGui::GetIO();
	wchar_t WindowsDir[MAX_PATH]{};
	if (::GetWindowsDirectoryW(WindowsDir, MAX_PATH) > 0)
	{
		const std::filesystem::path MonoPath = std::filesystem::path(WindowsDir) / L"Fonts" / L"consola.ttf";
		std::error_code Error;
		if (std::filesystem::exists(MonoPath, Error))
		{
			LogFont = IO.Fonts->AddFontFromFileTTF(FPaths::ToUtf8(MonoPath).c_str(), 14.0f);
			if (LogFont)
			{
				const std::filesystem::path KoreanFont = FPaths::EngineFile("EngineResources/Fonts/NanumSquareR.ttf");
				if (std::filesystem::exists(KoreanFont, Error))
				{
					ImFontConfig Merge;
					Merge.MergeMode = true;
					IO.Fonts->AddFontFromFileTTF(FPaths::ToUtf8(KoreanFont).c_str(), 14.0f,
						&Merge, IO.Fonts->GetGlyphRangesKorean());
				}
			}
		}
	}
}

void FOutputLogPanel::Tick(float DeltaTime)
{
}

void FOutputLogPanel::ToggleDrawer()
{
	if (bDocked)
	{
		ImGui::SetWindowFocus("Output Log");
		return;
	}
	bDrawerOpen = !bDrawerOpen;
	bJustOpened = bDrawerOpen;
	if (bDrawerOpen)
		ScrollToBottom = true;
}

void FOutputLogPanel::OpenDrawer()
{
	SetOpen(true);
	if (bDocked)
	{
		ImGui::SetWindowFocus("Output Log");
		return;
	}
	if (!bDrawerOpen)
	{
		bDrawerOpen = true;
		bJustOpened = true;
		ScrollToBottom = true;
	}
}

void FOutputLogPanel::SetDrawerRect(const ImVec2& Pos, const ImVec2& Size, ImGuiID ViewportId)
{
	DrawerPos = Pos;
	DrawerSize = Size;
	DrawerViewportId = ViewportId;
}

bool FOutputLogPanel::BeginWindow()
{
	if (bDocked)
	{
		if (bPendingDock)
		{
			if (ImGuiWindow* ContentWindow = ImGui::FindWindowByName("Content Drawer"); ContentWindow && ContentWindow->DockId != 0)
				ImGui::SetNextWindowDockID(ContentWindow->DockId, ImGuiCond_Always);
			bPendingDock = false;
		}
		bool bStayDocked = true;
		EditorPanelUI::BeginPanel("Output Log", &bStayDocked);
		if (!bStayDocked)
		{
			bDocked = false;
			bDrawerOpen = false;
		}
		return true;
	}

	if (!bDrawerOpen || DrawerSize.x <= 0.0f || DrawerSize.y <= 0.0f)
		return false;

	ImGui::SetNextWindowPos(DrawerPos);
	ImGui::SetNextWindowSize(DrawerSize);
	if (DrawerViewportId != 0)
		ImGui::SetNextWindowViewport(DrawerViewportId);
	if (bJustOpened)
		ImGui::SetNextWindowFocus();

	const ImGuiWindowFlags Flags = ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::Begin("Output Log###OutputLogOverlay", nullptr, Flags);
	ImGui::PopStyleVar();

	if (!bJustOpened)
	{
		const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		const bool bPopupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
		if ((!bFocused && !bPopupOpen && !bSuggestionPopupHovered && !bReclaimInputFocusNextFrame)
			|| (bFocused && ImGui::IsKeyPressed(ImGuiKey_Escape)))
		{
			bDrawerOpen = false;
		}
	}
	bJustOpened = false;
	return true;
}

void FOutputLogPanel::RebuildLogTextCache()
{
	CachedLogText.clear();
	for (const FLogData& Item : Items)
	{
		if (!PassVerbosity(Item.Verbosity) || !Filter.PassFilter(Item.message.c_str()))
			continue;
		CachedLogText += Item.message;
		if (Item.message.empty() || Item.message.back() != '\n')
			CachedLogText += '\n';
	}
	bLogTextDirty = false;
}

void FOutputLogPanel::OnRender()
{
	if (!BeginWindow())
		return;

	RenderContents();
	ImGui::End();
}

void FOutputLogPanel::RenderContents()
{
	const ImGuiStyle& Style = ImGui::GetStyle();
	const float S = EditorIcons::GetScale();
	const float BtnH = ImGui::GetFrameHeight();
	const ImU32 IconCol = ImGui::GetColorU32(ImGuiCol_Text, 0.85f);
	bool CopyToClipboard = false;

	// 상단 바: [Search Log] [Filters v] ......... [Dock in Layout] [Settings v]
	const char* DockLabel = bDocked ? "Undock to Drawer" : "Dock in Layout";
	const float FiltersW = EditorIcons::CalcButtonWidth(true, "Filters", true, BtnH);
	const float DockW = EditorIcons::CalcButtonWidth(true, DockLabel, false, BtnH);
	const float SettingsW = EditorIcons::CalcButtonWidth(true, "Settings", true, BtnH);
	const float RightTotalW = DockW + SettingsW + Style.ItemSpacing.x;
	const float SearchRoom = ImGui::GetContentRegionAvail().x - FiltersW - RightTotalW - Style.ItemSpacing.x * 2.0f - 12.0f * S;
	const float SearchWidth = std::clamp(SearchRoom, 120.0f * S, 320.0f * S);

	if (EditorIcons::SearchBox("##SearchLog", Filter.InputBuf, IM_COUNTOF(Filter.InputBuf), "Search Log...", SearchWidth))
	{
		Filter.Build();
		bLogTextDirty = true;
	}
	if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
	{
		ImGui::SetTooltip(
			"Filter syntax:\n"
			"error        Include matching lines\n"
			"-warning     Exclude matching lines\n"
			"error,-warn  Combine filters");
	}

	ImGui::SameLine();
	if (EditorIcons::Button("##LogFilters", EEditorIcon::Filter, IconCol, "Filters", true,
		ImGui::IsPopupOpen("##LogFiltersPopup"), BtnH))
	{
		ImGui::OpenPopup("##LogFiltersPopup");
	}

	ImGui::SameLine();
	const float RightPad = std::max(0.0f, ImGui::GetContentRegionAvail().x - RightTotalW);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + RightPad);
	if (EditorIcons::Button("##LogDock", EEditorIcon::Dock, IconCol, DockLabel, false, false, BtnH))
	{
		bDocked = !bDocked;
		bPendingDock = bDocked;
		bDrawerOpen = !bDocked;
		bJustOpened = bDrawerOpen;
	}
	ImGui::SameLine();
	if (EditorIcons::Button("##LogSettings", EEditorIcon::Settings, IconCol, "Settings", true,
		ImGui::IsPopupOpen("##LogSettingsPopup"), BtnH))
	{
		ImGui::OpenPopup("##LogSettingsPopup");
	}

	if (ImGui::BeginPopup("##LogFiltersPopup"))
	{
		EditorIcons::MenuSection("Verbosity");
		if (ImGui::Checkbox("Messages", &bShowMessages)) bLogTextDirty = true;
		if (ImGui::Checkbox("Warnings", &bShowWarnings)) bLogTextDirty = true;
		if (ImGui::Checkbox("Errors", &bShowErrors)) bLogTextDirty = true;
		if (ImGui::Checkbox("Commands", &bShowCommands)) bLogTextDirty = true;
		ImGui::Separator();
		if (ImGui::MenuItem("Show All"))
		{
			bShowMessages = bShowWarnings = bShowErrors = bShowCommands = true;
			bLogTextDirty = true;
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopup("##LogSettingsPopup"))
	{
		ImGui::Checkbox("Auto-scroll", &AutoScroll);
		ImGui::Separator();
		if (ImGui::MenuItem("Copy All")) CopyToClipboard = true;
		if (ImGui::MenuItem("Clear Log")) ClearLog();
		if (ImGui::MenuItem("Save Filtered Log")) SaveFilteredLog();
		ImGui::SetItemTooltip("Save visible filtered output into Project/Logs.");
		ImGui::EndPopup();
	}

	ImGui::Separator();

	// 로그 본문 (읽기 전용 멀티라인 텍스트: 마우스 드래그 파란색 선택, Ctrl+C 복사, Ctrl+A 전체 선택 지원)
	if (bLogTextDirty)
		RebuildLogTextCache();

	const float FooterHeight = Style.SeparatorSize + Style.ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	static char EmptyLogBuf[1] = { '\0' };
	char* LogBuf = CachedLogText.empty() ? EmptyLogBuf : CachedLogText.data();
	const size_t LogBufSize = CachedLogText.empty() ? 1 : (CachedLogText.size() + 1);

	auto LogViewCallback = [](ImGuiInputTextCallbackData* Data) -> int
	{
		auto* Self = static_cast<FOutputLogPanel*>(Data->UserData);
		if (Self && Self->bSelectAllLogNextFrame)
		{
			Data->SelectAll();
			Self->bSelectAllLogNextFrame = false;
		}
		return 0;
	};

	ImGui::PushStyleColor(ImGuiCol_FrameBg, EditorColor::BgDeepest);
	ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, EditorColor::WithAlpha(EditorColor::Accent, 110));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * S, 4.0f * S));
	if (LogFont) ImGui::PushFont(LogFont, 0.0f);

	if (bSelectAllLogNextFrame)
		ImGui::SetKeyboardFocusHere();

	ImGui::InputTextMultiline(
		"##LogTextArea",
		LogBuf,
		LogBufSize,
		ImVec2(-FLT_MIN, -FooterHeight),
		ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CallbackAlways,
		LogViewCallback,
		this);

	// 우클릭 컨텍스트 메뉴
	if (ImGui::BeginPopupContextItem("##LogTextContext"))
	{
		if (ImGui::MenuItem("Select All", "Ctrl+A"))
			bSelectAllLogNextFrame = true;
		if (ImGui::MenuItem("Copy All"))
			CopyToClipboard = true;
		ImGui::Separator();
		if (ImGui::MenuItem("Clear Log"))
			ClearLog();
		if (ImGui::MenuItem("Save Filtered Log"))
			SaveFilteredLog();
		ImGui::EndPopup();
	}

	// 자동 스크롤 (InputTextMultiline 내부 자식 창을 찾아 맨 아래로 이동)
	if (ScrollToBottom || AutoScroll)
	{
		if (ImGuiWindow* CurWin = ImGui::GetCurrentWindow())
		{
			for (int i = CurWin->DC.ChildWindows.Size - 1; i >= 0; --i)
			{
				ImGuiWindow* Child = CurWin->DC.ChildWindows[i];
				if (Child && std::strstr(Child->Name, "##LogTextArea") != nullptr)
				{
					if (ScrollToBottom || (AutoScroll && Child->Scroll.y >= Child->ScrollMax.y - 4.0f))
						ImGui::SetScrollY(Child, Child->ScrollMax.y);
					break;
				}
			}
		}
		ScrollToBottom = false;
	}

	if (LogFont) ImGui::PopFont();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(2);

	if (CopyToClipboard)
		ImGui::SetClipboardText(CachedLogText.c_str());

	ImGui::Separator();

	// 하단 명령 입력줄
	if (EditorIcons::Button("##LogCmdBtn", EEditorIcon::OutputLog, IconCol, "Cmd", true,
		ImGui::IsPopupOpen("##LogCmdPopup"), BtnH))
	{
		ImGui::OpenPopup("##LogCmdPopup");
	}
	if (ImGui::BeginPopup("##LogCmdPopup"))
	{
		ImGui::MenuItem("Console Command", nullptr, true);
		ImGui::Separator();
		if (ImGui::MenuItem("Show Commands (HELP)"))
			ExecCommand("HELP");
		if (ImGui::MenuItem("Clear Log"))
			ClearLog();
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	RenderCommandInput("##OutputLogInput", InputBuf, IM_COUNTOF(InputBuf), -1.0f, true, true);
}

void FOutputLogPanel::RenderCommandInput(const char* InputId, char* Buffer, size_t BufferSize, float Width, bool bUseLogFont, bool bReclaimFocusOnSubmit)
{
	if (!InputId || !Buffer || BufferSize == 0)
		return;

	CurrentInputRenderId = InputId;
	const bool bMatchesActiveInput = (ActiveSuggestionInputId == InputId);

	if (bApplySuggestionCompletion && bMatchesActiveInput)
	{
		std::snprintf(Buffer, BufferSize, "%s", PendingSuggestionText.c_str());
		LastSuggestionQuery = PendingSuggestionText;
		bApplySuggestionCompletion = false;
	}
	if (bReclaimInputFocusNextFrame && bMatchesActiveInput)
	{
		ImGui::SetKeyboardFocusHere();
		bReclaimInputFocusNextFrame = false;
	}

	bool reclaim_focus = false;
	const ImGuiInputTextFlags input_text_flags =
		ImGuiInputTextFlags_EnterReturnsTrue |
		ImGuiInputTextFlags_EscapeClearsAll |
		ImGuiInputTextFlags_CallbackCompletion |
		ImGuiInputTextFlags_CallbackHistory |
		ImGuiInputTextFlags_CallbackEdit |
		ImGuiInputTextFlags_CallbackAlways;

	ImGui::SetNextItemWidth(Width);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, EditorColor::BgDeepest);
	if (bUseLogFont && LogFont) ImGui::PushFont(LogFont, 0.0f);
	if (ImGui::InputTextWithHint(InputId, "Enter Console Command... (Tab to complete)", Buffer, BufferSize, input_text_flags, &TextEditCallbackStub, this))
	{
		if (Buffer[0] != '\0')
		{
			ExecCommand(Buffer);
		}
		Buffer[0] = '\0';
		HistoryPos = -1;
		FilteredSuggestionIndices.Reset();
		SelectedSuggestion = 0;
		bSuppressSuggestions = false;
		LastSuggestionQuery.clear();
		reclaim_focus = bReclaimFocusOnSubmit;
	}
	if (bUseLogFont && LogFont) ImGui::PopFont();
	ImGui::PopStyleColor();

	const ImVec2 InputMin = ImGui::GetItemRectMin();
	const ImVec2 InputMax = ImGui::GetItemRectMax();
	const bool bInputActive = ImGui::IsItemActive();

	if (bInputActive)
	{
		ActiveSuggestionInputId = InputId;
	}

	if ((bInputActive || bMatchesActiveInput) && Buffer[0] == '\0')
	{
		FilteredSuggestionIndices.Reset();
		SelectedSuggestion = 0;
		bSuppressSuggestions = false;
		LastSuggestionQuery.clear();
	}

	if (bReclaimFocusOnSubmit)
	{
		ImGui::SetItemDefaultFocus();
	}
	if (reclaim_focus)
		ImGui::SetKeyboardFocusHere(-1);

	const bool bPopupTarget = (ActiveSuggestionInputId == InputId);
	const bool bShowSuggestions =
		(bInputActive || (bSuggestionPopupHovered && bPopupTarget)) &&
		!bSuppressSuggestions &&
		!FilteredSuggestionIndices.IsEmpty();
	if (bPopupTarget)
	{
		bSuggestionPopupHovered = false;
	}
	if (bShowSuggestions)
	{
		RenderAutocompletePopup(InputMin, InputMax);
	}
	CurrentInputRenderId.clear();
}

bool FOutputLogPanel::PassVerbosity(ELogVerbosity Verbosity) const
{
	switch (Verbosity)
	{
	case ELogVerbosity::Input:   return bShowCommands;
	case ELogVerbosity::Warning: return bShowWarnings;
	case ELogVerbosity::Error:   return bShowErrors;
	default:                     return bShowMessages;
	}
}

void FOutputLogPanel::SaveFilteredLog()
{
	const auto Directory = FPaths::ProjectDir() / "Logs";
	std::error_code Error;
	std::filesystem::create_directories(Directory, Error);
	const auto Stamp = std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	const auto Path = Directory / ("Output-" + std::to_string(Stamp) + ".log");
	std::ofstream File(Path, std::ios::out);
	for (const auto& Item : Items)
	{
		if (PassVerbosity(Item.Verbosity) && Filter.PassFilter(Item.message.c_str()))
			File << Item.message << '\n';
	}
	File.flush();
	const bool bSaved = !Error && File.good();
	File.close();
	if (bSaved) HTR_LOG(Info, "Output saved to {}", FPaths::ToUtf8(Path));
	else        HTR_LOG(Error, "Could not save output to {}", FPaths::ToUtf8(Path));
}

void FOutputLogPanel::ClearLog()
{
	Items.Reset();
	CachedLogText.clear();
	bLogTextDirty = true;
}

void FOutputLogPanel::ExecCommand(const FString& CommandLine)
{
	FLog::Log(ELogVerbosity::Input, "# {}\n", CommandLine);
	AddToHistory(CommandLine);
	ScrollToBottom = true;

	const TArray<FString> Args = TokenizeCommand(CommandLine);
	if (Args.Num() == 0) return;

	for (const FConsoleCommand& Cmd : ConsoleCommands)
	{
		if (!EqualsIgnoreCase(Args[0], Cmd.Name.c_str())) continue;
		if (!Cmd.Handler(Args, CommandLine))
			FLog::Log(ELogVerbosity::Info, "Usage: {}", Cmd.Usage);
		return;
	}
	FLog::Log(ELogVerbosity::Info, "Unknown command: '{}'", CommandLine);
}

int FOutputLogPanel::TextEditCallback(ImGuiInputTextCallbackData* data)
{
	switch (data->EventFlag)
	{
	case ImGuiInputTextFlags_CallbackAlways:
	{
		if (bMoveCursorToEndOnFocus)
		{
			data->CursorPos = data->BufTextLen;
			data->SelectionStart = data->BufTextLen;
			data->SelectionEnd = data->BufTextLen;
			bMoveCursorToEndOnFocus = false;
		}
		if (LastSuggestionQuery != data->Buf || ActiveSuggestionInputId != CurrentInputRenderId)
		{
			ActiveSuggestionInputId = CurrentInputRenderId;
			bSuppressSuggestions = false;
			HistoryPos = -1;
			UpdateSuggestions(data->Buf);
		}
		break;
	}
	case ImGuiInputTextFlags_CallbackEdit:
	{
		ActiveSuggestionInputId = CurrentInputRenderId;
		bSuppressSuggestions = false;
		HistoryPos = -1;
		UpdateSuggestions(data->Buf);
		break;
	}
	case ImGuiInputTextFlags_CallbackCompletion:
	{
		if (!bSuppressSuggestions && !FilteredSuggestionIndices.IsEmpty())
		{
			const int32 ClampedIndex = std::clamp(SelectedSuggestion, 0, FilteredSuggestionIndices.Num() - 1);
			const FConsoleSuggestion& Choice = AllSuggestions[FilteredSuggestionIndices[ClampedIndex]];
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, Choice.InsertText.c_str());
			LastSuggestionQuery = Choice.InsertText;
			bSuppressSuggestions = true;
			break;
		}

		const char* word_end = data->Buf + data->CursorPos;
		const char* word_start = word_end;
		while (word_start > data->Buf)
		{
			const char c = word_start[-1];
			if (c == ' ' || c == '\t' || c == ',' || c == ';')
				break;
			word_start--;
		}

		ImVector<FString> candidates;
		for (int i = 0; i < Commands.Num(); i++)
			if (Strnicmp(Commands[i].c_str(), word_start, (int)(word_end - word_start)) == 0)
				candidates.push_back(Commands[i]);

		if (candidates.Size == 1)
		{
			data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
			data->InsertChars(data->CursorPos, candidates[0].c_str());
			data->InsertChars(data->CursorPos, " ");
		}
		break;
	}
	case ImGuiInputTextFlags_CallbackHistory:
	{
		if (!bSuppressSuggestions && !FilteredSuggestionIndices.IsEmpty())
		{
			const int32 Count = FilteredSuggestionIndices.Num();
			if (data->EventKey == ImGuiKey_DownArrow)
				SelectedSuggestion = (SelectedSuggestion + 1) % Count;
			else if (data->EventKey == ImGuiKey_UpArrow)
				SelectedSuggestion = (SelectedSuggestion - 1 + Count) % Count;
			bScrollSuggestionToSelection = true;
			break;
		}

		const int prev_history_pos = HistoryPos;
		if (data->EventKey == ImGuiKey_UpArrow)
		{
			if (HistoryPos == -1)
				HistoryPos = History.Num() - 1;
			else if (HistoryPos > 0)
				HistoryPos--;
		}
		else if (data->EventKey == ImGuiKey_DownArrow)
		{
			if (HistoryPos != -1)
				if (++HistoryPos >= History.Num())
					HistoryPos = -1;
		}

		if (prev_history_pos != HistoryPos)
		{
			const char* history_str = (HistoryPos >= 0) ? History[HistoryPos].c_str() : "";
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, history_str);
			LastSuggestionQuery = history_str;
			bSuppressSuggestions = true;
		}
		break;
	}
	}
	return 0;
}

void FOutputLogPanel::RegisterSuggestion(const char* Badge, const char* DisplayText, const char* InsertText, const char* Hint, bool bIsCVar)
{
	FConsoleSuggestion Item;
	Item.Badge = Badge ? Badge : "";
	Item.DisplayText = DisplayText ? DisplayText : "";
	Item.InsertText = InsertText ? InsertText : "";
	Item.Hint = Hint ? Hint : "";
	Item.bIsCVar = bIsCVar;
	AllSuggestions.Add(std::move(Item));
}

void FOutputLogPanel::UpdateSuggestions(const char* Query)
{
	LastSuggestionQuery = Query ? Query : "";
	const FString Trimmed = TrimLeftCopy(Query);
	FilteredSuggestionIndices.Reset();
	SelectedSuggestion = 0;

	if (Trimmed.empty())
		return;

	TArray<int32> PrefixMatches;
	TArray<int32> SubstringMatches;
	const TArray<FString> QueryTokens = TokenizeCommand(Trimmed);

	for (int32 i = 0; i < AllSuggestions.Num(); ++i)
	{
		const FConsoleSuggestion& Item = AllSuggestions[i];
		if (StartsWithIgnoreCase(Item.InsertText, Trimmed) || StartsWithIgnoreCase(Item.DisplayText, Trimmed))
		{
			PrefixMatches.Add(i);
			continue;
		}

		if (ContainsIgnoreCase(Item.InsertText, Trimmed) || ContainsIgnoreCase(Item.DisplayText, Trimmed))
		{
			SubstringMatches.Add(i);
			continue;
		}

		if (QueryTokens.Num() > 1)
		{
			bool bAllTokensMatch = true;
			for (const FString& Token : QueryTokens)
			{
				if (!ContainsIgnoreCase(Item.InsertText, Token) && !ContainsIgnoreCase(Item.Badge, Token))
				{
					bAllTokensMatch = false;
					break;
				}
			}
			if (bAllTokensMatch)
				SubstringMatches.Add(i);
		}
	}

	for (int32 Index : PrefixMatches)
		FilteredSuggestionIndices.Add(Index);
	for (int32 Index : SubstringMatches)
		FilteredSuggestionIndices.Add(Index);

	bScrollSuggestionToSelection = !FilteredSuggestionIndices.IsEmpty();
}

void FOutputLogPanel::RenderAutocompletePopup(const ImVec2& InputMin, const ImVec2& InputMax)
{
	if (FilteredSuggestionIndices.IsEmpty())
		return;

	SelectedSuggestion = std::clamp(SelectedSuggestion, 0, FilteredSuggestionIndices.Num() - 1);

	const float S = EditorIcons::GetScale();
	const float RowHeight = 18.0f * S;
	const float BadgeWidth = 42.0f * S;
	const float PopupBorderPad = 2.0f * S;
	const int32 MaxVisibleRows = 14;
	const int32 VisibleRows = std::min(FilteredSuggestionIndices.Num(), MaxVisibleRows);
	const float PopupHeight = VisibleRows * RowHeight + PopupBorderPad * 2.0f;
	const float PopupWidth = std::max(380.0f * S, InputMax.x - InputMin.x);

	ImGuiViewport* HostViewport = ImGui::GetWindowViewport();
	ImVec2 PopupPos(InputMin.x, InputMin.y - PopupHeight - 2.0f * S);
	if (HostViewport)
	{
		const ImVec2 WorkMin = HostViewport->WorkPos;
		const ImVec2 WorkMax(HostViewport->WorkPos.x + HostViewport->WorkSize.x,
			HostViewport->WorkPos.y + HostViewport->WorkSize.y);
		if (PopupPos.y < WorkMin.y)
			PopupPos.y = std::min(InputMax.y + 2.0f * S, WorkMax.y - PopupHeight);
		if (PopupPos.x + PopupWidth > WorkMax.x)
			PopupPos.x = std::max(WorkMin.x, WorkMax.x - PopupWidth);
		ImGui::SetNextWindowViewport(HostViewport->ID);
	}

	ImGui::SetNextWindowPos(PopupPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(PopupWidth, PopupHeight), ImGuiCond_Always);

	const ImGuiWindowFlags PopupFlags =
		ImGuiWindowFlags_Tooltip |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoDocking;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, PopupBorderPad));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, EditorColor::WithAlpha(EditorColor::BgPopup, 248));
	ImGui::PushStyleColor(ImGuiCol_Border, EditorColor::BorderStrong);
	if (LogFont) ImGui::PushFont(LogFont, 0.0f);

	if (ImGui::Begin("##OutputLogAutocompletePopup", nullptr, PopupFlags))
	{
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
		bSuggestionPopupHovered = ImGui::IsWindowHovered(
			ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
			ImGuiHoveredFlags_ChildWindows);

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const float ContentWidth = ImGui::GetContentRegionAvail().x;
		const float FontHeight = ImGui::GetFontSize();

		for (int32 Row = 0; Row < FilteredSuggestionIndices.Num(); ++Row)
		{
			const FConsoleSuggestion& Item = AllSuggestions[FilteredSuggestionIndices[Row]];
			const bool bSelected = (Row == SelectedSuggestion);

			ImGui::PushID(Row);
			const ImVec2 RowMin = ImGui::GetCursorScreenPos();
			const ImVec2 RowMax(RowMin.x + ContentWidth, RowMin.y + RowHeight);

			const bool bClicked = ImGui::InvisibleButton("##SuggestionRow", ImVec2(ContentWidth, RowHeight),
				ImGuiButtonFlags_PressedOnClick | ImGuiButtonFlags_NoHoldingActiveId);
			const bool bHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
			if (bHovered)
			{
				bSuggestionPopupHovered = true;
				const ImVec2 MouseDelta = ImGui::GetIO().MouseDelta;
				if (MouseDelta.x != 0.0f || MouseDelta.y != 0.0f)
					SelectedSuggestion = Row;
			}
			if (bClicked)
			{
				PendingSuggestionText = Item.InsertText;
				bApplySuggestionCompletion = true;
				bReclaimInputFocusNextFrame = true;
				bMoveCursorToEndOnFocus = true;
				bSuppressSuggestions = true;
			}

			if (bSelected && bScrollSuggestionToSelection)
			{
				ImGui::SetScrollHereY(0.5f);
				bScrollSuggestionToSelection = false;
			}

			ImU32 BadgeColor = IM_COL32(108, 186, 128, 255);
			if (Item.Badge == "Stat")      BadgeColor = IM_COL32(142, 204, 62, 255);
			else if (Item.Badge == "Show") BadgeColor = IM_COL32(176, 132, 224, 255);
			else if (Item.Badge == "CSM")  BadgeColor = IM_COL32(214, 162, 68, 255);
			else if (Item.bIsCVar)         BadgeColor = IM_COL32(92, 166, 214, 255);

			if (bSelected)
				DrawList->AddRectFilled(ImVec2(RowMin.x + BadgeWidth, RowMin.y), RowMax, EditorColor::Accent);
			else if (bHovered)
				DrawList->AddRectFilled(ImVec2(RowMin.x + BadgeWidth, RowMin.y), RowMax, EditorColor::BgAccentHover);

			DrawList->AddRectFilled(RowMin, ImVec2(RowMin.x + BadgeWidth, RowMax.y), BadgeColor);

			const ImVec2 BadgeTextSize = ImGui::CalcTextSize(Item.Badge.c_str());
			const ImVec2 BadgeTextPos(
				RowMin.x + std::max(3.0f * S, (BadgeWidth - BadgeTextSize.x) * 0.5f),
				RowMin.y + (RowHeight - FontHeight) * 0.5f);
			DrawList->AddText(BadgeTextPos, IM_COL32(18, 22, 16, 255), Item.Badge.c_str());

			const float LabelX = RowMin.x + BadgeWidth + 6.0f * S;
			const float TextY = RowMin.y + (RowHeight - FontHeight) * 0.5f;
			DrawList->AddText(ImVec2(LabelX, TextY), EditorColor::TextPrimary, Item.DisplayText.c_str());

			if (!Item.Hint.empty())
			{
				const ImVec2 LabelSize = ImGui::CalcTextSize(Item.DisplayText.c_str());
				const ImVec2 HintSize = ImGui::CalcTextSize(Item.Hint.c_str());
				const float HintRightX = RowMax.x - 6.0f * S - HintSize.x;
				if (HintRightX > LabelX + LabelSize.x + 10.0f * S)
					DrawList->AddText(ImVec2(HintRightX, TextY), EditorColor::TextMuted, Item.Hint.c_str());
			}

			ImGui::PopID();
		}
	}
	ImGui::End();

	if (LogFont) ImGui::PopFont();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(5);
}

void FOutputLogPanel::RegisterCommands()
{
	ConsoleCommands.Empty();
	Commands.Empty();
	AllSuggestions.Empty();

	auto Add = [this](const char* Name, const char* Usage, FConsoleHandler Handler)
		{
			ConsoleCommands.Add({ Name, Usage, std::move(Handler) });
			Commands.Add(Name);
		};

	Add("HELP", "HELP", [this](const TArray<FString>&, const FString&)
		{
			FLog::Log(ELogVerbosity::Info, "Commands:");
			for (const FConsoleCommand& Cmd : ConsoleCommands)
				FLog::Log(ELogVerbosity::Info, "- {}", Cmd.Usage);
			return true;
		});

	Add("CLEAR", "CLEAR", [this](const TArray<FString>&, const FString&)
		{
			ClearLog();
			return true;
		});

	Add("HISTORY", "HISTORY", [this](const TArray<FString>&, const FString&)
		{
			const int32 First = std::max(History.Num() - 10, 0);
			for (int32 i = First; i < History.Num(); ++i)
				FLog::Log(ELogVerbosity::Info, "{:03d}: {}", i, History[i]);
			return true;
		});

	Add("STAT", "STAT <fps|memory|profile|light|decal|physics|all|none>", [](const TArray<FString>&, const FString& Line)
		{
			FString Message;
			FStatOverlay::ExecCommand(Line, Message);
			FLog::Log(ELogVerbosity::Info, "{}", Message);
			return true;
		});

	Add("SHOW", "SHOW <FlagName> [0|1]", [this](const TArray<FString>& Args, const FString&)
		{
			if (!EditorSettings)
			{
				FLog::Log(ELogVerbosity::Info, "Settings not available");
				return true;
			}
			if (Args.Num() == 1)
			{
				FLog::Log(ELogVerbosity::Info, "ShowFlags:");
				for (const FShowFlagInfo& Info : FEngineShowFlags::GetAll())
					FLog::Log(ELogVerbosity::Info, "- {} = {}", Info.Name, EditorSettings->ShowFlags.Get(Info) ? 1 : 0);
				return true;
			}
			const FShowFlagInfo* Info = FEngineShowFlags::Find(Args[1]);
			if (!Info) return false;
			bool bNext = !EditorSettings->ShowFlags.Get(*Info);
			if (Args.Num() == 3)
			{
				if (Args[2] != "0" && Args[2] != "1") return false;
				bNext = (Args[2] == "1");
			}
			else if (Args.Num() != 2) return false;
			EditorSettings->ShowFlags.Set(*Info, bNext);
			FLog::Log(ELogVerbosity::Info, "ShowFlag.{} = {}", Info->Name, bNext ? 1 : 0);
			return true;
		});

	Add("CSM", "CSM Count <1-4> | CSM LogRatio <0.0-1.0>", [](const TArray<FString>& Args, const FString&)
		{
			if (Args.Num() != 3) return false;
			if (EqualsIgnoreCase(Args[1], "COUNT"))
			{
				uint32 Count = 0;
				if (!ParseNumber(Args[2], Count) || !FSceneRenderer::SetCascadeCount(Count)) return false;
				FLog::Log(ELogVerbosity::Info, "CSM Count: {}", Count);
				return true;
			}
			if (EqualsIgnoreCase(Args[1], "LOGRATIO"))
			{
				float Ratio = 0.0f;
				if (!ParseNumber(Args[2], Ratio) || !FSceneRenderer::SetCascadeLogRatio(Ratio)) return false;
				FLog::Log(ELogVerbosity::Info, "CSM LogRatio: {}", Ratio);
				return true;
			}
			return false;
		});

	Add("r.shadow.CacheWholeSceneShadows", "r.shadow.CacheWholeSceneShadows [0|1]",
		[this](const TArray<FString>& Args, const FString&)
		{
			if (!EditorSettings)
			{
				FLog::Log(ELogVerbosity::Info, "Settings not available");
				return true;
			}
			bool bOn = false;
			if (!ParseToggle(Args, EditorSettings->bCacheShadows, bOn)) return false;
			EditorSettings->bCacheShadows = bOn;
			FLog::Log(ELogVerbosity::Info, "r.shadow.CacheWholeSceneShadows = {}", bOn ? 1 : 0);
			return true;
		});

	Add("r.GPUSkinning", "r.GPUSkinning [0|1]", [](const TArray<FString>& Args, const FString&)
		{
			bool bOn = false;
			if (!ParseToggle(Args, USkinnedMeshComponent::bUseGPUSkinning, bOn)) return false;
			USkinnedMeshComponent::SetGPUSkinningEnabled(bOn);
			FLog::Log(ELogVerbosity::Info, "r.GPUSkinning = {}", bOn ? 1 : 0);
			return true;
		});

	Add("r.Skinning.ForceUpdate", "r.Skinning.ForceUpdate [0|1]", [](const TArray<FString>& Args, const FString&)
		{
			bool bOn = false;
			if (!ParseToggle(Args, USkinnedMeshComponent::bForceSkinUpdate, bOn)) return false;
			USkinnedMeshComponent::bForceSkinUpdate = bOn;
			FLog::Log(ELogVerbosity::Info, "r.Skinning.ForceUpdate = {}", bOn ? 1 : 0);
			return true;
		});

	Add("CauseCrash", "CauseCrash [zero|null|stack|crt|purecall|terminate|abort]", [](const TArray<FString>& Args, const FString&)
		{
			if (Args.Num() == 1)
			{
				WindowsPlatformCrashContext::CrashDefault();
				return true;
			}
			if (Args.Num() != 2) return false;

			const FString& Type = Args[1];
			if (EqualsIgnoreCase(Type, "zero"))           WindowsPlatformCrashContext::CrashDivideByZero();
			else if (EqualsIgnoreCase(Type, "null"))      WindowsPlatformCrashContext::CrashNullPointer();
			else if (EqualsIgnoreCase(Type, "stack"))     WindowsPlatformCrashContext::TriggerStackOverflow();
			else if (EqualsIgnoreCase(Type, "crt"))       WindowsPlatformCrashContext::CrashInvalidParameter();
			else if (EqualsIgnoreCase(Type, "purecall"))  WindowsPlatformCrashContext::CrashPureCall();
			else if (EqualsIgnoreCase(Type, "terminate")) WindowsPlatformCrashContext::CrashTerminate();
			else if (EqualsIgnoreCase(Type, "abort"))     WindowsPlatformCrashContext::CrashAbort();
			else return false;
			return true;
		});

	// 자동완성 후보 등록
	RegisterSuggestion("Cmd", "HELP", "HELP", "List all console commands");
	RegisterSuggestion("Cmd", "CLEAR", "CLEAR", "Clear output log");
	RegisterSuggestion("Cmd", "HISTORY", "HISTORY", "Show recent commands");
	RegisterSuggestion("Cmd", "CauseCrash", "CauseCrash ", "[zero|null|stack|crt|purecall|terminate|abort]");

	RegisterSuggestion("Stat", "stat fps", "stat fps", "Toggle FPS overlay");
	RegisterSuggestion("Stat", "stat memory", "stat memory", "Toggle memory overlay");
	RegisterSuggestion("Stat", "stat profile", "stat profile", "Toggle CPU/GPU profiler");
	RegisterSuggestion("Stat", "stat light", "stat light", "Toggle light stats");
	RegisterSuggestion("Stat", "stat decal", "stat decal", "Toggle decal stats");
	RegisterSuggestion("Stat", "stat physics", "stat physics", "Toggle physics stats");
	RegisterSuggestion("Stat", "stat all", "stat all", "Enable all stats");
	RegisterSuggestion("Stat", "stat none", "stat none", "Disable all stats");

	RegisterSuggestion("CSM", "CSM Count", "CSM Count ", "Set cascade count (1-4)");
	RegisterSuggestion("CSM", "CSM LogRatio", "CSM LogRatio ", "Set cascade split ratio (0.0-1.0)");

	RegisterSuggestion("CVar", "r.shadow.CacheWholeSceneShadows", "r.shadow.CacheWholeSceneShadows ", "[0|1]", true);
	RegisterSuggestion("CVar", "r.GPUSkinning", "r.GPUSkinning ", "[0|1]", true);
	RegisterSuggestion("CVar", "r.Skinning.ForceUpdate", "r.Skinning.ForceUpdate ", "[0|1]", true);

	for (const FShowFlagInfo& Info : FEngineShowFlags::GetAll())
	{
		const FString Display = FString("show ") + Info.Name;
		const FString Insert = Display + " ";
		RegisterSuggestion("Show", Display.c_str(), Insert.c_str(), Info.DisplayName);
	}
}

void FOutputLogPanel::AddToHistory(const FString& CommandLine)
{
	HistoryPos = -1;
	for (int32 i = History.Num() - 1; i >= 0; --i)
	{
		if (History[i] == CommandLine)
		{
			History.RemoveAt(i, 1);
			break;
		}
	}
	History.Add(CommandLine);
}

void FOutputLogPanel::OnLog(ELogVerbosity Verbosity, const FString& Message)
{
	FLogData LogData;
	LogData.Verbosity = Verbosity;

	switch (Verbosity)
	{
	case ELogVerbosity::Input:
		LogData.Color = ImVec4(1.0f, 0.8f, 0.6f, 1.0f);
		break;
	case ELogVerbosity::Info:
		LogData.Color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		break;
	case ELogVerbosity::Warning:
		LogData.Color = ImVec4(1.0f, 0.75f, 0.2f, 1.0f);
		break;
	case ELogVerbosity::Error:
		LogData.Color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
		break;
	}
	LogData.message = Message;
	Items.Add(LogData);
	bLogTextDirty = true;
	ScrollToBottom = true;
}
