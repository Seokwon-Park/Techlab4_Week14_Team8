#pragma once

#include "SkinnedMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Render/SkeletalMeshObject.h"
#include "SkeletalMeshComponent.generated.h"

class UAnimInstance;
class UAnimSequence;
class UBlendSpace1D;
class UWorld;
class FLineBatcher;
// World의 SkeletalMeshComponent마다 root 디버그를 그린다 (DrawCollisionShapes와 같은 방식). PIE의 Show Root에서 호출
void DrawSkeletalRootDebug(UWorld* World, FLineBatcher* LineBatcher);
// Settings > Debug Draw (PIE) > Show Root 값. 에디터가 매 프레임 갱신하고, 런타임 액터("root" 라벨 등)가 읽는다
extern bool GShowSkeletalRootDebug;
class UAnimationAsset;
class UAnimSingleNodeInstance;
class UAnimGraphInstance;

struct FBodyInstance;
struct FConstraintInstance;
struct FPhysicsOverlapShape;
class FPhysicsScene;
class UPhysicsAsset;
class USkeletalMeshComponent;

namespace physx
{
	class PxTransform;
}

// 네이티브 Cloth Solver의 전방 선언
namespace nv::cloth
{
	class Factory;
	class Cloth;
	class Solver;
}

// 본 포즈 확정 이후 실행되는 Cloth 전용 틱
struct FSkeletalMeshComponentClothTickFunction : public FTickFunction
{
	USkeletalMeshComponent* Target = nullptr;
	void ExecuteTick(float DeltaTime) override;
};

struct FSkeletalMeshComponentEndPhysicsTickFunction : public FTickFunction
{
	USkeletalMeshComponent* Target = nullptr;
	void ExecuteTick(float DeltaTime) override;
};

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	GENERATED_BODY()

