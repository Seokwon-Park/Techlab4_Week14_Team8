#include "EnginePCH.h"
#include "Core/Misc/Paths.h"

#include <fstream>
#include <Windows.h>

// 프로젝트 솔루션이 만드는 빌드 설정 (HITORI_ENGINE_DIR, HITORI_PROJECT_FILE). 엔진 솔루션에는 없다.
#if __has_include("HitoriBuildConfig.h")
#include "HitoriBuildConfig.h"
#endif

namespace fs = std::filesystem;

namespace
{
	fs::path GEngineDir;
	fs::path GProjectDir;
	fs::path GProjectFile;
	FString GProjectName;

	bool StartsWithFolder(const FString& Path, const char* Folder)
	{
		const size_t Length = std::strlen(Folder);
		if (Path.size() <= Length) return false;
		if (_strnicmp(Path.c_str(), Folder, Length) != 0) return false;
		return Path[Length] == '/' || Path[Length] == '\\';
	}
}

namespace
{
	bool IsEngineDir(const fs::path& Dir)
	{
		std::error_code Ec;
		return !Dir.empty() && fs::exists(Dir / L"Resources" / L"Shader", Ec);
	}

	fs::path EngineDirFromProjectFile(const fs::path& ProjectFile)
	{
		if (ProjectFile.empty()) return {};
		FProjectDescriptor Descriptor;
		if (!Descriptor.Load(ProjectFile)) return {};
		return Descriptor.ResolveEngineDir(ProjectFile);
	}
}

fs::path FPaths::BuiltProjectFile()
{
#ifdef HITORI_PROJECT_FILE
	return fs::path(HITORI_PROJECT_FILE);   // u8 리터럴(char8_t)은 UTF-8로 읽힌다
#else
	return {};
#endif
}

bool FPaths::InitEngineDir(const fs::path& ProjectFileHint)
{
	std::error_code Ec;

	// 1. exe 위쪽
	wchar_t ExePath[MAX_PATH] = {};
	if (GetModuleFileNameW(nullptr, ExePath, MAX_PATH))
	{
		fs::path Dir = fs::path(ExePath).parent_path();
		while (!Dir.empty())
		{
			if (IsEngineDir(Dir))
			{
				GEngineDir = Dir;
				return true;
			}
			const fs::path Parent = Dir.parent_path();
			if (Parent == Dir) break;
			Dir = Parent;
		}
	}

	// 2. 빌드할 때 정해진 엔진 폴더. 프로젝트 솔루션이 Intermediate/Generated/BuildConfig에 만든다.
#ifdef HITORI_ENGINE_DIR
	if (const fs::path Built(HITORI_ENGINE_DIR); IsEngineDir(Built))
	{
		GEngineDir = Built.lexically_normal();
		return true;
	}
#endif

	// 3. 프로젝트 파일이 기억하는 엔진 폴더 (엔진을 옮긴 뒤 프로젝트를 다시 생성하기 전 등)
	for (const fs::path& ProjectFile : { ProjectFileHint, BuiltProjectFile() })
	{
		if (const fs::path FromProject = EngineDirFromProjectFile(ProjectFile); IsEngineDir(FromProject))
		{
			GEngineDir = FromProject.lexically_normal();
			return true;
		}
	}

	// 4. 예전처럼 작업 디렉터리(디버거의 debugdir)를 엔진 폴더로 본다.
	GEngineDir = fs::current_path(Ec);
	return IsEngineDir(GEngineDir);
}

const fs::path& FPaths::EngineDir() { return GEngineDir; }
const fs::path& FPaths::ProjectDir() { return GProjectDir; }
const fs::path& FPaths::ProjectFilePath() { return GProjectFile; }
const FString& FPaths::ProjectName() { return GProjectName; }
bool FPaths::HasProject() { return !GProjectFile.empty(); }

bool FPaths::SetProject(const fs::path& ProjectFile, FString* OutError)
{
	std::error_code Ec;
	const fs::path Absolute = fs::absolute(ProjectFile, Ec).lexically_normal();
	FProjectDescriptor Descriptor;
	if (Ec || !Descriptor.Load(Absolute, OutError)) return false;

	const fs::path Dir = Absolute.parent_path();
	if (!SetCurrentDirectoryW(Dir.c_str()))
	{
		if (OutError) *OutError = "Cannot enter project folder: " + ToUtf8(Dir);
		return false;
	}

	GProjectFile = Absolute;
	GProjectDir = Dir;
	GProjectName = Descriptor.Name.empty() ? ToUtf8(Absolute.stem()) : Descriptor.Name;
	return true;
}

