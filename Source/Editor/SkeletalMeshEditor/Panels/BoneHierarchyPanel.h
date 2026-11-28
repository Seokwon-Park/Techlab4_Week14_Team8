#pragma once

#include "SkeletalMeshEditorPanel.h"
#include "Containers/Array.h"

class FSkeletalPreviewEditor;

// UE5 Skeleton Tree: 검색 칸과 Name 머리줄 아래에 스켈레탈 메시의 본 계층을 표시하는 패널입니다.
class FBoneHierarchyPanel : public FSkeletalMeshEditorPanel
{
public:
	explicit FBoneHierarchyPanel(FSkeletalMeshEditor& InEditor);

	// 본 계층 트리를 그린다. 애니메이션 뷰어의 Skeleton Tree도 같은 함수를 쓴다.
	// Filter가 있으면 이름이 맞는 본과 그 조상만 펼쳐서 보여 준다.
	static void DrawTree(FSkeletalPreviewEditor& InEditor, const char* Filter = nullptr);

protected:
	void DrawContent() override;

private:
	static void DrawBoneHierarchy(FSkeletalPreviewEditor& InEditor, int32 BoneIndex, const TArray<uint8>* Visible);

	char SearchBuffer[128] = {};
};
