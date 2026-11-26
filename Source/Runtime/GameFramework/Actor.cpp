#include "EnginePCH.h"
#include "Actor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Components/SceneComponent.h"
#include "Components/ParticleSubUVComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Core/EngineLog.h"
#include "UObject/Object.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGraph.h"
#include "Script/LuaScriptInstance.h"
#include "Script/LuaBindings.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
enum class EAttachmentRule;


AActor::AActor()
{
	PrimaryActorTick.Target = this;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>("DefaultSceneRoot"));
}

AActor::~AActor()
{
	TArray<UActorComponent*> ToDelete = Components;
	Components.Reset();
	RootComponent = nullptr;

	for (UActorComponent* Component : ToDelete)
	{
		delete Component;
	}
}

void AActor::SetActorEnableCollision(bool bEnabled)
{
	bActorEnableCollision = bEnabled;
	// 차체와 본별 바디를 포함한 소유 컴포넌트의 충돌 상태 갱신
	for (UActorComponent* Component : Components)
	{
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
		{
			Primitive->UpdateCollisionSettings();
		}
	}
}

void AActor::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "bActorEnableCollision")
	{
		SetActorEnableCollision(bActorEnableCollision);
	}
}

void AActor::BeginPlay()
{
	if (bHasBegunPlay || !World || !World->HasBegunPlay()) return;
	RegisterAllComponents();
	bHasBegunPlay = true;
	for (UActorComponent* Component : Components)
		if (Component) Component->BeginPlayComponent();

	if (!ScriptPath.empty())
	{
		Script = new FLuaScriptInstance();
		Script->Load(this, ScriptPath);
		PrimaryActorTick.bCanEverTick = true;
	}

	// Blueprint 그래프에 Event Tick이나 Delay가 있으면 매 프레임 불러야 한다.
	const bool bBlueprintGraph = FBlueprint::IsBlueprintClass(GetClass());
	if (bBlueprintGraph && FBlueprintGraphRuntime::NeedsTick(GetClass()))
		PrimaryActorTick.bCanEverTick = true;

	RegisterAllActorTickFunctions(true);

	if (Script)
		Script->Call("BeginPlay");
	if (bBlueprintGraph)
		FBlueprintGraphRuntime::OnBeginPlay(this);
}

void AActor::TickActor(float DeltaTime)
{
	Tick(DeltaTime);
	if (Script)
		Script->Tick(DeltaTime);
	if (FBlueprint::IsBlueprintClass(GetClass()))
		FBlueprintGraphRuntime::OnTick(this, DeltaTime);
}

void AActor::EndPlay()
{
	if (FBlueprint::IsBlueprintClass(GetClass()))
		FBlueprintGraphRuntime::OnEndPlay(this);
	if (!Script)
		return;

	Script->Call("EndPlay");
	if (World) World->GetCoroutineScheduler().StopAll(this);
	FLuaBindings::ReleaseCallbacks(this);       // 델리게이트에 걸린 Lua 함수 해제 (단계 8 안전장치 참고)
	delete Script;
	Script = nullptr;
}

void AActor::RegisterAllActorTickFunctions(bool bRegister)
{
	if (bRegister && !World)
		return;

	// bCanEverTick이 꺼진 함수는 등록하지 않으므로 정적 메시 액터는 매 프레임 순회 대상에서 빠진다.
	auto Apply = [&](FTickFunction& Function)
		{
			if (bRegister)
				Function.RegisterTickFunction(World->GetTickTaskManager());
			else
				Function.UnRegisterTickFunction();
		};

	Apply(PrimaryActorTick);
	for (UActorComponent* Component : Components)
	{
		if (Component)
			Component->RegisterComponentTickFunctions(bRegister);
	}
}

