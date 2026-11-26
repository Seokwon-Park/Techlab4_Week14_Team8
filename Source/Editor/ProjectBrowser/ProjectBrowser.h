#pragma once

#include <filesystem>
#include <Windows.h>

#include "Core/EngineString.h"
#include "Containers/Array.h"

// 최근에 연 프로젝트 목록. 엔진 폴더의 Saved/RecentProjects.json에 저장한다.
namespace RecentProjects
{
	// 파일이 없어진 항목은 빼고 돌려준다. 최근에 연 것이 앞에 온다.
	TArray<std::filesystem::path> Load();
	// 목록 맨 앞에 넣고 저장한다.
	void Add(const std::filesystem::path& ProjectFile);
	void Remove(const std::filesystem::path& ProjectFile);
}

// 새 프로젝트 템플릿 (UE의 Blueprint / C++ 프로젝트 선택)
enum class EProjectTemplate
{
	Blank,   // 콘텐츠만 있는 프로젝트. 엔진 폴더의 HitoriEditor.exe로 연다.
	Cpp,     // Source/<Name> C++ 모듈과 샘플 GameMode. 프로젝트 솔루션에서 <Name>Editor.exe를 빌드해 연다.
};

// 새 프로젝트 폴더를 만든다.
// <ParentDir>/<Name>/ 아래에 Assets, Scenes, Scripts, Config와 <Name>.hproject를 만든다.
// C++ 템플릿은 Source/<Name>과 프로젝트 솔루션(<Name>.slnx)도 만든다.
// 엔진 코드가 직접 참조하는 아이콘, 폰트 등은 엔진 콘텐츠(EngineContent)에서 읽으므로 복사하지 않는다.
namespace ProjectCreator
{
	// 이름 규칙: 영문자로 시작하고 영문자, 숫자, _, -만 쓴다 (C++ 템플릿은 -도 안 된다). 문제가 없으면 빈 문자열.
	FString ValidateName(const FString& Name, EProjectTemplate Template = EProjectTemplate::Blank);
	bool Create(const FString& Name, const std::filesystem::path& ParentDir,
		std::filesystem::path& OutProjectFile, FString& OutError, EProjectTemplate Template = EProjectTemplate::Blank);
}

// UE의 프로젝트 브라우저에 해당하는 창. 엔진을 띄우기 전에 따로 연다.
// 최근 프로젝트를 열거나, .hproject 파일을 찾아 열거나, 경로를 정해 새 프로젝트를 만든다.
// 고른 .hproject 경로를 돌려준다. 창을 닫으면 빈 경로를 돌려준다.
namespace ProjectBrowser
{
	std::filesystem::path Run(HINSTANCE hInstance);
}
