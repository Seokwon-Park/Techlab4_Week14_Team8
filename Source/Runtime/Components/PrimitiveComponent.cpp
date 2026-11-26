#include "EnginePCH.h"
#include "PrimitiveComponent.h"
#include "../Render/Renderer.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"

#include "Engine/PrimitiveSceneProxy.h"
#include "Engine/Scene.h"
#include "Physics/BodySetup.h"

#include "Physics/PhysicsScene.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	FString PrimitiveTypeToString(EPrimitiveType Type)
	{
		switch (Type)
		{
		case EPrimitiveType::Sphere:
			return "Sphere";
			break;
		case EPrimitiveType::Cube:
			return "Cube";
			break;
		case EPrimitiveType::Cone:
			return "Cone";
			break;
		case EPrimitiveType::Plane:
			return "Plane";
			break;
		default:
			return "";
			break;
		}
	}
}

UPrimitiveComponent::UPrimitiveComponent()
{
	//SetMaterial(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
}

UPrimitiveComponent::~UPrimitiveComponent()
{

}

void UPrimitiveComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UPrimitiveComponent::SetSimulatePhysics(bool bSimulate)
{
	bSimulatePhysics = bSimulate;
	BodyInstance.SetInstanceSimulatePhysics(bSimulate);
}

void UPrimitiveComponent::SetGenerateOverlapEvents(bool bEnabled)
{
	if (bGenerateOverlapEvents != bEnabled)
	{
		bGenerateOverlapEvents = bEnabled;
		UpdateCollisionSettings();
	}
}

void UPrimitiveComponent::SetBlockComponent(bool bEnabled)
{
	if (bBlockComponent != bEnabled)
	{
		bBlockComponent = bEnabled;
		UpdateCollisionSettings();
	}
}

void UPrimitiveComponent::UpdateCollisionSettings()
{
	BodyInstance.UpdateCollisionSettings();
}

// UE PrimitiveComponentPhysics.cpp SyncComponentToRBPhysics와 같은 흐름.
// 어떤 바디를 따라갈지(GetBodyInstance)와 바디 -> 컴포넌트 변환(GetComponentTransformFromBodyInstance)은 하위 클래스가 정한다
void UPrimitiveComponent::SyncComponentToRBPhysics()
{
	FBodyInstance* UseBI = GetBodyInstance();
	if (!bSimulatePhysics || !UseBI || !UseBI->IsValidBodyInstance()) return;
	SetWorldTransform(GetComponentTransformFromBodyInstance(UseBI).GetLocalMatrix(), false);
}

FTransform UPrimitiveComponent::GetComponentTransformFromBodyInstance(FBodyInstance* UseBI)
{
	FTransform T = UseBI->GetWorldTransform();
	T.Scale = FTransform::FromMatrix(GetWorldMatrix()).Scale;   // PhysX 포즈에는 스케일이 없다
	return T;
}

void UPrimitiveComponent::OnUnregister()
{
	Super::OnUnregister();
	// 삭제/등록 해제된 도형을 상대의 OverlapInfos에 남기지 않는다.
	while (!OverlapInfos.IsEmpty())
	{
		UPrimitiveComponent* Other = OverlapInfos[OverlapInfos.Num() - 1].OtherComp;
		EndComponentOverlap(Other);
		if (Other) Other->EndComponentOverlap(this);
	}
}

void UPrimitiveComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
}

bool UPrimitiveComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMesh(WorldRay, *Mesh, GetWorldMatrix(), OutHit);
}

FPrimitiveSceneProxy* UPrimitiveComponent::CreateSceneProxy()
{
	return new FPrimitiveSceneProxy(this);
}

void UPrimitiveComponent::MarkRenderStateDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkRenderStateDirty(SceneProxy);
}

bool UPrimitiveComponent::LineTraceComponentLocal(const FRay& LocalRay, float& OutT)
{
	const FStaticMeshData* Mesh = GetMeshData();
	return Mesh && TraceMeshLocal(LocalRay, *Mesh, OutT);
}

void UPrimitiveComponent::OnTransformDirty()
{
	if (SceneProxy)
		SceneProxy->GetScene()->MarkDirty(SceneProxy);

	// 시뮬레이션 바디는 PhysX가 위치를 정하고 SyncComponentToRBPhysics가 컴포넌트로 가져온다.
	// 여기서 다시 쓰면 그 결과를 덮어쓴다
	if (bSimulatePhysics || !BodyInstance.IsValidBodyInstance())
		return;

	const FTransform WorldTransform = FTransform::FromMatrix(GetWorldMatrix());
	if ((WorldTransform.Scale - BodyInstance.GetScale3D()).SizeSquared() > 1e-8f)
	{
		RecreatePhysicsState();
		return;
	}
	BodyInstance.SetBodyTransform(WorldTransform);
}

bool UPrimitiveComponent::MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit)
{
	return Super::MoveComponent(Delta, NewRotation, bSweep, Hit);
}