public:
    USkeletalMeshComponent();
    ~USkeletalMeshComponent() override;

    bool SetAnimBlueprint(const FString& Path);
    bool ReloadAnimBlueprint();
    UAnimGraphInstance* CreateAnimGraph();
    const FString& GetAnimBlueprintPath() const;
    const FString& GetAnimBlueprintError() const;
	void SetAnimInstance(UAnimInstance* InInstance);
	UAnimSingleNodeInstance* GetSingleNodeInstance() const;
	UAnimInstance* GetAnimInstance() const { return AnimInstance; }
	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;

	const TArray<FBoneInfo>& GetBones() const;
	int32 FindBoneIndex(const FString& Name) const;
	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local);
	const TArray<FTransform>& GetBoneSpaceTransforms() const { return BoneSpaceTransforms; }
	void SetBoneSpaceTransforms(const struct FCompactPose& Pose); // 애니메이션 평가 결과를 받는 입구
	FMatrix GetBoneComponentMatrix(int32 BoneIndex) const;
	FMatrix GetBoneWorldMatrix(int32 BoneIndex) const;

	void ResetPose();
	void RefreshBoneTransformsNow();

	// 스켈레탈 메쉬 뷰어 관련 함수
	bool RestartClothPreview();
	void StopClothPreview();
	void TickClothPreview(float DeltaTime);
	bool HasClothSimulation() const { return ClothSolver.get() != nullptr; }
	// 실행 중인 천을 현재 본 포즈에서 처음부터 다시 시뮬레이션한다
	void ForceClothNextUpdateTeleportAndReset();

	// Cloth 충돌에 사용할 PhysicsAsset 바디 이름
	UPROPERTY(EditAnywhere)
	FString ClothCollisionBody;

	// 게임 월드의 정적·이동 강체와 천의 충돌
	UPROPERTY(EditAnywhere)
	bool bCollideClothWithEnvironment = false;

	// Solver에 전달한 몸통 충돌 형상의 프리뷰 표시
	void DrawClothBodyCollision(FLineBatcher& LineBatcher) const;

	// 같은 천 섹션의 입자 분리 거리(미터), 0이면 자기충돌 해제
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.001))
	float ClothSelfCollisionDistance = 0.0f;

	// 자기충돌의 거리 보정 강도
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01))
	float ClothSelfCollisionStiffness = 1.0f;

	// 공기의 흐름에 천이 밀리는 항력 계수
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01))
	float ClothWindDragCoefficient = 0.0f;

	// 천 표면과 공기의 상대 방향에 따른 양력 계수
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01))
	float ClothWindLiftCoefficient = 0.0f;

	// 충돌면 위에서 미끄러질 때의 마찰 계수
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01))
	float ClothFriction = 0.8f;

	// 입자 속도를 줄이는 비율, 0이면 감쇠 없음
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01))
	float ClothDamping = 0.05f;

	// 월드 충돌면에서 천을 띄우는 거리(미터). 천이 충돌면과 같은 깊이에 그려지는 것을 막는다
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.001))
	float ClothCollisionThickness = 0.01f;

	void PostEditChangeProperty(const FProperty& Property) override;

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	void SetAnimation(UAnimationAsset* NewAnimToPlay);
	void Play(bool bLooping = true);
	void Stop();
	bool IsPlaying() const;
	void PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLoop = true);
	void RefreshAnimationPose();

	UPROPERTY()
	float BlendSpaceInput = 300.0f;

	// 루트 모션: 이번 Tick까지 모인 이동량(월드 공간). CharacterMovement가 가져가 Actor를 움직인다
	bool HasPendingRootMotion() const { return bHasPendingRootMotion; }
	FTransform ConsumeRootMotion();

	// root 부모 공간 delta → 월드 공간 delta (Unreal ConvertLocalRootMotionToWorld + Armature 변환). 시퀀스 뷰어 미리보기도 쓴다
	FTransform ConvertRootMotionToWorld(const FTransform& ParentSpaceDelta) const;

	// 루트 모션이 뽑히는 본. "root"를 먼저, 없으면 첫 본 (UAnimSequence::FindRootTrack과 같은 규칙)
	int32 GetRootMotionBoneIndex() const;

	// 디버그: root 본 구체 + 액터 위치(root 높이)에서 root까지의 선
	void DrawRootDebug(FLineBatcher* LineBatcher) const;

	TArray<FBodyInstance*> Bodies;
	TArray<FConstraintInstance*>Constraints;
	bool bBlendPhysics = false;

	void InitArticulated(FPhysicsScene* PhysScene);
	void TermArticulated();
	void SetSimulatePhysics(bool bEnabled) override;
	void SetAllBodiesSimulatePhysics(bool bNewSimulate);
	// 이름이 없으면 루트 바디 (UE와 같음)
	FBodyInstance* GetBodyInstance(FName BoneName = NAME_None) const override;
	bool HasValidPhysicsBody() const override;
	void UpdateCollisionSettings() override;
	UPhysicsAsset* GetPhysicsAsset() const;
	// 메시의 PhysicsAsset 대신 쓸 에셋을 지정하고 바디를 다시 만든다. nullptr이면 메시의 에셋으로 돌아간다
	void SetPhysicsAsset(UPhysicsAsset* NewPhysicsAsset);

	UPROPERTY(EditAnywhere)
	FString PhysicsAssetOverride;

	// 래그돌 중 컴포넌트가 따라갈 바디(본 계층에서 가장 위에 있는 바디)와 그 바디 기준 컴포넌트 Transform
	struct FRootBodyData
	{
		int32 BodyIndex = INDEX_NONE;
		FTransform TransformToRoot;
	};
	int32 FindRootBodyIndex() const;
	void SetRootBodyIndex(int32 InBodyIndex);
	void ResetRootBodyIndex();
	FTransform GetComponentTransformFromBodyInstance(FBodyInstance* UseBI) override;

protected:
	// 단일 애니메이션 에셋 경로/루프/재생 속도를 씬에 저장·복원한다 (Serialize에서 부른다)
	void SerializeSingleAnimation(FStructuredArchive::FRecord& Record);

	void RefreshBoneTransforms() override;
	void OnSkeletalMeshChanged() override;
	bool RequiresCPUSkinning() const override;
	void OnRegister() override;
	void OnUnregister() override;
	void OnCreatePhysicsState() override;
	void OnDestroyPhysicsState() override;
	void InstantiatePhysicsAssetBodies_Internal(const UPhysicsAsset& PhysAsset, FPhysicsScene& PhysScene);
	void UpdateKinematicBonesToAnim(); // 애니메이션 상태: 바디가 본을 따라감
	void BlendPhysicsBones(); // 래그돌 상태: 바디->본

	//래그돌 바디가 있을 때만 등록한다
	friend struct FSkeletalMeshComponentEndPhysicsTickFunction;
	FSkeletalMeshComponentEndPhysicsTickFunction EndPhysicsTickFunction;
	void RegisterEndPhysicsTick(bool bRegister);
	void EndPhysicsTickComponent(float DeltaTime);