void AActor::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);   // "Properties"

	FArchive& Ar = Record.GetUnderlyingArchive();

	if (Ar.HasAnyPortFlags(EPropertyPortFlags::PPF_Duplicate))
	{
		int32 Num = Components.Num();
		FStructuredArchive::FArray ActorArray = Record.EnterArray("Components", Num);
		if (Ar.IsLoading())
			Components.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			UObject* Obj = Components[i];
			ActorArray.EnterElement() << Obj;
			Components[i] = static_cast<UActorComponent*>(Obj);
		}

		UObject* WorldObj = World;
		UObject* LevelObj = Level;
		UObject* RootObj = RootComponent;
		Record << SA_VALUE("OwningWorld", WorldObj);
		Record << SA_VALUE("OwningLevel", LevelObj);
		Record << SA_VALUE("RootComponent", RootObj);
		World = static_cast<UWorld*>(WorldObj);
		Level = static_cast<ULevel*>(LevelObj);
		RootComponent = static_cast<USceneComponent*>(RootObj);
		return;
	}

	TArray<UActorComponent*> SavedComponents;
	if (Ar.IsSaving())
	{
		for (UActorComponent* Component : Components)
		{
			if (Component)
			{
				SavedComponents.Add(Component);
			}

		}
	}

	struct FPendingAttachment
	{
		USceneComponent* Child;
		FString ParentName;
	};
	TArray<FPendingAttachment> PendingAttachments;

	int32 Num = SavedComponents.Num();
	FStructuredArchive::FArray ComponentArray = Record.EnterArray("Components", Num);
	for (int32 i = 0; i < Num; ++i)
	{
		FStructuredArchive::FRecord ComponentRecord = ComponentArray.EnterElement().EnterRecord();
		FString ParentName;

		if (Ar.IsSaving())
		{
			UActorComponent* Component = SavedComponents[i];
			FString Name = Component->GetName();
			FString ClassName = Component->GetClass()->Name;
			// 부모가 다른 액터의 컴포넌트면 그 액터 이름도 남긴다. 같은 액터면 ParentActor는 비어 있다.
			FString ParentActorName;
			if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
			{
				if (USceneComponent* Parent = SceneComponent->GetAttachParent())
				{
					ParentName = Parent->GetName();
					if (Parent->GetOwner() && Parent->GetOwner() != this)
						ParentActorName = Parent->GetOwner()->GetName();
				}
			}
			ComponentRecord << SA_VALUE("Name", Name) << SA_VALUE("Class", ClassName) << SA_VALUE("Parent", ParentName)
				<< SA_VALUE("ParentActor", ParentActorName);
			Component->Serialize(ComponentRecord);
			continue;
		}

		FString Name, ClassName, ParentActorName;
		ComponentRecord << SA_VALUE("Name", Name) << SA_VALUE("Class", ClassName) << SA_VALUE("Parent", ParentName)
			<< SA_VALUE("ParentActor", ParentActorName);   // 예전 씬 파일에는 없어서 빈 문자열이 된다

		UActorComponent* Component = FindComponentByName(FName(Name));
		if (!Component)
		{
			UClass* ComponentClass = FindClass(ClassName);

			if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
			{
				HTR_LOG(Warning, "Load: unknown component class '{}'", ClassName);
				continue;
			}

			Component = AddComponent(ComponentClass, FName(Name));
		}

		if (!Component || Component->GetClass()->Name != ClassName)
		{
			HTR_LOG(Warning, "Load: component {} ({}) failed", Name, ClassName);
			continue;
		}

		// AddComponent는 SceneComponent를 기본 Root에 자동 부착한다. 저장 데이터에서
		// Parent가 비어 있으면 이 자동 부착을 먼저 풀어 Transform을 독립 월드 Transform으로 읽는다.
		// 다른 액터에 붙어 있던 컴포넌트도 같다. 그 부모는 레벨을 다 읽은 뒤에 붙인다.
		const bool bParentInOtherActor = !ParentActorName.empty();
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
			SceneComponent && (ParentName.empty() || bParentInOtherActor) && SceneComponent != RootComponent)
		{
			SceneComponent->DetachFromParent(EAttachmentRule::KeepRelative);
		}

		Component->Serialize(ComponentRecord);
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			if (bParentInOtherActor)
			{
				PendingActorAttachments.Add({ SceneComponent, ParentActorName, ParentName });
			}
			else if (!ParentName.empty())
			{
				PendingAttachments.Add({ SceneComponent, ParentName });
			}
		}
	}

	for (const FPendingAttachment& Attachment : PendingAttachments)
	{
		USceneComponent* ParentComponent = Cast<USceneComponent>(FindComponentByName(FName(Attachment.ParentName)));
		if (ParentComponent)
		{
			Attachment.Child->SetupAttachment(ParentComponent);
		}
	}
}


void AActor::RemoveOwnedComponent(UActorComponent* Component)
{
	for (uint32 i = 0; i < Components.Num(); ++i)
	{
		if (Components[i] == Component)
		{
			Components.RemoveAt(i, 1);
			break;
		}
	}

	if (RootComponent == Component)
	{
		RootComponent = nullptr;
	}
}

FVector AActor::GetActorLocation() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldLocation();
	}
	return FVector::ZeroVector;
}

FRotator AActor::GetActorRotation() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldRotation();
	}
	return FRotator();
}

FVector AActor::GetActorScale3D() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldScale3D();
	}
	return FVector::OneVector;
}

FQuat AActor::GetActorQuat() const
{
	if (RootComponent)
	{
		return RootComponent->GetWorldRotation().Quaternion();
	}
	return FQuat::Identity();
}

