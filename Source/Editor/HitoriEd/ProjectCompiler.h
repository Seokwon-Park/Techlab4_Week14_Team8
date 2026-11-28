#pragma once

#include "Core/EngineString.h"
#include <filesystem>

// 에디터의 Compile 버튼. C++ 프로젝트의 <Name>Editor.exe를 Setup/Build/CompileProject.bat으로 빌드하고
// 출력을 Output Log로 옮긴다 (FGamePackager와 같은 구조).
//
// 이 엔진은 엔진·에디터·모듈을 exe 하나에 정적으로 링크하므로 실행 중에 코드를 바꿔 끼울 수 없다.
// 대신 빌드가 끝나면 새 exe로 에디터를 다시 띄운다 (RestartEditor).
// 실행 중인 exe는 덮어쓸 수 없지만 이름은 바꿀 수 있으므로, 빌드 전에 <Name>Editor.exe를 .old로 비켜 둔다.
class FProjectCompiler
{
public:
	enum class EState : uint8 { Idle, Compiling, Succeeded, Failed };

	~FProjectCompiler();

	// 지금 프로젝트가 C++ 모듈을 가진 프로젝트인지 (아니면 Compile 버튼을 숨긴다)
	static bool CanCompile();
	// 지난 Compile에서 비켜 둔 실행 파일(*.old)을 지운다. 에디터 시작 때 부른다.
	static void CleanupStaleBinaries();

	bool Start();
	// 매 프레임 부른다. 파이프에 이미 쌓인 출력만 읽고 바로 돌아온다.
	void Tick();
	// 빌드한 <Name>Editor.exe를 같은 프로젝트로 띄우고 이 에디터를 닫는다. 성공한 뒤에만 쓴다.
	bool RestartEditor();

	EState GetState() const { return State; }
	bool IsCompiling() const { return State == EState::Compiling; }
	// 빌드 결과 exe
	const std::filesystem::path& GetTargetExe() const { return TargetExe; }

private:
	void DrainOutput();
	void Finish();
	// 실행 중인 exe가 빌드 결과 자리에 있으면 이름을 바꿔 링커가 새 파일을 쓸 수 있게 한다
	void MoveRunningBinariesAside();
	void RestoreRunningBinaries();

	EState State = EState::Idle;
	HANDLE Process = nullptr;
	HANDLE OutputRead = nullptr;
	FString Pending;   // 아직 줄바꿈이 오지 않은 출력
	std::filesystem::path TargetExe;
	std::vector<std::filesystem::path> MovedAside;   // .old로 비켜 둔 원래 경로
};
