#include "EnginePCH.h"
#include "Editor/HitoriEd/LevelUndoHistory.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "Serialization/JsonArchive.h"

void FLevelUndoHistory::NotePanelInteraction()
{
	// ChildWindows는 이 창에서 연 팝업(콤보 목록, 컴포넌트 추가 메뉴)까지 포함한다.
	const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
	const bool bHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

	// 값을 바꾼 위젯(드래그, 입력, 체크박스, 콤보)은 그 프레임에 이 표시를 남긴다.
	const ImGuiContext& G = *ImGui::GetCurrentContext();
	if (bFocused && G.ActiveIdHasBeenEditedThisFrame)
		MarkDirty();

	// 버튼, 메뉴 항목, 드래그 앤 드롭은 위의 표시를 남기지 않으므로 마우스를 놓은 순간으로 잡는다.
	if ((bFocused || bHovered) && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		MarkDirty();
}

void FLevelUndoHistory::Reset(UWorld* World)
{
	UndoStack.Empty();
	RedoStack.Empty();
	Committed.Empty();
	bHasBaseline = World && FJsonArchive::SaveWorldToBytes(World, Committed);
	bDirty = false;
}

bool FLevelUndoHistory::Commit(UWorld* World)
{
	if (!World)
		return false;

	if (!bHasBaseline)
	{
		// 기준이 없으면 지금 상태를 기준으로 삼는다. 이번 편집은 되돌릴 수 없지만 다음부터는 된다.
		Reset(World);
		return false;
	}

	if (!bDirty)
		return false;
	bDirty = false;

	TArray<uint8> Current;
	if (!FJsonArchive::SaveWorldToBytes(World, Current) || Current == Committed)
		return false;

	UndoStack.Add(std::move(Committed));
	Committed = std::move(Current);
	RedoStack.Empty();
	TrimToBudget();
	return true;
}

const TArray<uint8>* FLevelUndoHistory::Undo()
{
	if (UndoStack.IsEmpty())
		return nullptr;

	RedoStack.Add(std::move(Committed));
	Committed = std::move(UndoStack[UndoStack.Num() - 1]);
	UndoStack.RemoveLast();
	bDirty = false;
	return &Committed;
}

const TArray<uint8>* FLevelUndoHistory::Redo()
{
	if (RedoStack.IsEmpty())
		return nullptr;

	UndoStack.Add(std::move(Committed));
	Committed = std::move(RedoStack[RedoStack.Num() - 1]);
	RedoStack.RemoveLast();
	bDirty = false;
	return &Committed;
}

int64 FLevelUndoHistory::SizeOf(const TArray<TArray<uint8>>& Stack)
{
	int64 Total = 0;
	for (const TArray<uint8>& Snapshot : Stack)
		Total += Snapshot.Num();
	return Total;
}

void FLevelUndoHistory::TrimToBudget()
{
	// Commit 직후에는 Redo가 비어 있으므로 Undo 스택만 줄이면 된다.
	while (!UndoStack.IsEmpty() &&
		(UndoStack.Num() > MaxUndoSteps || SizeOf(UndoStack) + Committed.Num() > MaxTotalBytes))
	{
		UndoStack.RemoveAt(0);
	}
}
