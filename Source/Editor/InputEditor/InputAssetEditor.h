#pragma once

#include <vector>

#include "Editor/AssetEditor/AssetEditor.h"

class UInputAction;
class UInputMappingContext;

enum class EInputAssetKind : uint8
{
	Action,		// *.inputaction
	Context,	// *.inputcontext
};

// Input Action / Input Mapping Context 에셋 에디터. UE에서 IA_ / IMC_ 에셋을 더블클릭하면 뜨는 창에 해당한다.
// 미리보기 장면 없이 속성만 편집한다.
//
// FInputAssetLibrary가 캐시한 객체를 직접 고친다. 그래서 PIE 중에 고치면 바로 게임에 반영되고(감도 조절 등),
// 저장하지 않고 닫으면 디스크 내용으로 되돌린다.
class FInputAssetEditor : public FAssetEditor
{
public:
	FInputAssetEditor(class UEditorEngine& InEditorEngine, class FEditorManager& InEditorManager, const FString& InAssetPath);

	static bool CanOpen(const FString& AssetPath);

	const char* GetEditorTypeName() const override;
	void Tick(float DeltaTime) override;
	void OnRenderPreview() override {}
	bool IsDirty() const override { return bDirty; }
	bool ConfirmClose() override;

	bool Save();
	// 디스크 내용으로 되돌린다
	void Revert();

	EInputAssetKind GetKind() const { return Kind; }
	UInputAction* GetAction() const { return Action; }
	UInputMappingContext* GetContext() const { return Context; }

	// 패널이 값을 바꿨을 수 있을 때 부른다. 다음 Tick에서 저장된 상태와 비교한다
	void NoteEdited() { bCheckDirty = true; }

	// 매핑의 Action 선택 목록. Assets 아래 *.inputaction 전부 (Assets/... 형식)
	const std::vector<FString>& GetActionAssetPaths(bool bRefresh);

protected:
	bool InitializeResources() override;
	void ShutdownResources() override;
	TUniquePtr<class IEditorTab> CreateTab() override;

private:
	FString Serialize() const;

	EInputAssetKind Kind = EInputAssetKind::Action;
	UInputAction* Action = nullptr;
	UInputMappingContext* Context = nullptr;

	FString SavedText;
	bool bDirty = false;
	bool bCheckDirty = false;

	std::vector<FString> ActionAssetPaths;
};