void UPrimitiveComponent::PostEditChangeProperty(const FProperty& Property)
{
	if (Property.Name == "bCastShadow" ||
		Property.Name == "bReceiveShadow" ||
		Property.Name == "bReceivesDecals" ||
		Property.Name == "TranslucencySortPriority")
	{
		MarkRenderStateDirty();
	}

	if (Property.Name == "bSimulatePhysics")
		SetSimulatePhysics(bSimulatePhysics);
	if (Property.Name == "bUseCCD")
		BodyInstance.SetUseCCD(bUseCCD);
	if (Property.Name == "bGenerateOverlapEvents" || Property.Name == "bBlockComponent")
	{
		UpdateCollisionSettings();
	}
}

void UPrimitiveComponent::SetCastShadow(bool NewValue)
{
	if (bCastShadow != NewValue)
	{
		bCastShadow = NewValue;
	}
}

void UPrimitiveComponent::SetReceiveShadow(bool NewValue)
{
	if (bReceiveShadow != NewValue)
	{
		bReceiveShadow = NewValue;
	}
}

void UPrimitiveComponent::CreateRenderState()
{
	Super::CreateRenderState();
	GetWorld()->GetScene().AddPrimitive(this);
}

void UPrimitiveComponent::DestroyRenderState()
{
	GetWorld()->GetScene().RemovePrimitive(this);
	Super::DestroyRenderState();
}

bool UPrimitiveComponent::ShouldCreatePhysicsState() const
{
	return GetWorld() && GetWorld()->GetPhysicsScene();
}

void UPrimitiveComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();
	UBodySetup* Setup = GetBodySetup();
	if (!Setup || Setup->AggGeom.GetElementCount() == 0) return;
	const EPhysicsBodyType Type = bSimulatePhysics ? EPhysicsBodyType::Dynamic : EPhysicsBodyType::Kinematic;

	FPhysicsScene* Scene = GetWorld()->GetPhysicsScene();
	BodyInstance.SetUseCCD(bUseCCD);
	if (!BodyInstance.InitBody(*Setup, FTransform::FromMatrix(GetWorldMatrix()), this, *GetWorld()->GetPhysicsScene(), Type))
	{
		HTR_LOG(Warning, "InitBody failed: {} on {} (non-uniform scale or invalid shape?)", GetClass()->Name,
			GetOwner() ? GetOwner()->GetName() : FString("(no owner)"));
		return;
	}
}

void UPrimitiveComponent::OnDestroyPhysicsState()
{
	BodyInstance.TermBody();
	Super::OnDestroyPhysicsState();
}

bool UPrimitiveComponent::TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult)
{
	FBox MeshBox = Mesh.AABB.GetWorldAABB(WorldMatrix);
	float BoxT;
	if (!RayIntersectsAABB(WorldRay, MeshBox.Min, MeshBox.Max, BoxT))
	{
		return false;
	}

	FRay LocalRay = ToLocalRay(WorldRay, WorldMatrix);
	float T = FLT_MAX;

	if (!RayIntersectsMesh(LocalRay, Mesh, T)) return false;

	OutResult.HitComponent = this;
	OutResult.Distance = T;
	OutResult.ImpactPoint = WorldRay.Origin + WorldRay.Direction * T;

	return true;
}



bool UPrimitiveComponent::TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT)
{
	return RayIntersectsMesh(LocalRay, Mesh, OutT);
}

bool UPrimitiveComponent::IsOverlappingComponent(const UPrimitiveComponent* Other) const
{
	if (!Other) return false;

	for (const FOverlapInfo& Info : OverlapInfos)
	{
		if (Info.OtherComp == Other)
			return true;
	}

	return false;
}

void UPrimitiveComponent::BeginComponentOverlap(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp || OtherComp == this || IsOverlappingComponent(OtherComp))
	{
		return;
	}

	OverlapInfos.Add({ OtherComp });

	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentBeginOverlap.Broadcast(this, OtherActor, OtherComp);
	if (Owner && OtherActor)
	{
		Owner->NotifyActorBeginOverlap(OtherActor);
	}
}

void UPrimitiveComponent::EndComponentOverlap(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp) return;

	bool bRemoved = false;

	for (int32 Index = 0;Index < OverlapInfos.Num(); ++Index)
	{
		if (OverlapInfos[Index].OtherComp == OtherComp)
		{
			OverlapInfos.RemoveAt(Index, 1);
			bRemoved = true;
			break;
		}
	}

	if (!bRemoved)
		return;


	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentEndOverlap.Broadcast(this, OtherActor, OtherComp);

	if (Owner && OtherActor)
	{
		Owner->NotifyActorEndOverlap(OtherActor);
	}
}

void UPrimitiveComponent::DispatchHit(UPrimitiveComponent* OtherComp)
{
	if (!OtherComp) 
		return;

	AActor* Owner = GetOwner();
	AActor* OtherActor = OtherComp->GetOwner();

	OnComponentHit.Broadcast(this, OtherActor, OtherComp);

	if (Owner && OtherActor)
	{
		Owner->NotifyHit(OtherActor);
	}
}