private:
    UPROPERTY()
    FString AnimBlueprintPath;

    TUniquePtr<UAnimGraphInstance> ManagedAnimGraph;
    FString AnimBlueprintError;
    bool bBlueprintReloadRequested = false;
    bool bUpdatingAnimation = false;

	UPROPERTY()
	UAnimInstance* AnimInstance = nullptr;

	TArray<FTransform> BoneSpaceTransforms;

	void LoadPhysicsAssetOverride();
	UPhysicsAsset* PhysicsAssetOverrideAsset = nullptr;

	FRootBodyData RootBodyData;
	// [Hitori 추가] 래그돌을 켜기 직전 상태. 끌 때 똑바로 세우는 데 쓴다
	FTransform PreRagdollComponentTransform;
	FVector PreRagdollRootBodyLocation = FVector(0, 0, 0);

	FTransform PendingRootMotion;           // 월드 공간 누적값
	bool bHasPendingRootMotion = false;

	// 네이티브 Cloth와 렌더 정점 대응 정보의 공동 소유
	struct FClothInstance
	{
		TUniquePtr<nv::cloth::Cloth> Cloth;
		TArray<uint32> ParticleToRenderVertex;
		TArray<float> MotionConstraintRadii;
		FVector SimulationScale;
	};

	friend struct FSkeletalMeshComponentClothTickFunction;
	TUniquePtr<nv::cloth::Solver> ClothSolver;
	FSkeletalMeshComponentClothTickFunction ClothTickFunction;
	const USkeletalMesh* ClothSourceMesh = nullptr;

	TArray<FClothInstance> ClothInstances;
	TArray<FClothVertexPosition> ClothVertexPositions;

	// Cloth 하나가 사용할 수 있는 SDK 충돌 배열의 상한
	static constexpr int32 MaxClothCollisionSpheres = 32;
	static constexpr int32 MaxClothCollisionPlanes = 32;
	static constexpr int32 ClothBoxPlaneCount = 6;

	// 등록된 바디와 Cloth 공간의 충돌 데이터
	FString ActiveClothCollisionBody;
	TArray<FVector4> ClothCollisionSpheres;
	TArray<uint32> ClothCollisionCapsuleIndices;
	TArray<FVector4> ClothCollisionPlanes;
	TArray<uint32> ClothCollisionConvexMasks;

	void BuildClothBodyCollision();
	void UploadClothBodyCollision();
	void RemoveClothWorldCollision(nv::cloth::Cloth& Cloth) const;
	void AppendClothWorldCollision(nv::cloth::Cloth& Cloth, const physx::PxTransform& ClothToWorld,
		const TArray<FPhysicsOverlapShape>& Shapes) const;

	FClothInstance CreateClothInstance(nv::cloth::Factory& Factory,
		const FSkeletalMeshLODRenderData& LOD, const FClothSectionBinding& Binding,
		float GravityZ,bool bForPreview = false) const;
	void ApplyClothSelfCollision(const FClothInstance& Instance, const FSkeletalMeshLODRenderData& LOD) const;
	void ApplyClothWindCoefficients(nv::cloth::Cloth& Cloth) const;
	void ApplyClothFrictionAndDamping(nv::cloth::Cloth& Cloth) const;
	void UpdateClothMotionConstraints();
	void UpdateClothVertexPositions();
	const TArray<FClothVertexPosition>* GetClothVertexPositions() const override
	{
		return &ClothVertexPositions;
	}

	// 프리뷰 고정 스텝의 잔여 시간
	double ClothPreviewAccumulatedSeconds = 0.0;

	void RefreshClothSolver(bool bForPreview = false);
	void ReleaseClothSolver(bool bRefreshSkinningMode = true);
	void TickClothing();
	void AdvanceCloth(int32 StepCount, float StepDelta);
};
