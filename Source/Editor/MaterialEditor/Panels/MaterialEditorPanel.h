#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"

class FMaterialEditor;

class FMaterialEditorPanel : public TAssetEditorPanel<FMaterialEditor>
{
public:
	using TAssetEditorPanel<FMaterialEditor>::TAssetEditorPanel;
};
