#include "EnginePCH.h"
#include "Level.h"
#include "World.h"
#include "Engine/Blueprint.h"

// delete Actor가 소멸자를 호출하려면 AActor의 완전한 정의가 필요하다.
// 전방 선언만으로는 C4150(소멸자 미호출)이 되어 액터가 GUObjectArray에 남는다.
#include "GameFramework/Actor.h"
#include "Components/ExponentialHeightFogComponent.h"

void ULevel::AddActor(AActor* Actor)
{
	if (!Actor)
		return;

	Actors.Add(Actor);
}

void ULevel::ClearActors()
{
	for (AActor* Actor : Actors)
	{
		if (!Actor)
			continue;

		delete Actor;
	}
	Actors.Reset();
}

void ULevel::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();

	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		int32 Num = Actors.Num();
		FStructuredArchive::FArray ActorArray = Record.EnterArray("Actors", Num);
		if (Ar.IsLoading())
			Actors.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			UObject* Obj = Actors[i];
			ActorArray.EnterElement() << Obj;
			Actors[i] = static_cast<AActor*>(Obj);
		}

		UObject* WorldObj = OwningWorld;
		Record << SA_VALUE("OwningWorld", WorldObj);
		OwningWorld = static_cast<UWorld*>(WorldObj);
		return;
	}

	if (Ar.IsSaving())
	{
		// 개수를 먼저 알아야 EnterArray를 할 수 있으므로 저장할 액터부터 고른다
		TArray<AActor*> SavedActors;
		for (AActor* Actor : Actors)
		{
			if (Actor && !Actor->HasAnyFlags(EObjectFlags::RF_Transient))
				SavedActors.Add(Actor);
		}

		int32 Num = SavedActors.Num();
		FStructuredArchive::FArray ActorArray = Record.EnterArray("Actors", Num);
		for (AActor* Actor : SavedActors)
		{
			FStructuredArchive::FRecord ActorRecord = ActorArray.EnterElement().EnterRecord();
			FString ClassName = Actor->GetClass()->Name;
			FString ActorName = Actor->GetName();
			ActorRecord << SA_VALUE("Class", ClassName) << SA_VALUE("Name", ActorName);
			// BP 액터는 C++ 조상도 남긴다. BP 에셋을 지우거나 이름을 바꿔도 그 클래스로라도 열 수 있다.
			if (FBlueprint::IsBlueprintClass(Actor->GetClass()))
			{
				FString NativeClassName = FBlueprint::GetNativeClass(Actor->GetClass())->Name;
				ActorRecord << SA_VALUE("NativeClass", NativeClassName);
				// BP 액터는 BP 기본값과 다른 값만 쓴다. 그래야 BP를 고쳤을 때 이 액터도 따라간다.
				FString Delta;
				if (FBlueprint::MakeInstanceDelta(Actor, Delta))
				{
					ActorRecord << SA_VALUE("BlueprintDelta", Delta);
					continue;
				}
			}
			Actor->Serialize(ActorRecord);
		}
		return;
	}

	// 불러오기: 월드를 비우는 건 호출하는 쪽 책임 (PIE 복제는 새 월드라 비울 필요가 없다)
	assert(OwningWorld);

	int32 Num = 0;
	FStructuredArchive::FArray ActorArray = Record.EnterArray("Actors", Num);
	for (int32 i = 0; i < Num; ++i)
	{
		FStructuredArchive::FRecord ActorRecord = ActorArray.EnterElement().EnterRecord();

		FString ClassName;
		FString ActorName;
		ActorRecord << SA_VALUE("Class", ClassName) << SA_VALUE("Name", ActorName);   // 없으면 빈 문자열

		UClass* Class = FindClass(ClassName);
		if (!Class)
		{
			FString NativeClassName;
			ActorRecord << SA_VALUE("NativeClass", NativeClassName);   // BP 액터만 있다
			if (!NativeClassName.empty() && (Class = FindClass(NativeClassName)))
				HTR_LOG(Warning, "Load: blueprint '{}' not found, using {}", ClassName, NativeClassName);
		}
		if (!Class || !Class->IsChildOf(AActor::StaticClass()))
		{
			HTR_LOG(Warning, "Load: unknown actor class '{}', skipped", ClassName);
			continue;
		}
		// 파일 안에 같은 이름이 또 있으면 그대로 생성할 때 앞서 만든 액터를 덮어쓰게 되므로 새 이름을 받는다
		FName Name = ActorName.empty() ? NAME_None : FName(ActorName);
		if (!Name.IsNone() && StaticFindObjectFast(nullptr, this, Name))
		{
			HTR_LOG(Warning, "Load: duplicate actor name '{}', renamed", ActorName);
			Name = NAME_None;
		}
		AActor* Actor = OwningWorld->SpawnActor(Class, Name);

		if (Actor)
		{
			// BP 액터의 차이 기록이 있으면 (SpawnActor가 적용한) 지금 BP 기본값 위에 얹는다.
			// 없으면 예전 형식(모든 값)이거나 C++ 액터다. BP가 사라져 C++ 조상으로 만든 경우에는 차이를 적용할 기본값이 없다.
			FString Delta;
			bool bHasDelta = false;
			if (TOptional<FStructuredArchive::FSlot> DeltaField = ActorRecord.TryEnterField("BlueprintDelta", false); DeltaField.IsSet())
			{
				DeltaField.GetValue() << Delta;
				bHasDelta = true;
			}
			if (bHasDelta && FBlueprint::IsBlueprintClass(Actor->GetClass()))
				FBlueprint::ApplyInstanceDelta(Actor, Delta);
			else if (bHasDelta)
				HTR_LOG(Warning, "Load: {} was a blueprint instance; its overrides could not be applied", ActorName);
			else
				Actor->Serialize(ActorRecord);

			// SpawnActor가 기본값(원점, bSimulatePhysics=false)으로 등록할 때 만든 바디를 저장된 값으로 다시 만든다.
			// UE는 로드가 끝난 뒤에 컴포넌트를 등록하므로 이 문제가 없다
			for (UActorComponent* Component : Actor->GetComponents())
				if (Component) Component->RecreatePhysicsState();
		}
	}

	// 다른 액터에 붙어 있던 액터는 부모 액터가 만들어진 뒤에야 붙일 수 있다.
	for (AActor* Actor : Actors)
	{
		if (Actor)
			Actor->ResolvePendingActorAttachments();
	}
}
