#pragma once

#include <filesystem>

#include "Core/EngineString.h"

// C++ 프로젝트 지원. UE처럼 프로젝트가 Source/<모듈>에 자기 C++ 코드를 갖고,
// 엔진 폴더의 Setup/ProjectSolution.lua로 프로젝트 전용 솔루션(<Name>.slnx)을 만든다.
// 그 솔루션은 엔진과 모듈을 한 exe로 링크한 <Name>Editor.exe와 <Name>.exe를 빌드한다.
namespace ProjectCode
{
	// 모듈 이름 규칙: C++ 식별자 (영문자로 시작, 영문자·숫자·_). 문제가 없으면 빈 문자열.
	FString ValidateModuleName(const FString& Name);

	// Source/<Module>에 샘플 GameMode(A<Module>GameMode)를 만든다.
	bool WriteModuleTemplate(const std::filesystem::path& ProjectDir, const FString& ModuleName, FString& OutError);

	// 샘플 GameMode 클래스 이름 (A<Module>GameMode)
	FString SampleGameModeClass(const FString& ModuleName);

	// 프로젝트 솔루션 파일 경로 (<프로젝트 폴더>/<Name>.slnx)
	std::filesystem::path SolutionFile(const std::filesystem::path& ProjectFile);

	// Setup/GenerateProjectFiles.bat을 돌려 솔루션을 만든다. 끝날 때까지 기다린다.
	// OutLog에 premake 출력을 담는다.
	bool GenerateProjectFiles(const std::filesystem::path& ProjectFile, FString& OutLog, FString& OutError);
}
