#pragma once

#include <filesystem>

#include "Core/EngineString.h"
#include "Core/Types.h"
#include "Containers/Array.h"

// 엔진 리소스 폴더 이름과 엔진 셰이더 키. 셰이더 키는 엔진 폴더 기준 상대 경로("EngineResources/Shaders/X.hlsl")이고
// 상수 테이블과 이름 비교에도 쓰이므로 문자열 리터럴 매크로로 둔다. 예: HITORI_ENGINE_SHADER("UberLit.hlsl")
#define HITORI_ENGINE_RESOURCES_FOLDER "EngineResources"
#define HITORI_ENGINE_SHADERS_FOLDER HITORI_ENGINE_RESOURCES_FOLDER "/Shaders"
#define HITORI_ENGINE_SHADER(File) HITORI_ENGINE_SHADERS_FOLDER "/" File

// UE의 FPaths처럼 엔진 폴더와 프로젝트 폴더를 나눠서 다룬다.
// 엔진 폴더: EngineResources(셰이더, 아이콘, 폰트, 스플래시)와 Build(셰이더 캐시 Build/Cache, 엔진 사용자 데이터 Build/Saved)가 있는 곳.
// 프로젝트 폴더: .hproject 파일이 있는 곳. Content(Scenes, Scripts 포함), Config, Saved(썸네일, 크래시 덤프), editor.ini가 여기에 있다.
//   기본 프로젝트는 <엔진>/HitoriProject에 있다.
// 프로젝트를 정하면 작업 디렉터리를 프로젝트 폴더로 바꾸므로 "Content/..." 같은 상대 경로를 그대로 쓴다.
// 엔진 파일은 EngineFile()로 절대 경로를 만들어 연다.
class FPaths
{
public:
	static constexpr const wchar_t* ProjectExtension = L".hproject";

	// 엔진 폴더를 찾는다. 차례로 본다:
	//   1. exe 위치에서 위로 올라가며 EngineResources/Shaders가 있는 폴더 (엔진 폴더 안의 exe, 패키지된 게임)
	//   2. 빌드할 때 정해진 엔진 폴더 (HITORI_ENGINE_DIR. 프로젝트 폴더에서 빌드한 <Name>Editor.exe 등)
	//   3. ProjectFileHint(또는 빌드 때 정해진 프로젝트 파일)의 EngineDir
	//   4. 작업 디렉터리
	static bool InitEngineDir(const std::filesystem::path& ProjectFileHint = {});

	// 빌드할 때 정해진 프로젝트 파일 (프로젝트 솔루션에서 빌드한 exe만). 없으면 빈 경로.
	static std::filesystem::path BuiltProjectFile();

	static const std::filesystem::path& EngineDir();
	static const std::filesystem::path& ProjectDir();
	static const std::filesystem::path& ProjectFilePath();
	static const FString& ProjectName();
	static bool HasProject();

	// .hproject 파일을 읽어 현재 프로젝트로 정하고 작업 디렉터리를 프로젝트 폴더로 바꾼다.
	static bool SetProject(const std::filesystem::path& ProjectFile, FString* OutError = nullptr);

	// 프로젝트 콘텐츠 폴더 (UE의 <Project>/Content). 씬은 Content/Scenes, Lua 스크립트는 Content/Scripts에 둔다.
	// 모두 프로젝트 폴더 기준 상대 경로다. 작업 디렉터리가 프로젝트 폴더라서 그대로 열 수 있고,
	// 에셋 키와 씬에 저장되는 경로도 이 형식("Content/...")이라 절대 경로로 바꾸지 않는다.
	static constexpr const char* ProjectContentFolder = "Content";
	static std::filesystem::path ProjectContentDir();   // Content
	static std::filesystem::path ProjectScenesDir();    // Content/Scenes
	static std::filesystem::path ProjectScriptsDir();   // Content/Scripts
	// 위 폴더 아래 경로를 UTF-8 문자열로 만든다. Relative가 비어 있으면 폴더 자체. 예: ProjectScenesPath("A.scene") == "Content/Scenes/A.scene"
	static FString ProjectContentPath(const FString& Relative = {});
	static FString ProjectScenesPath(const FString& Relative = {});
	static FString ProjectScriptsPath(const FString& Relative = {});

