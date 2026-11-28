#include "EnginePCH.h"
#include "Editor/HitoriEd/ProjectCompiler.h"

#include <algorithm>

#include "Core/Misc/Paths.h"
#include "Core/Window.h"
#include "Engine/Engine.h"
#include "Launch/LaunchEngineLoop.h"

namespace fs = std::filesystem;

namespace
{
	constexpr const wchar_t* StaleSuffix = L".old";

	fs::path RunningExePath()
	{
		wchar_t Buffer[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, Buffer, MAX_PATH);
		return fs::path(Buffer);
	}

	// 실행 중인 exe가 있는 폴더 이름 (예: Debug-windows-x86_64). 빌드 결과도 같은 구성 폴더에 둔다.
	fs::path RunningBinFolderName()
	{
		const fs::path Folder = RunningExePath().parent_path().filename();
		return Folder.empty() ? fs::path(L"Debug-windows-x86_64") : Folder;
	}

	// 폴더 이름의 첫 '-' 앞이 빌드 구성 (Debug-windows-x86_64 -> Debug)
	FString RunningConfigName()
	{
		const FString Folder = FPaths::ToUtf8(RunningBinFolderName());
		const size_t Dash = Folder.find('-');
		return Dash == FString::npos || Dash == 0 ? FString("Debug") : Folder.substr(0, Dash);
	}

	FString ProjectName(const FProjectDescriptor& Descriptor)
	{
		return Descriptor.Name.empty() ? FPaths::ToUtf8(FPaths::ProjectFilePath().stem()) : Descriptor.Name;
	}

	fs::path StalePath(const fs::path& File)
	{
		fs::path Result = File;
		Result += StaleSuffix;
		return Result;
	}

	bool IsSameFile(const fs::path& A, const fs::path& B)
	{
		std::error_code Ec;
		return fs::exists(A, Ec) && fs::exists(B, Ec) && fs::equivalent(A, B, Ec);
	}
}

FProjectCompiler::~FProjectCompiler()
{
	// 에디터를 닫아도 빌드는 끝까지 돈다. 핸들만 놓는다.
	if (Process) CloseHandle(Process);
	if (OutputRead) CloseHandle(OutputRead);
}

bool FProjectCompiler::CanCompile()
{
	if (!FPaths::HasProject()) return false;
	FProjectDescriptor Descriptor;
	if (!Descriptor.Load(FPaths::ProjectFilePath()) || !Descriptor.HasCode()) return false;
	// 빌드 스크립트가 없는 배포본 엔진에서는 컴파일할 수 없다
	std::error_code Ec;
	return fs::exists(FPaths::EngineFile(L"Setup/Build/CompileProject.bat"), Ec);
}

void FProjectCompiler::CleanupStaleBinaries()
{
	const fs::path Exe = RunningExePath();
	fs::path Pdb = Exe;
	Pdb.replace_extension(L".pdb");
	for (const fs::path& File : { Exe, Pdb })
	{
		std::error_code Ec;
		fs::remove(StalePath(File), Ec);   // 이전 에디터가 아직 끝나는 중이면 지우지 못한다. 다음 시작 때 다시 지운다
	}
}

bool FProjectCompiler::Start()
{
	if (IsCompiling()) return false;
	if (!CanCompile())
	{
		HTR_LOG(Error, "Compile: this project has no C++ modules (or the engine has no Setup/Build/CompileProject.bat).");
		return false;
	}

	FProjectDescriptor Descriptor;
	Descriptor.Load(FPaths::ProjectFilePath());
	const FString Name = ProjectName(Descriptor);
	const FString Config = RunningConfigName();
	TargetExe = FPaths::ProjectDir() / L"Build" / L"Bin" / RunningBinFolderName() / (FPaths::FromUtf8(Name).native() + L"Editor.exe");

	SECURITY_ATTRIBUTES Security{ sizeof(Security), nullptr, TRUE };
	HANDLE OutputWrite = nullptr;
	if (!CreatePipe(&OutputRead, &OutputWrite, &Security, 0)) return false;
	SetHandleInformation(OutputRead, HANDLE_FLAG_INHERIT, 0);   // 읽는 쪽은 자식에게 물려주지 않는다

	MoveRunningBinariesAside();

	// cmd /c는 바깥 따옴표 한 쌍을 벗기므로 전체를 한 번 더 감싼다.
	const fs::path Script = FPaths::EngineFile(L"Setup/Build/CompileProject.bat");
	std::wstring CommandLine = L"cmd.exe /d /c \"\"" + Script.wstring() + L"\" \"" + FPaths::ProjectFilePath().wstring() +
		L"\" " + FPaths::FromUtf8(Name).wstring() + L" " + FPaths::FromUtf8(Config).wstring() + L"\"";

	STARTUPINFOW Startup{};
	Startup.cb = sizeof(Startup);
	Startup.dwFlags = STARTF_USESTDHANDLES;
	Startup.hStdOutput = OutputWrite;
	Startup.hStdError = OutputWrite;
	PROCESS_INFORMATION Info{};
	const BOOL bCreated = CreateProcessW(nullptr, CommandLine.data(), nullptr, nullptr, TRUE,
		CREATE_NO_WINDOW, nullptr, FPaths::EngineDir().c_str(), &Startup, &Info);
	CloseHandle(OutputWrite);   // 쓰기 핸들은 자식만 가져야 자식이 끝날 때 파이프가 닫힌다
	if (!bCreated)
	{
		HTR_LOG(Error, "Compile: failed to start CompileProject.bat (error {})", GetLastError());
		CloseHandle(OutputRead);
		OutputRead = nullptr;
		RestoreRunningBinaries();
		State = EState::Failed;
		return false;
	}
	CloseHandle(Info.hThread);
	Process = Info.hProcess;
	State = EState::Compiling;
	HTR_LOG(Info, "Compile started: {}Editor ({})", Name, Config);
	return true;
}