FTransform AActor::GetActorTransform() const
{
	if (RootComponent)
	{
		return FTransform(
			RootComponent->GetWorldRotation(),
			RootComponent->GetWorldLocation(),
			RootComponent->GetWorldScale3D()
		);

		// return FTransform(RootComponent->GetWorldMatrix());
	}
	return FTransform::Identity;
}

bool AActor::Destroy()
{
	if (!World)
		return false;

	return World->DestroyActor(this);
}

bool AActor::AttachToActor(AActor* ParentActor, EAttachmentRule Rule)
{
	USceneComponent* Root = RootComponent;
	USceneComponent* ParentRoot = ParentActor ? ParentActor->GetRootComponent() : nullptr;
	if (!Root || !ParentRoot || ParentActor == this)
		return false;

	// SetupAttachment가 순환이면 아무것도 하지 않으므로, 붙었는지는 결과로 확인한다.
	Root->SetupAttachment(ParentRoot, Rule);
	return Root->GetAttachParent() == ParentRoot;
}

void AActor::DetachFromActor(EAttachmentRule Rule)
{
	if (GetAttachParentActor())
		RootComponent->DetachFromParent(Rule);
}

AActor* AActor::GetAttachParentActor() const
{
	const USceneComponent* Parent = RootComponent ? RootComponent->GetAttachParent() : nullptr;
	AActor* Owner = Parent ? Parent->GetOwner() : nullptr;
	return Owner != this ? Owner : nullptr;
}

void AActor::GetAttachedActors(TArray<AActor*>& OutActors) const
{
	for (UActorComponent* Component : Components)
	{
		const USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		if (!SceneComponent) continue;
		for (USceneComponent* Child : SceneComponent->GetAttachChildren())
		{
			AActor* ChildOwner = Child ? Child->GetOwner() : nullptr;
			if (ChildOwner && ChildOwner != this && ChildOwner->GetRootComponent() == Child &&
				OutActors.Find(ChildOwner) == INDEX_NONE)
				OutActors.Add(ChildOwner);
		}
	}
}

void AActor::ResolvePendingActorAttachments()
{
	if (PendingActorAttachments.IsEmpty())
		return;

	for (const FPendingActorAttachment& Pending : PendingActorAttachments)
	{
		AActor* ParentActor = nullptr;
		if (Level)
		{
			for (AActor* Candidate : Level->GetActors())
			{
				if (Candidate && Candidate != this && Candidate->GetName() == Pending.ParentActorName)
				{
					ParentActor = Candidate;
					break;
				}
			}
		}
		USceneComponent* Parent = ParentActor
			? Cast<USceneComponent>(ParentActor->FindComponentByName(FName(Pending.ParentComponentName)))
			: nullptr;
		if (!Parent)
		{
			HTR_LOG(Warning, "Load: {} could not find attach parent {}.{}", GetName(), Pending.ParentActorName, Pending.ParentComponentName);
			continue;
		}
		// 저장된 Transform은 부모 기준 값이므로 그대로 붙인다.
		Pending.Child->SetupAttachment(Parent, EAttachmentRule::KeepRelative);
	}
	PendingActorAttachments.Empty();
}

UActorComponent* AActor::AddComponent(UClass* ComponentClass, FName Name)
{
	if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		return nullptr;
	}


	if (FName(Name) == FName("None"))
	{
		Name = MakeUniqueObjectName(this, ComponentClass);
	}

	UActorComponent* NewComponent = NewObject<UActorComponent>(this, ComponentClass, Name);
	if (!NewComponent) { return nullptr; }

	if (USceneComponent* SceneComponent = Cast<USceneComponent>(NewComponent))
	{
		SceneComponent->SetupAttachment(RootComponent, EAttachmentRule::KeepRelative);
	}

	NewComponent->SetOwner(this);
	Components.Add(NewComponent);
	NewComponent->RegisterComponent();

	// 컴포넌트 BP면 BP 기본값을 얹는다 (씬을 읽을 때처럼 등록한 뒤 Serialize로 읽는다).
	if (FBlueprint::IsBlueprintClass(ComponentClass))
		FBlueprint::ApplyComponentDefaults(NewComponent);

	return NewComponent;
}

void AActor::DestroyComponent(UActorComponent* Component)
{
	if (!Component)
		return;

	if (Component == RootComponent)
	{
		HTR_LOG(Warning, "DefaultSceneRoot cannot be deleted.");
		return;
	}

	if (USceneComponent* Parent = Cast<USceneComponent>(Component))
	{
		TArray<USceneComponent*> Children = Parent->GetAttachChildren();
		for (USceneComponent* Candidate : Children)
		{
			if (!Candidate) { continue; }

			Candidate->DetachFromParent(EAttachmentRule::KeepWorld);
			Candidate->SetupAttachment(Parent->GetAttachParent(), EAttachmentRule::KeepWorld);
		}
	}

	Component->UnregisterComponent();
	RemoveOwnedComponent(Component);
	delete Component;
	return;
}

