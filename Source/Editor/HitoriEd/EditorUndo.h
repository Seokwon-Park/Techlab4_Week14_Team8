#pragma once

#include "Containers/Array.h"
#include "Core/NameTypes.h"
#include "Core/Types.h"
#include "Math/Transform.h"

class UEditorEngine;
class UWorld;
class AActor;
class UActorComponent;
class USceneComponent;
class UObject;

// 단일 편집 동작의 Undo/Redo 계약. 생포인터 대신 이름으로 대상을 찾아 삭제·복원 후에도 안전하다.
class IEditorUndoCommand
{
public:
	virtual ~IEditorUndoCommand() = default;
	virtual const char* GetDescription() const = 0;
	virtual void Undo(UEditorEngine& Editor) = 0;
	virtual void Redo(UEditorEngine& Editor) = 0;
};

// 에디터 월드의 커맨드 기반 Undo/Redo 히스토리 (SavedStateId 기반 Dirty 추적 포함).
class FEditorUndoHistory
{
public:
	static constexpr int32 MaxHistorySteps = 100;

	// Undo/Redo 실행 중 파생 호출이 새 커맨드를 쌓지 않도록 막는 RAII 가드.
	struct FScopedSuppress
	{
		explicit FScopedSuppress(FEditorUndoHistory& InHistory)
			: History(InHistory)
			, bPrev(InHistory.bSuppressRecording)
		{
			History.bSuppressRecording = true;
		}
		~FScopedSuppress()
		{
			History.bSuppressRecording = bPrev;
		}
		FEditorUndoHistory& History;
		bool bPrev;
	};

	void SetRecordingEnabled(bool bEnabled) { bRecordingEnabled = bEnabled; }
	bool CanRecord() const { return bRecordingEnabled && !bSuppressRecording; }
	bool IsRecordingSuppressed() const { return bSuppressRecording; }

	void Push(TUniquePtr<IEditorUndoCommand> Command);

	bool CanUndo() const { return !UndoStack.IsEmpty(); }
	bool CanRedo() const { return !RedoStack.IsEmpty(); }
	const char* GetUndoDescription() const;
	const char* GetRedoDescription() const;

	void Undo(UEditorEngine& Editor);
	void Redo(UEditorEngine& Editor);
	void Clear();

	// 저장 이후 씬이 바뀌었는지. Undo로 저장 시점까지 되돌리면 다시 깨끗한 상태가 된다.
	bool IsDirty() const { return bForceDirty || GetCurrentStateId() != SavedStateId; }
	void MarkSaved() { SavedStateId = GetCurrentStateId(); bForceDirty = false; }
	void MarkDirty() { bForceDirty = true; }

private:
	uint64 GetCurrentStateId() const { return UndoIds.IsEmpty() ? 0 : UndoIds.Last(); }

	TArray<TUniquePtr<IEditorUndoCommand>> UndoStack;
	TArray<TUniquePtr<IEditorUndoCommand>> RedoStack;
	bool bRecordingEnabled = true;
	bool bSuppressRecording = false;

	TArray<uint64> UndoIds;
	TArray<uint64> RedoIds;
	uint64 NextStateId = 1;
	uint64 SavedStateId = 0;
	bool bForceDirty = false;
};

namespace EditorUndoUtils
{
	AActor* FindActorByName(UWorld* World, FName ActorName);
	UActorComponent* FindComponentByNames(UWorld* World, FName ActorName, FName ComponentName);

	// 기즈모/트랜스폼 편집용 경량 커맨드 (월드 전체 직렬화 없이 O(1) Undo/Redo)
	TUniquePtr<IEditorUndoCommand> MakeTransformCommand(
		FName ActorName,
		FName ComponentName,
		const FTransform& OldTransform,
		const FTransform& NewTransform,
		const char* Description = "Transform");

	// 범용 스냅샷 폴백 커맨드 (복합 편집·삭제·복제·컴포넌트 추가 등)
	TUniquePtr<IEditorUndoCommand> MakeSnapshotCommand(
		TArray<uint8> BeforeSnapshot,
		TArray<uint8> AfterSnapshot,
		const char* Description = "Level Edit");
}
