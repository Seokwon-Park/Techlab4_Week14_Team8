#pragma once

#include <filesystem>
#include <unordered_map>
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "imgui.h"
#include "Core/EngineString.h"
#include "Containers/Array.h"
#include "Editor/ProjectBrowser/ProjectBrowser.h"

// UE 런처의 홈 화면처럼 생긴 프로젝트 브라우저 화면. 창은 만들지 않고 지금 ImGui 창 안을 채운다.
//   - 왼쪽: 로고, [New Project] [My Projects], Home / Open Project...
//   - 오른쪽: Home(최근 프로젝트 타일), My Projects(전체 목록), New Project(템플릿, 이름, 위치)
// 엔진을 띄우기 전의 프로젝트 브라우저 창(ProjectBrowser::Run)과 에디터의 Home 탭이 함께 쓴다.
class FProjectBrowserView
{
public:
	enum class EPage { Home, MyProjects, NewProject };

	// 최근 프로젝트를 다시 읽고 새 프로젝트 기본 이름·위치를 채운다. 썸네일도 다시 읽는다.
	void Init();
	// 프로젝트 썸네일(ProjectThumbnail::Find)을 텍스처로 만들 장치. 브라우저 창은 자기 장치, 에디터는 엔진 장치를 넘긴다.
	// 넘기지 않으면 썸네일 대신 색과 첫 글자를 그린다.
	void SetDevice(ID3D11Device* InDevice) { Device = InDevice; }
	// Owner: 파일 대화상자의 부모 창. Logo: 왼쪽 위 로고 (없으면 글자로 그린다).
	// CurrentProject: 에디터에서 지금 연 프로젝트. 그 타일에 "Current"를 쓴다 (브라우저 창에서는 비워 둔다).
	void Draw(HWND Owner, ImTextureID Logo, const std::filesystem::path& CurrentProject = {});

	// 열거나 새로 만든 프로젝트 파일. 한 번 꺼내면 비워진다.
	std::filesystem::path ConsumeChosen();
	void SetError(const FString& InError) { Error = InError; }
	void SetPage(EPage InPage) { Page = InPage; }

private:
	void DrawSidebar(HWND Owner, ImTextureID Logo, const std::filesystem::path& CurrentProject);
	void DrawHome(const std::filesystem::path& CurrentProject);
	void DrawMyProjects(HWND Owner, const std::filesystem::path& CurrentProject);
	void DrawNewProject(HWND Owner);
	void DrawError();

	// 최근 프로젝트 하나의 우클릭 메뉴 (Open / Show in Explorer / Remove from List)
	void DrawProjectContextMenu(int32 Index);
	void TryOpen(const std::filesystem::path& ProjectFile);
	void BrowseForProject(HWND Owner);
	void RemoveRecent(int32 Index);
	// 이 프로젝트 썸네일 텍스처. 없으면 ImTextureID_Invalid. 처음 물을 때 한 번 읽어 둔다.
	ImTextureID GetThumbnail(const std::filesystem::path& ProjectFile);
	void DrawProjectImage(ImDrawList* DL, ImVec2 Min, ImVec2 Max, const std::filesystem::path& ProjectFile);

	ID3D11Device* Device = nullptr;
	std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> Thumbnails;

	EPage Page = EPage::Home;
	TArray<std::filesystem::path> Recent;
	int32 SelectedRecent = -1;

	char NameBuffer[64] = {};
	char LocationBuffer[1024] = {};
	EProjectTemplate Template = EProjectTemplate::Blank;

	FString Error;
	std::filesystem::path Chosen;
};
