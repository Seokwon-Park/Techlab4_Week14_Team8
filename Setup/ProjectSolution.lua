-- 사용자 C++ 프로젝트의 솔루션을 만든다 (UE의 Generate Visual Studio project files).
-- 사용법: Setup/premake5.exe --file=<엔진>/Setup/ProjectSolution.lua --project=<프로젝트>.hproject vs2026
--         (Setup/GenerateProjectFiles.bat <프로젝트>.hproject 가 같은 일을 한다)
--
-- 만들어지는 것 (프로젝트 폴더 기준):
--   <Name>.slnx
--   Intermediate/ProjectFiles/*.vcxproj  엔진 프로젝트들 + 모듈 + 실행 파일 (엔진 폴더의 .vcxproj와 겹치지 않는다)
--   Build/Bin/<구성>/                    <Name>Editor.exe, <Name>.exe와 엔진 라이브러리
--   Intermediate/Generated/              헤더 툴 결과와 BuildConfig/HitoriBuildConfig.h
--
-- 실행 파일 구성 (monolithic, 엔진을 한 exe에 정적으로 링크):
--   <Name>Editor.exe = 에디터 진입점 + HitoriEditorCore + HitoriEngine + 모듈들
--   <Name>.exe       = 게임 진입점 + HitoriEngine + 모듈들 (VS 프로젝트 이름은 <Name>Game)
--   모듈 <M>          = Source/<M>의 정적 라이브러리 <M>Module.lib

newoption
{
	trigger     = "project",
	value       = "PATH",
	description = "Hitori project file (.hproject) to generate a solution for",
}

include "HitoriBuild.lua"

if not _OPTIONS["project"] then
	-- --help 같은 정보 출력 액션에서는 프로젝트가 없어도 된다
	if _ACTION and _ACTION ~= "--help" then
		error("Pass the project file: --project=Path/To/Game.hproject")
	end
	return
end

local ProjectFile = path.getabsolute(_OPTIONS["project"])
local ProjectDir = path.getdirectory(ProjectFile)

local Text = io.readfile(ProjectFile)
if not Text then error("Cannot read project file: " .. ProjectFile) end
local Descriptor, DecodeError = json.decode(Text)
if not Descriptor then error("Project file is not valid JSON: " .. ProjectFile .. " (" .. tostring(DecodeError) .. ")") end

local ProjectName = Descriptor.Name
if not ProjectName or ProjectName == "" then ProjectName = path.getbasename(ProjectFile) end

-- 모듈: {"Name": "MyGame"} 또는 "MyGame". 모듈 이름은 C++ 식별자여야 한다(FID 매크로, 클래스 접두사에 쓰인다).
local Modules = {}
for _, Module in ipairs(Descriptor.Modules or {}) do
	local ModuleName = type(Module) == "table" and Module.Name or Module
	if type(ModuleName) ~= "string" or not ModuleName:match("^[%a_][%w_]*$") then
		error("Invalid module name in " .. ProjectFile .. ": " .. tostring(ModuleName))
	end
	if not os.isdir(path.join(ProjectDir, "Source", ModuleName)) then
		error("Module source folder not found: " .. path.join(ProjectDir, "Source", ModuleName))
	end
	table.insert(Modules, ModuleName)
end
if #Modules == 0 then
	error(ProjectFile .. " has no C++ modules. Code-less projects open with the engine's HitoriEditor.exe.")
end

workspace (ProjectName)
	location (ProjectDir)
	Hitori.WorkspaceSettings()
	startproject (ProjectName .. "Editor")

	local Ctx = Hitori.MakeContext(
	{
		Root            = ProjectDir,
		ProjectFilesDir = path.join(ProjectDir, "Intermediate/ProjectFiles"),
		ProjectFile     = ProjectFile,
	})

	group "Engine"
		Hitori.DefineEngineProjects(Ctx)
		Hitori.ShadersProject(Ctx)

	group "Game"
		for _, ModuleName in ipairs(Modules) do
			Hitori.ModuleProject(Ctx, ModuleName, path.join(ProjectDir, "Source", ModuleName))
		end

		local DebugArgs = { '"' .. ProjectFile .. '"' }
		Hitori.EditorApp(Ctx, ProjectName .. "Editor", { Modules = Modules, DebugArgs = DebugArgs })
		-- 모듈 프로젝트와 이름이 겹치지 않게 프로젝트는 <Name>Game, 파일은 <Name>.exe
		Hitori.GameApp(Ctx, ProjectName .. "Game", { Modules = Modules, DebugArgs = DebugArgs, TargetName = ProjectName })

	group ""
