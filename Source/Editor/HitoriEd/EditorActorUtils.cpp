#include "EnginePCH.h"
#include "Editor/HitoriEd/EditorActorUtils.h"

#include "Core/EngineLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"
#include "Editor/HitoriEd/LevelUndoHistory.h"

namespace
{
	FString TrimSpaces(const FString& Text)
	{
		const size_t First = Text.find_first_not_of(" \t\r\n");
		if (First == FString::npos)
			return FString();
		const size_t Last = Text.find_last_not_of(" \t\r\n");
		return Text.substr(First, Last - First + 1);
	}
}

bool FEditorActorUtils::RenameActor(AActor* Actor, const FString& NewName)
{
	if (!Actor)
		return false;

	const FString Name = TrimSpaces(NewName);
	if (Name.empty())
	{
		HTR_LOG(Warning, "Rename: actor name cannot be empty");
		return false;
	}
	if (Name == Actor->GetName())
		return true;

	UWorld* World = Actor->GetWorld();
	if (!World || World->GetWorldType() != EWorldType::Editor)
	{
		HTR_LOG(Warning, "Rename: only actors in the editor world can be renamed");
		return false;
	}

	// 같은 Outer(레벨) 안에서 이름이 겹치면 이름으로 찾는 코드가 엉뚱한 액터를 집는다.
	const FName NewFName(Name);
	UObject* Existing = StaticFindObjectFast(nullptr, Actor->GetOuter(), NewFName);
	if (Existing && Existing != Actor)
	{
		HTR_LOG(Warning, "Rename: an object named '{}' already exists in this level", Name);
		return false;
	}

	HTR_LOG(Info, "Renamed actor {} -> {}", Actor->GetName(), Name);
	Actor->SetName(NewFName);
	FLevelUndoHistory::MarkDirty();
	return true;
}

namespace
{
	bool IsEditorActor(const AActor* Actor)
	{
		return Actor && Actor->GetWorld() && Actor->GetWorld()->GetWorldType() == EWorldType::Editor;
	}
}

bool FEditorActorUtils::IsSelfOrAttachedDescendant(const AActor* Candidate, const AActor* Actor)
{
	for (const AActor* It = Candidate; It; It = It->GetAttachParentActor())
		if (It == Actor) return true;
	return false;
}

bool FEditorActorUtils::AttachActor(AActor* Child, AActor* Parent)
{
	if (!IsEditorActor(Child) || !IsEditorActor(Parent) || Child->GetWorld() != Parent->GetWorld())
		return false;
	if (Child->GetAttachParentActor() == Parent)
		return true;
	if (IsSelfOrAttachedDescendant(Parent, Child))
	{
		HTR_LOG(Warning, "Attach: {} cannot be attached to itself or to its own child {}", Child->GetName(), Parent->GetName());
		return false;
	}
	if (!Child->AttachToActor(Parent, EAttachmentRule::KeepWorld))
	{
		HTR_LOG(Warning, "Attach: failed to attach {} to {}", Child->GetName(), Parent->GetName());
		return false;
	}
	Child->SetFolderPath(Parent->GetFolderPath());
	HTR_LOG(Info, "Attached {} to {}", Child->GetName(), Parent->GetName());
	FLevelUndoHistory::MarkDirty();
	return true;
}

bool FEditorActorUtils::DetachActor(AActor* Child)
{
	if (!IsEditorActor(Child) || !Child->GetAttachParentActor())
		return false;
	HTR_LOG(Info, "Detached {} from {}", Child->GetName(), Child->GetAttachParentActor()->GetName());
	Child->DetachFromActor(EAttachmentRule::KeepWorld);
	FLevelUndoHistory::MarkDirty();
	return true;
}