fs::path FPaths::EngineFile(const fs::path& Relative)
{
	if (Relative.is_absolute()) return Relative;
	return (GEngineDir / Relative).lexically_normal();
}

fs::path FPaths::ResolveEngineRelative(const FString& Path)
{
	if (StartsWithFolder(Path, "Resources") || StartsWithFolder(Path, "Intermediate"))
		return EngineFile(FromUtf8(Path));
	return FromUtf8(Path);
}

FString FPaths::EngineFileUtf8(const fs::path& Relative)
{
	return ToUtf8(EngineFile(Relative));
}

fs::path FPaths::EngineContentDir()
{
	return GEngineDir / L"EngineContent";
}

bool FPaths::IsEngineContentPath(const FString& Path)
{
	return StartsWithFolder(Path, "Engine");
}

FString FPaths::ResolveContentPath(const FString& Path)
{
	if (!IsEngineContentPath(Path)) return Path;
	const size_t PrefixLength = std::strlen(EngineContentPrefix);
	return ToUtf8(EngineContentDir() / FromUtf8(Path.substr(PrefixLength)));
}

fs::path FPaths::EngineSavedDir()
{
	return GEngineDir / L"Saved";
}

FString FPaths::ToUtf8(const fs::path& Path)
{
	const std::u8string Utf8 = Path.generic_u8string();
	return FString(Utf8.begin(), Utf8.end());
}

fs::path FPaths::FromUtf8(const FString& Utf8)
{
	return fs::path(std::u8string(Utf8.begin(), Utf8.end()));
}

fs::path FPaths::FindProjectFileIn(const fs::path& Dir)
{
	std::error_code Ec;
	for (const fs::directory_entry& Entry : fs::directory_iterator(Dir, Ec))
	{
		if (Entry.is_regular_file(Ec) && _wcsicmp(Entry.path().extension().c_str(), ProjectExtension) == 0)
			return Entry.path();
	}
	return {};
}

bool FProjectDescriptor::Load(const fs::path& File, FString* OutError)
{
	std::ifstream In(File);
	if (!In)
	{
		if (OutError) *OutError = "Cannot open project file: " + FPaths::ToUtf8(File);
		return false;
	}

	const json Root = json::parse(In, nullptr, false);
	if (Root.is_discarded() || !Root.is_object())
	{
		if (OutError) *OutError = "Project file is not valid JSON: " + FPaths::ToUtf8(File);
		return false;
	}

	// v1 파일에는 Modules와 EngineDir가 없다. 없으면 코드 없는 프로젝트로 읽는다.
	FileVersion = Root.value("FileVersion", 1);
	Name = Root.value("Name", FString());
	Description = Root.value("Description", FString());
	EngineDir = Root.value("EngineDir", FString());

	Modules.Empty();
	const auto ModulesIt = Root.find("Modules");
	if (ModulesIt != Root.end() && ModulesIt->is_array())
	{
		// UE의 .uproject처럼 {"Name": "MyGame"} 객체 배열. 문자열만 적어도 받는다.
		for (const json& Module : *ModulesIt)
		{
			FString ModuleName;
			if (Module.is_string()) ModuleName = Module.get<FString>();
			else if (Module.is_object()) ModuleName = Module.value("Name", FString());
			if (!ModuleName.empty()) Modules.Add(ModuleName);
		}
	}
	return true;
}

bool FProjectDescriptor::Save(const fs::path& File) const
{
	json Root;
	Root["FileVersion"] = FileVersion;
	Root["Name"] = Name;
	Root["Description"] = Description;
	if (!EngineDir.empty()) Root["EngineDir"] = EngineDir;
	if (!Modules.IsEmpty())
	{
		json ModulesJson = json::array();
		for (const FString& ModuleName : Modules)
			ModulesJson.push_back({ { "Name", ModuleName }, { "Type", "Runtime" } });
		Root["Modules"] = ModulesJson;
	}

	std::ofstream Out(File);
	if (!Out) return false;
	Out << Root.dump(4) << '\n';
	return static_cast<bool>(Out);
}

fs::path FProjectDescriptor::ResolveEngineDir(const fs::path& ProjectFile) const
{
	if (EngineDir.empty()) return {};
	const fs::path Dir = FPaths::FromUtf8(EngineDir);
	if (Dir.is_absolute()) return Dir.lexically_normal();
	return (ProjectFile.parent_path() / Dir).lexically_normal();
}
