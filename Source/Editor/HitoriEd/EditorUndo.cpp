#include "EnginePCH.h"
#include "Editor/HitoriEd/EditorUndo.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"

namespace
{
	class FTransformUndoCommand final : public IEditorUndoCommand
	{
	public:
		FTransformUndoCommand(FName InActorName, FName InComponentName,
			const FTransform& InOldTransform, const FTransform& InNewTransform, const char* InDesc)
			: ActorName(InActorName)
			, ComponentName(InComponentName)
			, OldTransform(InOldTransform)
			, NewTransform(InNewTransform)
			, Description(InDesc ? InDesc : "Transform")
		{}

		const char* GetDescription() const override { return Description.c_str(); }

		void Undo(UEditorEngine& Editor) override { Apply(Editor, OldTransform); }
		void Redo(UEditorEngine& Editor) override { Apply(Editor, NewTransform); }

	private:
		void Apply(UEditorEngine& Editor, const FTransform& Transform)
		{
			UWorld* World = Editor.GetEditorWorld();
			if (!World) return;
			UActorComponent* Comp = EditorUndoUtils::FindComponentByNames(World, ActorName, ComponentName);
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
			{
				SceneComp->SetTransform(Transform);
			}
		}

		FName ActorName;
		FName ComponentName;
		FTransform OldTransform;
		FTransform NewTransform;
		FString Description;
	};

	class FSnapshotUndoCommand final : public IEditorUndoCommand
	{
	public:
		FSnapshotUndoCommand(TArray<uint8> InBefore, TArray<uint8> InAfter, const char* InDesc)
			: Before(std::move(InBefore))
			, After(std::move(InAfter))
			, Description(InDesc ? InDesc : "Level Edit")
		{}

		const char* GetDescription() const override { return Description.c_str(); }

		void Undo(UEditorEngine& Editor) override { Editor.ApplyLevelSnapshot(Before); }
		void Redo(UEditorEngine& Editor) override { Editor.ApplyLevelSnapshot(After); }

	private:
		TArray<uint8> Before;
		TArray<uint8> After;
		FString Description;
	};
}

void FEditorUndoHistory::Push(TUniquePtr<IEditorUndoCommand> Command)
{
	if (!Command || !CanRecord())
		return;

	UndoStack.Add(std::move(Command));
	UndoIds.Add(NextStateId++);
	RedoStack.Empty();
	RedoIds.Empty();

	while (UndoStack.Num() > MaxHistorySteps)
	{
		if (!UndoIds.IsEmpty() && UndoIds[0] == SavedStateId)
			bForceDirty = true;
		UndoStack.RemoveAt(0);
		UndoIds.RemoveAt(0);
	}
}

const char* FEditorUndoHistory::GetUndoDescription() const
{
	return UndoStack.IsEmpty() ? nullptr : UndoStack.Last()->GetDescription();
}

const char* FEditorUndoHistory::GetRedoDescription() const
{
	return RedoStack.IsEmpty() ? nullptr : RedoStack.Last()->GetDescription();
}

void FEditorUndoHistory::Undo(UEditorEngine& Editor)
{
	if (UndoStack.IsEmpty())
		return;

	TUniquePtr<IEditorUndoCommand> Command = std::move(UndoStack.Last());
	UndoStack.RemoveLast();
	const uint64 Id = UndoIds.Last();
	UndoIds.RemoveLast();

	{
		FScopedSuppress Suppress(*this);
		Command->Undo(Editor);
	}

	RedoStack.Add(std::move(Command));
	RedoIds.Add(Id);
}

void FEditorUndoHistory::Redo(UEditorEngine& Editor)
{
	if (RedoStack.IsEmpty())
		return;

	TUniquePtr<IEditorUndoCommand> Command = std::move(RedoStack.Last());
	RedoStack.RemoveLast();
	const uint64 Id = RedoIds.Last();
	RedoIds.RemoveLast();

	{
		FScopedSuppress Suppress(*this);
		Command->Redo(Editor);
	}

	UndoStack.Add(std::move(Command));
	UndoIds.Add(Id);
}

void FEditorUndoHistory::Clear()
{
	UndoStack.Empty();
	RedoStack.Empty();
	UndoIds.Empty();
	RedoIds.Empty();
	SavedStateId = 0;
	bForceDirty = false;
}

namespace EditorUndoUtils
{
	AActor* FindActorByName(UWorld* World, FName ActorName)
	{
		if (!World || !World->GetPersistentLevel() || ActorName == NAME_None)
			return nullptr;
		const FString Target = ActorName.ToString();
		for (AActor* Actor : World->GetPersistentLevel()->GetActors())
		{
			if (Actor && Actor->GetName() == Target)
				return Actor;
		}
		return nullptr;
	}

	UActorComponent* FindComponentByNames(UWorld* World, FName ActorName, FName ComponentName)
	{
		AActor* Actor = FindActorByName(World, ActorName);
		if (!Actor)
			return nullptr;
		if (ComponentName == NAME_None)
			return Actor->GetRootComponent();
		const FString Target = ComponentName.ToString();
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (Comp && Comp->GetName() == Target)
				return Comp;
		}
		return Actor->GetRootComponent();
	}

	TUniquePtr<IEditorUndoCommand> MakeTransformCommand(
		FName ActorName,
		FName ComponentName,
		const FTransform& OldTransform,
		const FTransform& NewTransform,
		const char* Description)
	{
		return MakeUnique<FTransformUndoCommand>(ActorName, ComponentName, OldTransform, NewTransform, Description);
	}

	TUniquePtr<IEditorUndoCommand> MakeSnapshotCommand(
		TArray<uint8> BeforeSnapshot,
		TArray<uint8> AfterSnapshot,
		const char* Description)
	{
		return MakeUnique<FSnapshotUndoCommand>(std::move(BeforeSnapshot), std::move(AfterSnapshot), Description);
	}
}
