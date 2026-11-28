#pragma once

#include "SkeletalMeshEditorPanel.h"

// 프리뷰 렌더 결과를 표시하고 카메라 입력과 화면 크기를 Editor에 전달합니다.
class FSkeletalMeshViewportPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FSkeletalMeshViewportPanel(FSkeletalMeshEditor& InEditor);

protected:
	void DrawContent() override;

private:
	// UE5처럼 왼쪽 위에 프리뷰 상태와 메시 통계, 오른쪽 위에 단축키를 쓴다.
	void DrawStatsOverlay(const ImVec2& ViewMin, const ImVec2& ViewMax);
	// 뷰포트 아래 재생 막대 (Cloth 프리뷰 재생·정지)
	void DrawTransportBar(const ImVec2& BarMin, const ImVec2& BarMax);
};
