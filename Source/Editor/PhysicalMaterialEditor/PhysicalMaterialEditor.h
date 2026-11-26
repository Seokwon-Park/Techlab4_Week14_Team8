#pragma once

#include "Editor/AssetEditor/AssetEditor.h"
#include "Editor/AssetEditor/AssetEditorTab.h"

class UPhysicalMaterial;

// .pmat 편집기. 미리보기 없이 프로퍼티만 보여 준다.
class FPhysicalMaterialEditor : public FAssetEditor
{
public:
	FPhysicalMaterialEditor(class UEditorEngine& InEditorEngine, class FEditorManager& InEditorManager,
		const FString& InAssetPath, UPhysicalMaterial& InMaterial);

	const char* GetEditorTypeName() const override { return "Physical Material"; }
	bool IsDirty() const override { return bDirty; }
	void Tick(float DeltaTime) override {}

	UPhysicalMaterial* GetMaterial() const { return Material; }
	void MarkDirty() { bDirty = true; }
	bool Save();

protected:
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override { return true; }
	void ShutdownResources() override {}

private:
	UPhysicalMaterial* Material = nullptr;
	bool bDirty = false;
};

class FPhysicalMaterialEditorTab : public FAssetEditorTab
{
public:
	explicit FPhysicalMaterialEditorTab(FPhysicalMaterialEditor& InEditor);

	void DrawMenuBar() override;
	void OnRender(bool bActive) override;

private:
	FPhysicalMaterialEditor& PhysicalMaterialEditor;
};
