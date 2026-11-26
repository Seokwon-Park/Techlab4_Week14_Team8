#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Core/EngineString.h"

#include <functional>

class UWorld;
class UClass;

// UE5 World Settings 패널. 지금 보고 있는 월드의 설정(Game Mode, Physics, Time)을 고친다.
// 값은 UWorld의 프로퍼티라 씬 파일에 저장되고 Ctrl+Z로 되돌릴 수 있다.
// PIE 중에는 PIE 월드를 보여 주며, 그때 바꾼 값은 그 플레이에만 쓰이고 저장되지 않는다.
class FWorldSettingsPanel : public IEditorPanel
{
public:
	bool Init() override { return true; }
	void Tick(float DeltaTime) override {}
	void OnRender() override;
	const char* GetPanelName() const override { return "World Settings"; }

	void SetWorld(UWorld* InWorld) { World = InWorld; }
	// GameMode Override가 Blueprint일 때 Edit 버튼이 그 에셋을 연다.
	void SetOpenAssetCallback(std::function<void(const FString&)> InCallback) { OpenAssetCallback = std::move(InCallback); }

	// 패널을 열고 앞으로 가져온다. Outliner 메뉴의 World Settings가 부른다.
	void Focus();

private:
	void DrawGameModeSection();
	void DrawPhysicsSection();
	void DrawTimeSection();

	// BaseClass와 그 자식 클래스를 고르는 콤보. 빈 문자열은 None(기본값)이다. 바뀌면 true.
	bool DrawClassPicker(const char* Id, UClass* BaseClass, FString& InOutClassName);
	// 고른 GameMode가 정해 둔 클래스 한 줄. C++ GameMode면 고칠 수 없고(클래스에서 바꾼다),
	// Blueprint GameMode면 UE처럼 여기서 바로 고치고 그 BP 에셋에 저장한다.
	void DrawGameModeClassRow(UClass* GameModeClass, const char* Label, const char* PropertyName, const FString& ClassName,
		UClass* BaseClass, const char* Tooltip);

	UWorld* World = nullptr;
	std::function<void(const FString&)> OpenAssetCallback;
	char Search[128] = {};
	bool bFocusRequested = false;
};
