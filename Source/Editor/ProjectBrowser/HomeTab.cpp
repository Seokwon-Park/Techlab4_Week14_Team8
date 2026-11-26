#include "EnginePCH.h"
#include "Editor/ProjectBrowser/HomeTab.h"

#include "Core/CoreGlobals.h"
#include "Engine/Engine.h"
#include "Launch/LaunchEngineLoop.h"
#include "Render/RenderDevice.h"
#include "Core/Misc/Paths.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Editor/EditorUI/EditorTabContainer.h"
#include "Editor/EditorUI/EditorTitleBar.h"

namespace fs = std::filesystem;

namespace
{
	bool SameFile(const fs::path& A, const fs::path& B)
	{
		std::error_code Ec;
		return fs::equivalent(A, B, Ec) || _wcsicmp(A.lexically_normal().c_str(), B.lexically_normal().c_str()) == 0;
	}

	// 지금 실행 중인 exe로 그 프로젝트를 연다 (UE도 프로젝트를 바꾸면 에디터를 새로 띄운다).
	bool LaunchEditor(const fs::path& ProjectFile, FString& OutError)
	{
		wchar_t Exe[MAX_PATH] = {};
		if (GetModuleFileNameW(nullptr, Exe, MAX_PATH) == 0)
		{
			OutError = "Cannot find the editor executable.";
			return false;
		}
		std::wstring CommandLine = L"\"" + std::wstring(Exe) + L"\" \"" + fs::path(ProjectFile).make_preferred().wstring() + L"\"";
		STARTUPINFOW Startup{ sizeof(Startup) };
		PROCESS_INFORMATION Process{};
		if (!CreateProcessW(nullptr, CommandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &Startup, &Process))
		{
			OutError = "Cannot start the editor (error " + std::to_string(GetLastError()) + ").";
			return false;
		}
		CloseHandle(Process.hThread);
		CloseHandle(Process.hProcess);
		return true;
	}
}

FHomeTab::FHomeTab(FEditorTabContainer& InContainer, IEditorTab* InMenuSource)
	: Container(InContainer)
	, MenuSource(InMenuSource)
{
	View.Init();
}

bool FHomeTab::GetTabIcon(EEditorIcon& OutIcon, ImU32& OutColor) const
{
	OutIcon = EEditorIcon::Home;
	OutColor = IM_COL32(225, 225, 228, 255);
	return true;
}

void FHomeTab::DrawMenuBar()
{
	if (MenuSource)
		MenuSource->DrawMenuBar();
}

void FHomeTab::OnRender(bool bActive)
{
	// 탭으로 돌아올 때마다 최근 목록과 썸네일을 새로 읽는다 (레벨을 저장해 썸네일이 새로 찍혔을 수 있다).
	if (bActive && !bWasActive)
	{
		// 썸네일 텍스처는 엔진 장치로 만든다.
		if (GEngine)
			if (FRenderDevice* RenderDevice = GEngine->GetEngineLoop().GetRenderDevice())
				View.SetDevice(RenderDevice->GetDevice());
		View.Init();
	}
	bWasActive = bActive;
	if (!bActive)
		return;
	View.Draw(GetActiveWindow(), EditorTitleBar::GetLogo(), FPaths::ProjectFilePath());
	if (const fs::path Chosen = View.ConsumeChosen(); !Chosen.empty())
		SwitchToProject(Chosen);
}

void FHomeTab::SwitchToProject(const fs::path& ProjectFile)
{
	if (SameFile(ProjectFile, FPaths::ProjectFilePath()))
	{
		View.SetError("This project is already open.");
		return;
	}

	const std::wstring Message = L"Open " + ProjectFile.stem().wstring() +
		L"?\n\nThe editor restarts with this project. Save the current level first if it has changes you want to keep.";
	if (MessageBoxW(GetActiveWindow(), Message.c_str(), L"Open Project", MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
		return;
	// 열린 에셋 편집기(BP, 파티클 등)마다 저장할지 묻는다. 하나라도 취소하면 그대로 둔다.
	if (!Container.ConfirmCloseAllTabs())
		return;

	FString Error;
	if (!LaunchEditor(ProjectFile, Error))
	{
		View.SetError(Error);
		return;
	}
	RecentProjects::Add(ProjectFile);
	RequestEngineExit("switching project");
}
