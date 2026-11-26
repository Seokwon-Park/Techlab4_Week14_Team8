#pragma once

#include "Containers/Array.h"
#include "Core/Types.h"

class UWorld;

// 레벨 에디터의 실행 취소 기록. 편집이 끝날 때마다 에디터 월드 전체를 직렬화해 한 단계로 쌓는다.
// UI 에디터, 애니메이션 시퀀스 에디터와 같은 스냅샷 방식이다. UE의 UTransBuffer와 같은 자리지만,
// UE는 바뀐 오브젝트만 기록하고 이 엔진은 월드를 통째로 기록한다.
//
// 편집하는 쪽은 MarkDirty만 부른다. 에디터 엔진이 프레임 끝에 조작(드래그, 입력)이 모두 끝났는지 보고
// Commit을 불러 바뀐 것이 있으면 한 단계를 만든다. 바뀐 것이 없으면 단계를 만들지 않으므로
// 편집이 있었을지도 모르는 곳(패널 클릭 등)에서 넉넉히 불러도 된다.
class FLevelUndoHistory
{
public:
	// 레벨을 바꿨거나 바꿨을 수 있다고 알린다. 다음 Commit이 실제로 바뀌었는지 확인한다.
	static void MarkDirty() { bDirty = true; }
	static bool IsDirty() { return bDirty; }

	// 패널의 ImGui::End() 바로 앞에서 부른다. 이 창(자식 창, 팝업 포함)에서 값을 바꿨거나
	// 마우스를 놓았거나(버튼, 드래그 앤 드롭) 하면 MarkDirty한다.
	static void NotePanelInteraction();

	// 기록을 모두 지우고 지금 월드를 기준 상태로 삼는다. 레벨을 새로 열거나 만들 때 부른다.
	// 월드가 nullptr이면 기준 없이 비워 두고, 다음 Commit이 기준을 잡는다.
	void Reset(UWorld* World);

	// 표시된 편집이 있으면 지금 월드를 기준 상태와 비교해 다르면 한 단계를 쌓는다. 쌓았으면 true.
	bool Commit(UWorld* World);

	// 기준 상태를 바꿀 스냅샷을 돌려준다. 꺼낼 것이 없으면 nullptr. 반환값은 다음 호출까지 유효하다.
	const TArray<uint8>* Undo();
	const TArray<uint8>* Redo();

	bool CanUndo() const { return !UndoStack.IsEmpty(); }
	bool CanRedo() const { return !RedoStack.IsEmpty(); }
	bool HasBaseline() const { return bHasBaseline; }

private:
	void TrimToBudget();
	static int64 SizeOf(const TArray<TArray<uint8>>& Stack);

	// 월드 하나가 수 MB가 될 수 있어 개수와 전체 크기를 함께 제한한다. 넘치면 가장 오래된 단계를 버린다.
	static constexpr int32 MaxUndoSteps = 64;
	static constexpr int64 MaxTotalBytes = 512ll * 1024 * 1024;

	TArray<TArray<uint8>> UndoStack;
	TArray<TArray<uint8>> RedoStack;
	// 마지막으로 확정된 월드 상태. Undo하면 이것이 Redo 스택으로 간다.
	TArray<uint8> Committed;
	bool bHasBaseline = false;

	static inline bool bDirty = false;
};
