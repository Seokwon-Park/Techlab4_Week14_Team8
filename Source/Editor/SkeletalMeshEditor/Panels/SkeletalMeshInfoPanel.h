#pragma once

#include "SkeletalMeshEditorPanel.h"

// UE5 Asset Details처럼 머티리얼 슬롯, LOD, 섹션, 메시 통계, Physics Asset을 분류별로 표시합니다.
class FSkeletalMeshInfoPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FSkeletalMeshInfoPanel(FSkeletalMeshEditor& InEditor);

protected:
	void DrawContent() override;

private:
	char SearchBuffer[128] = {};
};
