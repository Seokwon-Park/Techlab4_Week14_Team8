#pragma once

#include "Editor/EditorUI/EditorTab.h"
#include "Editor/ProjectBrowser/ProjectBrowserView.h"

class FEditorTabContainer;

// 에디터 탭 줄 맨 왼쪽의 집 아이콘 탭 (UE 에디터의 Home 탭). 닫거나 옮길 수 없다.
// 엔진을 띄우기 전의 프로젝트 브라우저와 같은 화면(FProjectBrowserView)을 보여 준다.
// 다른 프로젝트를 고르거나 새로 만들면, 열린 에셋 편집기의 저장 여부를 묻고 그 프로젝트로 에디터를 다시 띄운다.
// (한 프로세스가 프로젝트 하나만 연다: 작업 디렉터리, 에셋, BP 클래스가 모두 프로젝트에 묶여 있기 때문이다.)
class FHomeTab : public IEditorTab
{
public:
	// MenuSource: 이 탭이 앞에 있을 때 제목 줄에 대신 보여 줄 메뉴의 탭 (레벨 에디터의 File/Edit/Help).
	FHomeTab(FEditorTabContainer& InContainer, IEditorTab* InMenuSource);

	// 이름 없이 아이콘만 보인다. ### 뒤는 창 ID다.
	const char* GetTabName() const override { return "###EditorHomeTab"; }
	bool GetTabIcon(EEditorIcon& OutIcon, ImU32& OutColor) const override;
	bool CanDetach() const override { return false; }
	bool IsLeadingTab() const override { return true; }
	void DrawMenuBar() override;
	void OnRender(bool bActive) override;

private:
	void SwitchToProject(const std::filesystem::path& ProjectFile);

	FEditorTabContainer& Container;
	IEditorTab* MenuSource = nullptr;
	FProjectBrowserView View;
	bool bWasActive = false;
};