void FProjectCompiler::Tick()
{
	if (!Process) return;
	// 끝났는지 먼저 본다. 끝났다면 마지막 출력까지 이미 파이프에 있으므로 아래에서 다 읽힌다.
	const bool bExited = WaitForSingleObject(Process, 0) == WAIT_OBJECT_0;
	DrainOutput();
	if (bExited) Finish();
}

void FProjectCompiler::DrainOutput()
{
	DWORD Available = 0;
	while (PeekNamedPipe(OutputRead, nullptr, 0, nullptr, &Available, nullptr) && Available > 0)
	{
		char Buffer[1024];
		DWORD Read = 0;
		if (!ReadFile(OutputRead, Buffer, std::min<DWORD>(Available, sizeof(Buffer)), &Read, nullptr) || Read == 0)
			break;
		Pending.append(Buffer, Read);
	}

	size_t NewLine;
	while ((NewLine = Pending.find('\n')) != FString::npos)
	{
		FString Line = Pending.substr(0, NewLine);
		if (!Line.empty() && Line.back() == '\r') Line.pop_back();
		Pending.erase(0, NewLine + 1);
		if (Line.empty()) continue;
		// MSBuild 진단 형식: file(line,col): error C2065: ...
		if (Line.find(": error ") != FString::npos || Line.find(": fatal error ") != FString::npos)
			HTR_LOG(Error, "[Compile] {}", Line);
		else if (Line.find(": warning ") != FString::npos)
			HTR_LOG(Warning, "[Compile] {}", Line);
		else
			HTR_LOG(Info, "[Compile] {}", Line);
	}
}

void FProjectCompiler::Finish()
{
	if (!Pending.empty()) HTR_LOG(Info, "[Compile] {}", Pending);
	Pending.clear();

	DWORD ExitCode = 1;
	GetExitCodeProcess(Process, &ExitCode);
	CloseHandle(Process);
	CloseHandle(OutputRead);
	Process = nullptr;
	OutputRead = nullptr;

	std::error_code Ec;
	if (ExitCode == 0 && fs::exists(TargetExe, Ec))
	{
		State = EState::Succeeded;
		HTR_LOG(Info, "Compile succeeded: {}. Restart the editor to use the new code.", FPaths::ToUtf8(TargetExe));
	}
	else
	{
		RestoreRunningBinaries();
		State = EState::Failed;
		HTR_LOG(Error, "Compile failed (exit code {}). See the log above.", ExitCode);
	}
}

void FProjectCompiler::MoveRunningBinariesAside()
{
	MovedAside.clear();
	const fs::path Exe = RunningExePath();
	if (!IsSameFile(Exe, TargetExe)) return;   // 엔진의 HitoriEditor.exe로 열었으면 빌드 결과와 겹치지 않는다

	fs::path Pdb = Exe;
	Pdb.replace_extension(L".pdb");
	for (const fs::path& File : { Exe, Pdb })
	{
		std::error_code Ec;
		if (!fs::exists(File, Ec)) continue;
		fs::remove(StalePath(File), Ec);
		// 실행 중인 exe는 쓰기는 막혀 있어도 이름은 바꿀 수 있다
		if (MoveFileExW(File.c_str(), StalePath(File).c_str(), MOVEFILE_REPLACE_EXISTING))
			MovedAside.push_back(File);
		else
			HTR_LOG(Warning, "Compile: could not move {} aside (error {}). Linking may fail while it is in use.",
				FPaths::ToUtf8(File), GetLastError());
	}
}

void FProjectCompiler::RestoreRunningBinaries()
{
	// 새 파일이 생기지 않은 자리만 되돌린다 (컴파일 오류로 링크까지 가지 못한 경우)
	for (const fs::path& File : MovedAside)
	{
		std::error_code Ec;
		if (!fs::exists(File, Ec))
			MoveFileExW(StalePath(File).c_str(), File.c_str(), 0);
	}
	MovedAside.clear();
}

bool FProjectCompiler::RestartEditor()
{
	if (State != EState::Succeeded) return false;

	std::wstring CommandLine = L"\"" + TargetExe.wstring() + L"\" \"" + FPaths::ProjectFilePath().wstring() + L"\"";
	STARTUPINFOW Startup{};
	Startup.cb = sizeof(Startup);
	PROCESS_INFORMATION Info{};
	if (!CreateProcessW(TargetExe.c_str(), CommandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
		FPaths::ProjectDir().c_str(), &Startup, &Info))
	{
		HTR_LOG(Error, "Restart: failed to start {} (error {})", FPaths::ToUtf8(TargetExe), GetLastError());
		return false;
	}
	CloseHandle(Info.hThread);
	CloseHandle(Info.hProcess);

	// 새 에디터가 뜨는 동안 이 에디터는 평소처럼 창을 닫아 종료한다
	if (GEngine)
		if (FWindow* Window = GEngine->GetEngineLoop().GetMainWindow())
			PostMessageW(Window->GetHandle(), WM_CLOSE, 0, 0);
	return true;
}
