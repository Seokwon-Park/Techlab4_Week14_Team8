#pragma once

#include "Editor/MaterialEditor/Panels/MaterialEditorPanel.h"

class FMaterialViewportPanel : public FMaterialEditorPanel
{
public:
	explicit FMaterialViewportPanel(FMaterialEditor& InEditor);

protected:
	void DrawContent() override;
};
