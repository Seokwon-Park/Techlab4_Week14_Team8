-- 엔진 솔루션 (Hitori.slnx). 프로젝트 정의는 Setup/HitoriBuild.lua에 있고,
-- 사용자 C++ 프로젝트의 솔루션도 같은 정의를 재사용한다.
include "Setup/HitoriBuild.lua"

workspace "Hitori"
	Hitori.WorkspaceSettings()
	startproject "HitoriEditor"

	local Ctx = Hitori.MakeContext({ Root = _MAIN_SCRIPT_DIR })

	-- ImGui, Lua, HitoriHeaderTool, HitoriEngine, HitoriEditorCore
	Hitori.DefineEngineProjects(Ctx)

	-- 에디터 애플리케이션 (코드 없는 프로젝트용). ObjViewer 구성에서는 같은 exe가 뷰어로 빌드된다.
	Hitori.EditorApp(Ctx, "HitoriEditor")

	-- 패키징용 게임 실행 파일
	Hitori.GameApp(Ctx, "HitoriGame")

	Hitori.ShadersProject(Ctx)
