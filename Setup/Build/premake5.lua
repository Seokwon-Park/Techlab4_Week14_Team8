-- 엔진 솔루션 (Hitori.slnx). 프로젝트 정의는 Setup/Build/HitoriBuild.lua에 있고,
-- 사용자 C++ 프로젝트의 솔루션도 같은 정의를 재사용한다.
-- 이 파일은 <엔진>/Setup/Build에 있지만 솔루션은 엔진 폴더에 만든다.
include "HitoriBuild.lua"

workspace "Hitori"
	location (Hitori.EngineDir)
	Hitori.WorkspaceSettings()
	startproject "HitoriEditor"

	local Ctx = Hitori.MakeContext({ Root = Hitori.EngineDir })

	-- ImGui, Lua, HitoriHeaderTool, HitoriEngine, HitoriEditorCore
	Hitori.DefineEngineProjects(Ctx)

	-- 에디터 애플리케이션 (코드 없는 프로젝트용). ObjViewer 구성에서는 같은 exe가 뷰어로 빌드된다.
	Hitori.EditorApp(Ctx, "HitoriEditor")

	-- 패키징용 게임 실행 파일
	Hitori.GameApp(Ctx, "HitoriGame")

	Hitori.ShadersProject(Ctx)