void AActor::OnDefaultSubobjectCreated(UObject* Subobject)
{
	if (UActorComponent* Component = Cast<UActorComponent>(Subobject))
	{
		Component->SetOwner(this);
		Components.Add(Component);
	}
}

UActorComponent* AActor::FindComponentByName(FName Name) const
{
	for (UActorComponent* Component : Components)
		if (Component && Component->GetFName() == Name)
			return Component;
	return nullptr;
}

void AActor::RegisterAllComponents()
{
	if (!World) return;

	for (UActorComponent* Component : Components)
	{
		if (Component && !Component->IsRegistered())
			Component->RegisterComponentWithWorld(World);
	}
}

void AActor::UnregisterAllComponents()
{
	for (UActorComponent* Component : Components)
	{
		if (Component && Component->IsRegistered())
			Component->UnregisterComponent();
	}
}

bool AActor::TryGetActorBounds(FBox& OutBounds) const
{
	bool bHasBounds = false;

	for (UActorComponent* Component : Components)
	{
		UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
		if (!Primitive)
			continue;

		FBox PrimitiveBounds = Primitive->CalcBounds();
		if (bHasBounds)
		{
			OutBounds.Expand(PrimitiveBounds);
		}
		else
		{
			OutBounds = PrimitiveBounds;
			bHasBounds = true;
		}
	}

	return bHasBounds;
}

void AActor::NotifyActorBeginOverlap(AActor* OtherActor)
{
	// TODO(A): if (Script) Script->Call("OnBeginOverlap", OtherActor);
	//          sol.hpp는 LuaScriptInstance.cpp 안에서만 include하도록 비템플릿 진입점을 거친다
	if (Script)
		Script->Call("OnBeginOverlap", OtherActor);
	if (FBlueprint::IsBlueprintClass(GetClass()))
		FBlueprintGraphRuntime::OnActorBeginOverlap(this, OtherActor);
}

void AActor::NotifyActorEndOverlap(AActor* OtherActor)
{
	// TODO(A): if (Script) Script->Call("OnEndOverlap", OtherActor);
	if (Script)
		Script->Call("OnEndOverlap", OtherActor);
	if (FBlueprint::IsBlueprintClass(GetClass()))
		FBlueprintGraphRuntime::OnActorEndOverlap(this, OtherActor);
}

void AActor::NotifyHit(AActor* OtherActor)
{
	// TODO(A): if (Script) Script->Call("OnHit", OtherActor);
	if (Script)
		Script->Call("OnHit", OtherActor);

}

void AActor::NotifyDeath(AActor* OtherActor)
{
	if (Script)
		Script->Call("OnDeath", OtherActor);
}

bool AActor::IsOverlappingActor(const AActor* Other) const
{
	if (!Other)
		return false;

	for (UActorComponent* Component : Components)
	{
		UPrimitiveComponent* Primitive =
			Cast<UPrimitiveComponent>(Component);

		if (!Primitive)
			continue;

		for (const FOverlapInfo& Info :
			Primitive->GetOverlapInfos())
		{
			if (Info.OtherComp &&
				Info.OtherComp->GetOwner() == Other)
			{
				return true;
			}
		}
	}

	return false;	return false;
}

void AActor::GetOverlappingActors(TArray<AActor*>& OutActors) const
{
	OutActors.Reset();
	TSet<AActor*> Seen;
	for (UActorComponent* Component : Components)
	{
		const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
		if (!Primitive || !Primitive->IsRegistered()) continue;
		for (const FOverlapInfo& Info : Primitive->GetOverlapInfos())
		{
			if (!Info.OtherComp || !Info.OtherComp->IsRegistered()) continue;
			AActor* Other = Info.OtherComp->GetOwner();
			if (Other && Other != this && Other->GetWorld() == GetWorld() && Seen.Add(Other))
				OutActors.Add(Other);
		}
	}
}

void AActor::CalcCamera(float DeltaTime, FMinimalViewInfo& OutPOV)
{

	// 카메라있음 -> 정보 전달
	for (UActorComponent* Component : GetComponents())
	{
		if (UCameraComponent* Camera = Cast<UCameraComponent>(Component))
		{
			Camera->GetCameraView(DeltaTime, OutPOV);
			return;
		}
	}
	// 카메라없음 -> 액터자신
	OutPOV.Location = GetActorLocation();
	OutPOV.Rotation = GetActorRotation();
	OutPOV.DepthOfField = FDepthOfFieldSettings{};
}
