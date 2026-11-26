#pragma once

#include "SceneComponent.h"
#include "../Render/Shader.h"
#include "../Render/Mesh.h"
#include "Render/RenderPacket.h"
#include "Render/GeometryGenerator.h"
#include "Collision/HitResult.h"
#include "Core/Delegate/Delegate.h"
#include "Physics/BodyInstance.h"   // FBodyInstance를 값으로 들고 있으므로 전방 선언으로는 부족
#include "PrimitiveComponent.generated.h"

class FPrimitiveSceneProxy;
class AActor;
class UPrimitiveComponent;
class UBodySetup;

// (Self, Other, OtherComp). 담당: C
DECLARE_DELEGATE(FComponentOverlapSignature, UPrimitiveComponent*, AActor*, UPrimitiveComponent*);
DECLARE_DELEGATE(FComponentHitSignature, UPrimitiveComponent*, AActor*, UPrimitiveComponent*);
DECLARE_DELEGATE(FComponentContactSignature, const FVector&, const FVector&, float);

struct FOverlapInfo
{
	UPrimitiveComponent* OtherComp = nullptr;
};

enum class EPrimitiveType
{
	Sphere,
	Cube,
	Cone,
	Plane,

	Particle,
	ParticleQuad
};

UCLASS()
class UPrimitiveComponent :public USceneComponent
{
	GENERATED_BODY()

	// Material은 여기서 등록하지 않는다. 컴포넌트마다 머티리얼을 다루는 방식이 달라서
	// (빌보드는 단일 머티리얼, 스태틱 메시는 슬롯별 덮어쓰기) 각자 등록한다.
public:
	UPrimitiveComponent();
	virtual ~UPrimitiveComponent();

	virtual void BeginPlay() override;

	// Todo: subuv
	//void SubmitToRenderQueue(TQueue<FRenderPacket>& RenderQueue);
	virtual void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext);

	virtual const FStaticMeshData* GetMeshData() const { return nullptr; }

	// Outline처럼 컴포넌트를 통째로 한 번 더 그릴 때 쓰는 GPU 메시.
	// 메시가 없거나(텍스트) View에 따라 형상이 정해지는 컴포넌트(빌보드·파티클)는
	// 컴포넌트 월드 행렬만으로 같은 그림을 못 만들므로 nullptr을 돌려준다.
	virtual UStaticMesh* GetRenderMesh() const { return nullptr; }

	virtual int32 GetNumMaterials() const { return 0; }
	virtual UMaterial* GetMaterial(int32 SlotIndex) const { return nullptr; }
	virtual void SetMaterial(int32 SlotIndex, UMaterial* InMaterial) {}
	//
	// FShader* GetShader() const { return Shader.get(); };

	void SetVisible(bool bInVisible) { SetVisibility(bInVisible); }

	virtual bool LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit);
	virtual bool LineTraceComponentLocal(const FRay& LocalRay, float& OutT);
	virtual FBox CalcLocalBounds() const override
	{
		const FStaticMeshData* Data = GetMeshData();
		return Data ? Data->AABB : Super::CalcLocalBounds();
	}

	virtual FPrimitiveSceneProxy* CreateSceneProxy();
	FPrimitiveSceneProxy* GetSceneProxy() const { return SceneProxy; }

	void MarkRenderStateDirty() override;

	virtual void OnTransformDirty() override;

	virtual bool MoveComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit = nullptr) override;

	virtual void PostEditChangeProperty(const FProperty& Property) override;

	void SetCastShadow(bool NewValue);
	void SetReceiveShadow(bool NewValue);

	UPROPERTY(EditAnywhere)
	bool bCastShadow = true;
	UPROPERTY(EditAnywhere)
	bool bReceiveShadow = true;
	UPROPERTY(EditAnywhere)
	bool bReceivesDecals = true;

	// ---- 충돌 이벤트 (담당: C) ----
	FComponentOverlapSignature OnComponentBeginOverlap;
	FComponentOverlapSignature OnComponentEndOverlap;
	FComponentHitSignature OnComponentHit;
	// PhysX contact point and inward normal in the rigid body's local space, followed by impulse magnitude.
	FComponentContactSignature OnComponentContact;

	UPROPERTY(EditAnywhere)
	bool bGenerateOverlapEvents = true;
	UPROPERTY(EditAnywhere)
	bool bBlockComponent = false;
	// 실행 중 충돌 응답 변경과 네이티브 형상 동기화
	void SetGenerateOverlapEvents(bool bEnabled);
	void SetBlockComponent(bool bEnabled);
	virtual void UpdateCollisionSettings();

	UPROPERTY(EditAnywhere)
	int32 TranslucencySortPriority = 0;

	bool IsOverlappingComponent(const UPrimitiveComponent* Other) const;
	const TArray<FOverlapInfo>& GetOverlapInfos() const { return OverlapInfos; }

	// UWorld::UpdateOverlaps가 겹침 상태가 바뀐 프레임에 호출한다
	void BeginComponentOverlap(UPrimitiveComponent* OtherComp);
	void EndComponentOverlap(UPrimitiveComponent* OtherComp);
	void DispatchHit(UPrimitiveComponent* OtherComp);

	FBodyInstance BodyInstance;
	UPROPERTY(EditAnywhere)
	bool bSimulatePhysics = false;   // UE는 BodyInstance.bSimulatePhysics. A의 FBodyInstance에 없어서 컴포넌트에 둔다
	UPROPERTY(EditAnywhere)
	bool bUseCCD = false;

	virtual UBodySetup* GetBodySetup() { return nullptr; }   // UStaticMeshComponent가 override
	UFUNCTION(BlueprintCallable)
	virtual void SetSimulatePhysics(bool bSimulate);
	void SyncComponentToRBPhysics();
	// UE UPrimitiveComponent::GetBodyInstance. 이름이 없으면 컴포넌트를 대표하는 바디 (스켈레탈은 루트 바디)
	virtual FBodyInstance* GetBodyInstance(FName BoneName = NAME_None) const { return const_cast<FBodyInstance*>(&BodyInstance); }
	// PhysX 씬 쿼리에 잡히는 바디가 하나라도 있는지
	virtual bool HasValidPhysicsBody() const { return BodyInstance.IsValidBodyInstance(); }
	// UE UPrimitiveComponent::GetComponentTransformFromBodyInstance. 바디 월드 포즈 -> 컴포넌트 월드 Transform
	virtual FTransform GetComponentTransformFromBodyInstance(FBodyInstance* UseBI);

protected:
	virtual void OnUnregister() override;
	TArray<FOverlapInfo> OverlapInfos;

	virtual bool ShouldCreateRenderState() const override
	{
		return true;
	}

	virtual void CreateRenderState() override;
	virtual void DestroyRenderState() override;

	bool ShouldCreatePhysicsState() const override;
	void OnCreatePhysicsState() override;
	void OnDestroyPhysicsState() override;

	bool TraceMesh(const FRay& WorldRay, const FStaticMeshData& Mesh, const FMatrix& WorldMatrix, FHitResult& OutResult);
	bool TraceMeshLocal(const FRay& LocalRay, const FStaticMeshData& Mesh, float& OutT);
private:
	friend class FScene;
	FPrimitiveSceneProxy* SceneProxy = nullptr;
};
