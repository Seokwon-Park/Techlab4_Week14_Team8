#pragma once

#include "SkeletalMeshEditorPanel.h"

// 선택한 본의 프리뷰 로컬 변환을 UE5 Details처럼 이름/값 두 칸으로 편집합니다.
class FSkeletalMeshDetailsPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FSkeletalMeshDetailsPanel(FSkeletalMeshEditor& InEditor);

protected:
	void DrawContent() override;
};
