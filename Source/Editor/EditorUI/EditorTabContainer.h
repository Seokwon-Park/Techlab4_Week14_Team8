#pragma once

#include <functional>

#include "Containers/Array.h"
#include "Editor/EditorUI/EditorTab.h"

// 탭은 각 편집기가 소유하며, 등록한 탭의 수명은 컨테이너보다 길어야 한다.
class FEditorTabContainer
{
public:
	void AddTab(IEditorTab* Tab);

	void RemoveTab(IEditorTab* Tab);
	void ActivateTab(IEditorTab* Tab);
	void RequestCloseTab(IEditorTab* Tab);
	void ProcessCloseRequests();

	bool HasTab(const IEditorTab* Tab) const
	{
		for (const IEditorTab* Existing : Tabs)
			if (Existing == Tab)
				return true;
		return false;
	}

	IEditorTab* GetMainSelectedTab() const { return MainSelectedTab; }
	IEditorTab* GetFocusedTab() const { return FocusedTab; }

	void DrawMainMenuBar();

	void OnRender(bool bPassthruCentralNode = false);

	// 닫을 수 있는 탭마다 ConfirmClose(저장할지 묻기)를 부른다. 하나라도 취소하면 false.
	bool ConfirmCloseAllTabs();

private:
	void DrawTabs();

	static bool IsInDockSpace(const ImGuiWindow* Window, ImGuiID DockSpaceId);

	static void DrawTabMenu(IEditorTab& Tab);

	TArray<IEditorTab*> Tabs;
	TArray<IEditorTab*> PendingCloseTabs;
	IEditorTab* PendingActivation = nullptr;
	bool bRendering = false;
	IEditorTab* MainSelectedTab = nullptr;
	IEditorTab* FocusedTab = nullptr;
};
