#pragma once

#include "Editor/MaterialEditor/Panels/MaterialEditorPanel.h"

class FMaterialDetailsPanel : public FMaterialEditorPanel
{
public:
	explicit FMaterialDetailsPanel(FMaterialEditor& InEditor);

protected:
	void DrawContent() override;
};