	// 엔진 폴더 기준 경로. 절대 경로는 그대로 돌려준다.
	static std::filesystem::path EngineFile(const std::filesystem::path& Relative);
	// "EngineResources/..."로 시작하는 경로만 엔진 폴더로 바꾸고 나머지는 그대로 둔다.
	// 셰이더 키처럼 엔진 상대 경로가 데이터로 돌아다니는 곳에서 파일을 열 때 쓴다.
	static std::filesystem::path ResolveEngineRelative(const FString& Path);
	// 엔진 폴더 기준 경로의 UTF-8 문자열. FString을 받는 로더에 넘길 때 쓴다.
	static FString EngineFileUtf8(const std::filesystem::path& Relative);

	// 엔진 리소스: 엔진 코드가 직접 쓰는 아이콘, 폰트 같은 에셋. 실제 위치는 <엔진>/EngineResources (UE의 Engine/Content).
	// UE의 /Engine/처럼 에셋 경로는 "Engine/Icons/Point.png" 형식으로 쓴다. 프로젝트마다 복사할 필요가 없다.
	static constexpr const char* EngineResourcePrefix = "Engine/";
	static std::filesystem::path EngineResourcesDir();
	static bool IsEngineResourcePath(const FString& Path);
	// "Engine/..."이면 엔진 리소스 폴더의 절대 경로(UTF-8)로 바꾸고, 그 밖의 경로는 받은 그대로 돌려준다.
	static FString ResolveEngineResourcePath(const FString& Path);

	// 실행 중 생기는 데이터를 두는 Saved 폴더 이름. 엔진 쪽은 <엔진>/Build/Saved, 프로젝트 쪽은 <프로젝트>/Saved.
	static constexpr const wchar_t* SavedFolder = L"Saved";
	// 엔진 쪽 사용자 데이터(최근 프로젝트 목록 등). <엔진>/Build/Saved
	static std::filesystem::path EngineSavedDir();
	// 지금 프로젝트의 데이터(크래시 덤프 등). <프로젝트>/Saved. 프로젝트를 정하기 전에는 빈 경로.
	static std::filesystem::path ProjectSavedDir();
	// 셰이더 컴파일 캐시(.cso). <엔진>/Build/Cache/Shaders
	static std::filesystem::path EngineShaderCacheDir();

	static FString ToUtf8(const std::filesystem::path& Path);
	static std::filesystem::path FromUtf8(const FString& Utf8);

	// Dir 바로 아래에 있는 첫 .hproject 파일. 없으면 빈 경로.
	static std::filesystem::path FindProjectFileIn(const std::filesystem::path& Dir);

	// 엔진에 딸린 기본 프로젝트 폴더 이름 (<엔진>/HitoriProject).
	static constexpr const wchar_t* DefaultProjectFolder = L"HitoriProject";
	// 기본 프로젝트 파일 (<엔진>/HitoriProject 바로 아래의 .hproject). 없으면 빈 경로.
	static std::filesystem::path FindDefaultProjectFile();
};

// .hproject 파일 내용. JSON 형식이다.
// v1: Name, Description
// v2: + Modules (C++ 모듈 이름 목록, 각 모듈은 Source/<모듈>), EngineDir (이 프로젝트를 만든 엔진 폴더)
struct FProjectDescriptor
{
	static constexpr int32 CurrentFileVersion = 2;

	int32 FileVersion = CurrentFileVersion;
	FString Name;
	FString Description;
	// 비어 있으면 코드 없는 프로젝트다. 엔진 폴더의 HitoriEditor.exe로 연다.
	// 있으면 프로젝트 솔루션에서 빌드한 <Name>Editor.exe로 연다.
	TArray<FString> Modules;
	// 엔진 폴더. 절대 경로이거나 .hproject 폴더 기준 상대 경로. 비어 있으면 모른다는 뜻.
	FString EngineDir;

	bool HasCode() const { return !Modules.IsEmpty(); }
	// EngineDir를 절대 경로로 푼다. 비어 있으면 빈 경로.
	std::filesystem::path ResolveEngineDir(const std::filesystem::path& ProjectFile) const;

	bool Load(const std::filesystem::path& File, FString* OutError = nullptr);
	bool Save(const std::filesystem::path& File) const;
};
