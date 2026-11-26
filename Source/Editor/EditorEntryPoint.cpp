#include "EnginePCH.h"

#include "Launch/EntryPoint.h"
#include "Core/Misc/Paths.h"

#include "Editor/HitoriEd/EditorEngine.h"
#ifdef OBJ_VIEWER
#include "Programs/ObjViewer/ObjViewerApp.h"
#elif BENCHMARK
#include "Programs/Benchmark/BenchmarkApp.h"
#endif

UClass* GetEngineClass()
{
#ifdef OBJ_VIEWER
	return UObjViewerEngine::StaticClass();
#elif BENCHMARK
	return UBenchmarkEngine::StaticClass();
#else
	return UEditorEngine::StaticClass();
#endif
}

#if !defined(OBJ_VIEWER) && !defined(BENCHMARK)
#include "Editor/ProjectBrowser/ProjectBrowser.h"
#endif

std::filesystem::path ResolveProjectFile(HINSTANCE hInstance, const std::filesystem::path& FromCommandLine)
{
#if defined(OBJ_VIEWER) || defined(BENCHMARK)
	// 도구 빌드는 프로젝트를 고르지 않고 엔진 폴더의 기본 프로젝트를 쓴다.
	(void)hInstance;
	return FromCommandLine.empty() ? FPaths::FindProjectFileIn(FPaths::EngineDir()) : FromCommandLine;
#else
	// 프로젝트 솔루션에서 빌드한 <Name>Editor.exe는 자기 프로젝트를 연다.
	// 그 밖에는 커맨드라인으로 받지 않았으면 UE처럼 프로젝트 브라우저에서 고르거나 새로 만든다.
	std::filesystem::path ProjectFile = FromCommandLine;
	if (ProjectFile.empty()) ProjectFile = FPaths::BuiltProjectFile();
	if (ProjectFile.empty()) ProjectFile = ProjectBrowser::Run(hInstance);
	if (!ProjectFile.empty()) RecentProjects::Add(ProjectFile);
	return ProjectFile;
#endif
}
