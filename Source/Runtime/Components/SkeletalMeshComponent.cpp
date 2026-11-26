#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Serialization/MathSerialization.h"
#include "Animation/BonePose.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimBlueprintCompiler.h"
#include "Animation/AnimBlueprintFile.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Casts.h"
#include "UObject/UObjectIterator.h"
#include "Render/LineBatcher.h"
#include "GameFramework/Actor.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/BlendSpaceFile.h"
#include "Asset/AssetManager.h"
#include <PxScene.h>
#include <PxSceneLock.h>
#include "Physics/BodyInstance.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/PhysicsScene.h"
#include "Engine/World.h"
#include <extensions/PxDefaultSimulationFilterShader.h>   // PxSetGroup, PxSetGroupCollisionFlag
#include <PxRigidDynamic.h>
#include "Physics/NvClothSDK.h"
#include "Physics/PhysXConversion.h"
#include <NvCloth/Cloth.h>
#include <NvCloth/Fabric.h>
#include <NvCloth/PhaseConfig.h>
#include <NvClothExt/ClothFabricCooker.h>
#include <limits>
#include <algorithm>
#include <cmath>
#include "Physics/PhysicsSettings.h"
#include <NvCloth/Factory.h>
#include <NvCloth/Solver.h>

namespace
{
	// 래그돌 바디끼리는 충돌하지 않게 한 그룹으로 묶는다.
	// 씬이 PxDefaultSimulationFilterShader를 쓰므로 CollisionDisableTable 대신 그룹 충돌 표를 쓴다 (A와 합의 필요)
	constexpr physx::PxU16 RagdollCollisionGroup = 1;
}
void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InInstance)
{
	if (AnimInstance == InInstance)
	{
		return;
	}
	if (AnimInstance)
	{
		AnimInstance->UninitializeAnimation();
	}
	AnimInstance = InInstance;

	if (ManagedAnimGraph && ManagedAnimGraph.get() != InInstance)
	{
		ManagedAnimGraph.reset();
	}

	AnimBlueprintPath.clear();
	bBlueprintReloadRequested = false;

	if (AnimInstance)
	{
		AnimInstance->InitializeAnimation();
	}
}

void USkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!AnimBlueprintPath.empty() && (bBlueprintReloadRequested || !ManagedAnimGraph))
	{
		ReloadAnimBlueprint();
	}

	if (AnimInstance)
	{
		AnimInstance->InitializeAnimation();

		if (auto* Instance = GetSingleNodeInstance())
		{
			if (Instance->GetCurrentAsset())
			{
				Instance->SetPlaying(true);
			}
		}
	}
	RefreshClothSolver();
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
	if (bBlueprintReloadRequested)
	{
		bBlueprintReloadRequested = false;
		ReloadAnimBlueprint();
	}

	// 래그돌 중(bBlendPhysics)에는 애니 평가를 건너뛴다. 본은 BlendPhysicsBones가 채운다
	if (AnimInstance && GetSkeletalMesh() && !bBlendPhysics)
	{
		bUpdatingAnimation = true;
		if (auto* Instance = Cast<UAnimSingleNodeInstance>(AnimInstance))
		{
			Instance->SetBlendSpaceInput(BlendSpaceInput);
		}

		AnimInstance->UpdateAnimation(DeltaTime);
		FPoseContext Pose(AnimInstance, GetBones());
		AnimInstance->EvaluateAnimation(Pose);
		SetBoneSpaceTransforms(Pose.Pose);
		AnimInstance->TriggerAnimNotifies(DeltaTime);

		// 루트 모션: AnimInstance에 모인 delta를 월드 공간으로 바꿔 CharacterMovement가 가져갈 때까지 쌓아 둔다
		const FRootMotionMovementParams RootMotion = AnimInstance->ConsumeExtractedRootMotion();
		if (RootMotion.bHasRootMotion)
		{
			const FTransform WorldDelta = ConvertRootMotionToWorld(RootMotion.GetRootMotionTransform());
			PendingRootMotion.Location = (bHasPendingRootMotion ? PendingRootMotion.Location : FVector::ZeroVector) + WorldDelta.Location;
			PendingRootMotion.Rotation = bHasPendingRootMotion ? (WorldDelta.Rotation * PendingRootMotion.Rotation).Normalized() : WorldDelta.Rotation;
			bHasPendingRootMotion = true;
		}
		bUpdatingAnimation = false;
	}
	Super::TickComponent(DeltaTime);

	// 갱신된 본 위치를 kinematic 바디의 목표로. 이번 프레임 물리 스텝(PrePhysics 다음)이 따라간다
	UpdateKinematicBonesToAnim();
}

const TArray<FBoneInfo>& USkeletalMeshComponent::GetBones() const
{
	static const TArray<FBoneInfo> Empty{};
	return GetSkeletalMesh() ? GetSkeletalMesh()->GetRefSkeleton() : Empty;
}

int32 USkeletalMeshComponent::FindBoneIndex(const FString& Name) const
{
	return GetSkeletalMesh() ? GetSkeletalMesh()->FindBoneIndex(Name) : -1;
}

FTransform USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num()) return FTransform();
	return BoneSpaceTransforms[BoneIndex];
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local)
{
	if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num()) return;
	BoneSpaceTransforms[BoneIndex] = Local;
	MarkBoneTransformsDirty();
}

void USkeletalMeshComponent::SetBoneSpaceTransforms(const FCompactPose& Pose)
{
	if (Pose.GetNumBones() != GetBones().Num())
	{
		HTR_LOG(Warning, "SetBoneSpaceTransforms: pose has {} bones, mesh has {}.", Pose.GetNumBones(), GetBones().Num());
		return;
	}
#if defined(_DEBUG)
	for (int32 i = 0; i < Pose.GetNumBones(); ++i)
	{
		const FQuat& Q = Pose[i].Rotation;
		const float LenSq = Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W;
		if (std::abs(LenSq - 1.0f) > 1e-3f)
			HTR_LOG(Warning, "Pose bone {} rotation not normalized (|q|^2 = {})", i, LenSq);
	}
#endif
	BoneSpaceTransforms = Pose.GetBones();
	MarkBoneTransformsDirty();
}

FMatrix USkeletalMeshComponent::GetBoneComponentMatrix(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= ComponentSpaceTransforms.Num()) return FMatrix::Identity;
	return ComponentSpaceTransforms[BoneIndex];
}

FMatrix USkeletalMeshComponent::GetBoneWorldMatrix(int32 BoneIndex) const
{
	return GetBoneComponentMatrix(BoneIndex) * GetWorldMatrix();
}

void USkeletalMeshComponent::ResetPose()
{
	OnSkeletalMeshChanged();
	MarkBoneTransformsDirty();
}

void USkeletalMeshComponent::RefreshBoneTransformsNow()
{
	UpdateBoneTransformsNow();
}

void USkeletalMeshComponent::RefreshBoneTransforms()
{
	const TArray<FBoneInfo>& Bones = GetBones();
	if (BoneSpaceTransforms.Num() != Bones.Num())
		OnSkeletalMeshChanged();

	ComponentSpaceTransforms.SetNum(Bones.Num());
	for (int i = 0; i < Bones.Num(); ++i)
	{
		const FMatrix Local = BoneSpaceTransforms[i].GetLocalMatrix();
		const int32 Parent = Bones[i].ParentIndex;
		ComponentSpaceTransforms[i] = Parent < 0 ? Local : Local * ComponentSpaceTransforms[Parent];
	}
}

void USkeletalMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
	FArchive& Ar = Record.GetUnderlyingArchive();
	UAnimInstance* SavedInstance = AnimInstance;

	if (Ar.IsSaving() && (!AnimBlueprintPath.empty() || ManagedAnimGraph))
	{
		AnimInstance = nullptr;
	}  

	Super::Serialize(Record);

	if (Ar.IsSaving())
	{
		AnimInstance = SavedInstance;
	}

	int32 Num = Ar.IsSaving() ? BoneSpaceTransforms.Num() : 0;
	FStructuredArchive::FArray PoseArray = Record.EnterArray("BonePose", Num);

	for (int32 I = 0; I < Num; ++I)
	{
		FStructuredArchive::FRecord BoneRecord = PoseArray.EnterElement().EnterRecord();
		FString Name = Ar.IsSaving() ? GetBones()[I].Name : FString();
		FTransform Local = Ar.IsSaving() ? BoneSpaceTransforms[I] : FTransform();
		BoneRecord << SA_VALUE("Name", Name) << SA_VALUE("Local", Local);

		if (Ar.IsLoading())
		{
			// 메시의 본 순서가 바뀌어도 맞도록 이름으로 찾는다
			const int32 Index = FindBoneIndex(Name);
			if (Index >= 0)
				BoneSpaceTransforms[Index] = Local;
		}
	}

	SerializeSingleAnimation(Record);

	if (Ar.IsLoading())
	{
		LoadPhysicsAssetOverride();

		if (!AnimBlueprintPath.empty())
		{
			ReloadAnimBlueprint();
		}

		RefreshBoneTransformsNow();
	}
}

void USkeletalMeshComponent::InitArticulated(FPhysicsScene* PhysScene)
{
	UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	if (!PhysAsset || !PhysScene || Bodies.Num() > 0) return;

	RefreshBoneTransformsNow();
	InstantiatePhysicsAssetBodies_Internal(*PhysAsset, *PhysScene);

	// UE InitArticulated: 루트 바디를 다시 찾는다
	RootBodyData.BodyIndex = INDEX_NONE;
	SetRootBodyIndex(FindRootBodyIndex());

	for (UPhysicsConstraintTemplate* Template : PhysAsset->ConstraintSetup)
	{
		const FConstraintInstance& Src = Template->DefaultInstance;
		FBodyInstance* Body1 = GetBodyInstance(Src.ConstraintBone1);
		FBodyInstance* Body2 = GetBodyInstance(Src.ConstraintBone2);
		if (!Body1 || !Body2) continue;

		FConstraintInstance* CI = new FConstraintInstance(Src);
		// 조인트 위치(Pos1/Pos2)는 본 로컬 공간 값이라 바디와 같은 스케일(본 월드 스케일)을 곱해야 한다.
		// UE는 본 스케일이 1이라 컴포넌트 스케일을 쓰지만, UAL은 Armature 본에 ×100이 있어 둘이 다르다
		const float Scale = FTransform::FromMatrix(GetBoneWorldMatrix(Body1->InstanceBoneIndex)).Scale.X;
		if (!CI->InitConstraint(Body1, Body2, Scale)) { delete CI; continue; }
		Constraints.Add(CI);
	}

	RegisterEndPhysicsTick(true);

	if (bSimulatePhysics) SetSimulatePhysics(true);
}

void USkeletalMeshComponent::TermArticulated()
{
	RegisterEndPhysicsTick(false);
	for (FConstraintInstance* CI : Constraints) { CI->TermConstraint(); delete CI; }
	Constraints.Empty();
	for (FBodyInstance* BI : Bodies)
	{
		if (BI)
		{
			BI->TermBody();
			delete BI;
		}
	}
	Bodies.Empty();
	ResetRootBodyIndex();
	bBlendPhysics = false;
}

void USkeletalMeshComponent::SetSimulatePhysics(bool bEnabled)
{
	const bool bWasSimulating = bBlendPhysics;
	bSimulatePhysics = bEnabled;
	bBlendPhysics = bEnabled;

	// [Hitori 추가] 래그돌을 끄면 루트 바디가 수평으로 움직인 만큼만 옮기고 켜기 전 자세(회전·높이)로 세운다.
	// UE는 컴포넌트를 쓰러진 방향 그대로 두고 일어나기는 게임 코드(Get-up 애니메이션 등)에 맡긴다
	FBodyInstance* RootBody = GetBodyInstance();
	if (bEnabled && !bWasSimulating && RootBody)
	{
		PreRagdollComponentTransform = FTransform::FromMatrix(GetWorldMatrix());
		PreRagdollRootBodyLocation = RootBody->GetWorldTransform().Location;
	}
	else if (!bEnabled && bWasSimulating && RootBody)
	{
		const FVector Moved = RootBody->GetWorldTransform().Location - PreRagdollRootBodyLocation;
		FTransform Upright = PreRagdollComponentTransform;
		Upright.Location.X += Moved.X;
		Upright.Location.Y += Moved.Y;
		SetWorldTransform(Upright.GetLocalMatrix(), false);
	}

	if (UPhysicsAsset* PhysAsset = GetPhysicsAsset())
	{
		for (int32 i = 0; i < Bodies.Num(); ++i)
		{
			if (Bodies[i] && PhysAsset->SkeletalBodySetups.IsValidIndex(i) && PhysAsset->SkeletalBodySetups[i]->PhysicsType == PhysType_Default)
				Bodies[i]->SetInstanceSimulatePhysics(bEnabled);
		}
	}

	// 켜는 순간의 컴포넌트-루트 바디 관계를 저장한다. 이후 컴포넌트는 이 관계를 유지하며 루트 바디를 따라간다
	if (bEnabled)
		SetRootBodyIndex(RootBodyData.BodyIndex);
}

void USkeletalMeshComponent::SetAllBodiesSimulatePhysics(bool bNewSimulate)
{
	for (FBodyInstance* BI : Bodies)
	{
		if (BI) BI->SetInstanceSimulatePhysics(bNewSimulate);
	}
}

FBodyInstance* USkeletalMeshComponent::GetBodyInstance(FName BoneName) const
{
	if (BoneName == NAME_None)
		return Bodies.IsValidIndex(RootBodyData.BodyIndex) ? Bodies[RootBodyData.BodyIndex] : nullptr;

	const UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	const int32 Index = PhysAsset ? PhysAsset->FindBodyIndex(BoneName) : INDEX_NONE;
	return Bodies.IsValidIndex(Index) ? Bodies[Index] : nullptr;
}

void USkeletalMeshComponent::UpdateCollisionSettings()
{
	Super::UpdateCollisionSettings();
	for (FBodyInstance* Body : Bodies)
	{
		if (Body)
		{
			Body->UpdateCollisionSettings();
		}
	}
}

bool USkeletalMeshComponent::HasValidPhysicsBody() const
{
	for (const FBodyInstance* Body : Bodies)
	{
		if (Body && Body->IsValidBodyInstance())
			return true;
	}
	return false;
}

UPhysicsAsset* USkeletalMeshComponent::GetPhysicsAsset() const
{
	if (PhysicsAssetOverrideAsset)
		return PhysicsAssetOverrideAsset;
	return GetSkeletalMesh() ? GetSkeletalMesh()->GetPhysicsAsset() : nullptr;
}

void USkeletalMeshComponent::SetPhysicsAsset(UPhysicsAsset* NewPhysicsAsset)
{
	PhysicsAssetOverrideAsset = NewPhysicsAsset;
	RecreatePhysicsState();
}

void USkeletalMeshComponent::LoadPhysicsAssetOverride()
{
	PhysicsAssetOverrideAsset = nullptr;
	if (PhysicsAssetOverride.empty()) return;

	UPhysicsAsset* Loaded = NewObject<UPhysicsAsset>(this);
	FString Error;
	if (!PhysicsAssetFile::Read(PhysicsAssetOverride, *Loaded, Error))
	{
		HTR_LOG(Warning, "PhysicsAssetOverride load failed: {} ({})", PhysicsAssetOverride, Error);
		return;
	}
	PhysicsAssetOverrideAsset = Loaded;
}

// UE FindRootBodyIndex: 본 순서(부모가 앞)대로 돌며 처음 만나는 바디
int32 USkeletalMeshComponent::FindRootBodyIndex() const
{
	int32 RootBodyIndex = RootBodyData.BodyIndex;
	const UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	if (RootBodyIndex == INDEX_NONE && PhysAsset)
	{
		for (const FBoneInfo& Bone : GetBones())
		{
			const int32 BodyIndex = PhysAsset->FindBodyIndex(FName(Bone.Name));
			if (BodyIndex != INDEX_NONE)
			{
				RootBodyIndex = BodyIndex;
				break;
			}
		}
	}
	return RootBodyIndex;
}

// UE SetRootBodyIndex: TransformToRoot = 루트 바디 기준 컴포넌트 Transform (UE GetRelativeTransform)
void USkeletalMeshComponent::SetRootBodyIndex(int32 InBodyIndex)
{
	if (InBodyIndex == INDEX_NONE) return;

	RootBodyData.BodyIndex = InBodyIndex;
	RootBodyData.TransformToRoot = FTransform();
	if (Bodies.Num() == 0) return;

	if (Bodies.IsValidIndex(InBodyIndex) && Bodies[InBodyIndex])
	{
		// 행 벡터 행렬이라 Comp = ToRoot * Body, 즉 ToRoot = Comp * Body^-1. 컴포넌트 스케일(0.01)은 ToRoot에 남는다
		const FMatrix BodyWorld = Bodies[InBodyIndex]->GetWorldTransform().GetLocalMatrix();
		RootBodyData.TransformToRoot = FTransform::FromMatrix(GetWorldMatrix() * BodyWorld.Inverse());
	}
	else
	{
		ResetRootBodyIndex();
	}
}

void USkeletalMeshComponent::ResetRootBodyIndex()
{
	RootBodyData.BodyIndex = INDEX_NONE;
	RootBodyData.TransformToRoot = FTransform();
}

// UE GetComponentTransformFromBodyInstance (SimulationUpatesComponentTransform 모드)
FTransform USkeletalMeshComponent::GetComponentTransformFromBodyInstance(FBodyInstance* UseBI)
{
	return FTransform::FromMatrix(RootBodyData.TransformToRoot.GetLocalMatrix() * UseBI->GetWorldTransform().GetLocalMatrix());
}

void USkeletalMeshComponent::SerializeSingleAnimation(FStructuredArchive::FRecord& Record)
{
	// 디테일 패널에서 고른 단일 애니메이션(SingleNodeInstance의 에셋)은 AnimInstance 객체가 아니라
	// 에셋 경로로 저장한다. 없으면 필드를 쓰지 않으므로 예전 씬도 그대로 읽힌다.
	FArchive& Ar = Record.GetUnderlyingArchive();
	UAnimSingleNodeInstance* Instance = GetSingleNodeInstance();
	UAnimationAsset* Asset = Instance ? Instance->GetCurrentAsset() : nullptr;
	const bool bHasAnimation = Ar.IsSaving() && Asset && !Asset->GetAssetPath().empty();

	TOptional<FStructuredArchive::FSlot> AnimSlot = Record.TryEnterField("SingleAnimation", bHasAnimation);
	if (!AnimSlot.IsSet())
		return;

	FStructuredArchive::FRecord AnimRecord = AnimSlot.GetValue().EnterRecord();
	FString Path = Ar.IsSaving() ? Asset->GetAssetPath() : FString();
	bool bLoop = Ar.IsSaving() ? Instance->IsLooping() : true;
	float Rate = Ar.IsSaving() ? Instance->GetPlayRate() : 1.0f;
	AnimRecord << SA_VALUE("Asset", Path) << SA_VALUE("Looping", bLoop) << SA_VALUE("PlayRate", Rate);

	if (!Ar.IsLoading())
		return;

	// AssetManager 캐시로 불러온다 (디테일 패널이 고를 때와 같은 객체)
	UAnimationAsset* Loaded = BlendSpaceFile::IsBlendSpacePath(Path)
		? static_cast<UAnimationAsset*>(UAssetManager::LoadBlendSpace(Path))
		: static_cast<UAnimationAsset*>(UAssetManager::LoadAnimSequence(Path));
	if (!Loaded)
	{
		HTR_LOG(Warning, "SkeletalMeshComponent: single animation not found: {}", Path);
		return;
	}

	// 재생 시작은 BeginPlay가 한다 (에디터 월드에서는 첫 프레임 포즈만 보인다)
	SetAnimation(Loaded);
	if (UAnimSingleNodeInstance* NewInstance = GetSingleNodeInstance())
	{
		NewInstance->SetLooping(bLoop);
		NewInstance->SetPlayRate(Rate);
	}
}
void USkeletalMeshComponent::OnRegister()
{
	Super::OnRegister();
	RefreshClothSolver();
}
void USkeletalMeshComponent::OnUnregister()
{
	ReleaseClothSolver();
	Super::OnUnregister();
}
void USkeletalMeshComponent::OnSkeletalMeshChanged()
{
	bBlueprintReloadRequested = !AnimBlueprintPath.empty();
	BoneSpaceTransforms.Reset();
	for (const FBoneInfo& Bone : GetBones())
		BoneSpaceTransforms.Add(Bone.LocalBindPose);
	RefreshClothSolver();
}
bool USkeletalMeshComponent::RequiresCPUSkinning() const
{
	// 현재 메시의 Cloth 정점을 직접 쓰는 CPU 스키닝 경로 선택
	return HasClothSimulation() && ClothSourceMesh == GetSkeletalMesh();
}
void USkeletalMeshComponent::RefreshClothSolver(bool bForPreview)
{
	UWorld* World = GetWorld();
	const USkeletalMesh* Mesh = GetSkeletalMesh();

	const bool bValidWorld = World && (bForPreview ? World->GetWorldType() == EWorldType::EditorPreview
		: World->IsGameWorld() && HasBegunPlay());

	if (!IsRegistered() || !bValidWorld || !Mesh)
	{
		ReleaseClothSolver();
		return;
	}

	// 게임 경로에서 동일 메시의 Solver 재사용
	if (!bForPreview && ClothSourceMesh == Mesh)
	{
		return;
	}

	// Solver 교체 중 기존 스키닝 모드 유지
	ReleaseClothSolver(false);
	const auto& Bindings = Mesh->GetClothSectionBindings();
	nv::cloth::Factory* Factory = NvClothSDK::GetFactory();
	if (Bindings.IsEmpty() || !Factory)
	{
		RefreshSkinningMode();
		return;
	}

	// 바인딩별 Cloth 생성과 초기 월드 중력 적용
	const FSkeletalMeshLODRenderData& LOD = Mesh->GetRenderData().LODRenderData[0];
	const float GravityZ = World->IsWorldGravityOverridden()
		? World->GetWorldGravityZ() : FPhysicsSettings::Get().DefaultGravityZ;
	TArray<FClothInstance> NewCloths;
	NewCloths.Reserve(Bindings.Num());

	for (const FClothSectionBinding& Binding : Bindings)
	{
		FClothInstance Instance = CreateClothInstance(*Factory, LOD, Binding, GravityZ, bForPreview);
		if (!Instance.Cloth)
		{
			HTR_LOG(Warning, "Failed to create cloth for section {}", Binding.SectionIndex);
			RefreshSkinningMode();
			return;
		}
		NewCloths.Add(std::move(Instance));
	}

	TUniquePtr<nv::cloth::Solver> NewSolver(Factory->createSolver());
	if (!NewSolver)
	{
		HTR_LOG(Warning, "Failed to create component cloth solver");
		RefreshSkinningMode();
		return;
	}

	// 준비된 Cloth의 등록과 틱 실행 연결
	for (const FClothInstance& Instance : NewCloths)
	{
		NewSolver->addCloth(Instance.Cloth.get());
	}

	ClothInstances = std::move(NewCloths);
	ClothSolver = std::move(NewSolver);
	ClothSourceMesh = Mesh;
	RefreshSkinningMode();
	if (!bForPreview)
	{
		ClothTickFunction.RegisterTickFunction(World->GetTickTaskManager());
	}
}
// 바인딩과 렌더 섹션으로부터 Cloth 인스턴스 생성
USkeletalMeshComponent::FClothInstance USkeletalMeshComponent::CreateClothInstance(
	nv::cloth::Factory& Factory, const FSkeletalMeshLODRenderData& LOD,
	const FClothSectionBinding& Binding, float GravityZ, bool bForPreview) const
{
	const FSkeletalMeshSection& Section = LOD.Sections[Binding.SectionIndex];
	FClothInstance Instance;

	// FBX 프리뷰 공간과 게임 월드의 미터 단위 통일
	const float WorldToCloth = bForPreview ? CmToWorld : 1.0f;
	Instance.SimulationScale = GetWorldScale3D() * WorldToCloth;
	const FVector & Scale = Instance.SimulationScale;
	if (Scale.X <= 0.0f || Scale.Y <= 0.0f || Scale.Z <= 0.0f)
	{
		return {};
	}

	// 전역 렌더 정점에서 연속 Cloth 입자로의 변환
	TArray<int32> RenderToParticle;
	RenderToParticle.Init(INDEX_NONE, LOD.BindVertices.Num());
	TArray<physx::PxVec3> Positions;
	TArray<uint32> Indices;
	Indices.Reserve(Section.IndexCount);
	const uint32 End = Section.StartIndex + Section.IndexCount;

	for (uint32 Index = Section.StartIndex; Index < End; ++Index)
	{
		const uint32 RenderVertex = LOD.Indices[Index];
		int32& Particle = RenderToParticle[RenderVertex];
		if (Particle == INDEX_NONE)
		{
			Particle = Positions.Num();
			Positions.Add(U2PVector(LOD.BindVertices[RenderVertex].Position * Scale));
			Instance.ParticleToRenderVertex.Add(RenderVertex);
		}
		Indices.Add(static_cast<uint32>(Particle));
	}

	// Windows CPU Fabric의 16비트 인덱스와 SIMD 패딩 상한
	constexpr uint32 MaxCpuParticles = (std::numeric_limits<uint16>::max)() - 7;
	const uint32 ParticleCount = static_cast<uint32>(Positions.Num());
	if (ParticleCount > MaxCpuParticles)
	{
		return {};
	}

	// 바인딩에서 지정한 고정점의 역질량 설정
	TArray<float> InverseMasses;
	InverseMasses.Init(1.0f, ParticleCount);
	uint32 MovableParticleCount = ParticleCount;
	for (const uint32 RenderVertex : Binding.PinnedRenderVertices)
	{
		float& InverseMass = InverseMasses[RenderToParticle[RenderVertex]];
		if (InverseMass > 0.0f)
		{
			InverseMass = 0.0f;
			--MovableParticleCount;
		}
	}
	// 이동 가능한 입자가 없는 섹션의 쿠킹 생략
	if (MovableParticleCount == 0)
	{
		return {};
	}

	// 현재 본 포즈를 추종할 고정점과 거리 제한의 입자별 반경
	if (bForPreview || !Binding.PinnedRenderVertices.IsEmpty() || !Binding.MaxDistanceByRenderVertex.IsEmpty())
	{
		constexpr float UnboundedRadius = (std::numeric_limits<float>::max)();
		Instance.MotionConstraintRadii.Init(UnboundedRadius, ParticleCount);
		for (const auto& [RenderVertex, MaxDistance] : Binding.MaxDistanceByRenderVertex)
		{
			Instance.MotionConstraintRadii[RenderToParticle[RenderVertex]] = MaxDistance;
		}
		for (const uint32 RenderVertex : Binding.PinnedRenderVertices)
		{
			Instance.MotionConstraintRadii[RenderToParticle[RenderVertex]] = 0.0f;
		}
	}

	// 추출한 위치, 삼각형, 역질량의 Fabric 쿠킹
	nv::cloth::ClothMeshDesc Desc;
	Desc.points.data = Positions.GetData();
	Desc.points.count = ParticleCount;
	Desc.points.stride = sizeof(physx::PxVec3);
	Desc.triangles.data = Indices.GetData();
	Desc.triangles.count = Indices.Num() / 3;
	Desc.triangles.stride = sizeof(uint32) * 3;
	Desc.invMasses.data = InverseMasses.GetData();
	Desc.invMasses.count = ParticleCount;
	Desc.invMasses.stride = sizeof(float);

	const physx::PxQuat Rotation = U2PQuat(GetWorldRotation().Quaternion());
	const physx::PxVec3 CookDown = Rotation.rotateInv(physx::PxVec3(0.0f, 0.0f, -1.0f));
	nv::cloth::Fabric* Fabric = NvClothCookFabricFromMesh(&Factory, Desc, CookDown, nullptr, false);
	if (!Fabric)
	{
		return {};
	}

	// 초기 입자 생성과 쿠킹 참조 반환
	TArray<physx::PxVec4> Particles;
	Particles.Reserve(ParticleCount);
	for (uint32 Particle = 0; Particle < ParticleCount; ++Particle)
	{
		Particles.Emplace(Positions[Particle], InverseMasses[Particle]);
	}

	const uint32 PhaseCount = Fabric->getNumPhases();
	TUniquePtr<nv::cloth::Cloth> Cloth(Factory.createCloth(
		{ Particles.GetData(), Particles.GetData() + ParticleCount }, *Fabric));
	Fabric->decRefCount();
	if (!Cloth)
	{
		return {};
	}

	// 기본 제약 그룹과 초기 월드 자세의 적용
	TArray<nv::cloth::PhaseConfig> Phases;
	Phases.Reserve(PhaseCount);
	for (uint32 Phase = 0; Phase < PhaseCount; ++Phase)
	{
		Phases.Emplace(static_cast<uint16>(Phase));
	}
	Cloth->setPhaseConfig({ Phases.GetData(), Phases.GetData() + Phases.Num() });
	Cloth->teleportToLocation(U2PVector(GetWorldLocation() * WorldToCloth), Rotation);
	Cloth->setGravity(physx::PxVec3(0.0f, 0.0f, GravityZ));
	ApplyClothWindCoefficients(*Cloth);
	ApplyClothFrictionAndDamping(*Cloth);
	Instance.Cloth = std::move(Cloth);
	ApplyClothSelfCollision(Instance, LOD);
	return Instance;
}

void USkeletalMeshComponent::ApplyClothSelfCollision(const FClothInstance& Instance,
	const FSkeletalMeshLODRenderData& LOD) const
{
	// 씬 파일과 에디터에서 받은 설정의 SDK 범위 보정
	const float Distance = std::isfinite(ClothSelfCollisionDistance)
		? std::max(0.0f, ClothSelfCollisionDistance) : 0.0f;
	const float Stiffness = std::isfinite(ClothSelfCollisionStiffness)
		? std::clamp(ClothSelfCollisionStiffness, 0.0f, 1.0f) : 0.0f;
	nv::cloth::Cloth& Cloth = *Instance.Cloth;
	if (Distance > 0.0f && Stiffness > 0.0f && Cloth.getNumRestPositions() == 0)
	{
		// 바인드 상태에서 이미 가까운 입자 쌍을 제외할 섹션 기준 자세
		TArray<physx::PxVec4> RestPositions;
		RestPositions.Reserve(Instance.ParticleToRenderVertex.Num());
		for (uint32 RenderVertex : Instance.ParticleToRenderVertex)
		{
			const FVector Position = LOD.BindVertices[RenderVertex].Position * Instance.SimulationScale;
			RestPositions.Emplace(U2PVector(Position), 0.0f);
		}
		Cloth.setRestPositions({RestPositions.GetData(), RestPositions.GetData() + RestPositions.Num()});

		// NvCloth CUDA의 부호 있는 16비트 셀 경계에 맞춘 대형 섹션 표본
		constexpr uint32 MaxCudaSelfCollisionParticles = (std::numeric_limits<int16>::max)();
		const uint32 ParticleCount = Cloth.getNumParticles();
		if (Cloth.getFactory().getPlatform() == nv::cloth::Platform::CUDA && ParticleCount > MaxCudaSelfCollisionParticles)
		{
			TArray<uint32> Indices;
			Indices.Reserve(MaxCudaSelfCollisionParticles);
			for (uint32 Index = 0; Index < MaxCudaSelfCollisionParticles; ++Index)
			{
				Indices.Add(Index * (ParticleCount - 1) / (MaxCudaSelfCollisionParticles - 1));
			}
			Cloth.setSelfCollisionIndices({Indices.GetData(), Indices.GetData() + Indices.Num()});
		}
	}
	Cloth.setSelfCollisionDistance(Distance);
	Cloth.setSelfCollisionStiffness(Stiffness);
}

void USkeletalMeshComponent::ApplyClothWindCoefficients(nv::cloth::Cloth& Cloth) const
{
	// 에셋과 Details의 항력·양력을 NvCloth 유효 범위로 제한
	Cloth.setDragCoefficient(std::clamp(ClothWindDragCoefficient, 0.0f, 1.0f));
	Cloth.setLiftCoefficient(std::clamp(ClothWindLiftCoefficient, 0.0f, 1.0f));
}

void USkeletalMeshComponent::ApplyClothFrictionAndDamping(nv::cloth::Cloth& Cloth) const
{
	Cloth.setFriction(std::isfinite(ClothFriction) ? std::clamp(ClothFriction, 0.0f, 1.0f) : 0.0f);
	const float Damping = std::isfinite(ClothDamping) ? std::clamp(ClothDamping, 0.0f, 1.0f) : 0.0f;
	Cloth.setDamping(physx::PxVec3(Damping));
}

void USkeletalMeshComponent::PostEditChangeProperty(const FProperty& Property)
{
	Super::PostEditChangeProperty(Property);
	if (Property.Name == "PhysicsAssetOverride")
	{
		LoadPhysicsAssetOverride();
		RecreatePhysicsState();
		return;
	}
	if (Property.Name == "ClothWindDragCoefficient" || Property.Name == "ClothWindLiftCoefficient")
	{
		// Solver 재생성 없이 실행 중인 섹션에 공기력 계수 반영
		for (const FClothInstance& Instance : ClothInstances)
		{
			ApplyClothWindCoefficients(*Instance.Cloth);
		}
		return;
	}
	if (Property.Name == "ClothFriction" || Property.Name == "ClothDamping")
	{
		for (const FClothInstance& Instance : ClothInstances)
		{
			ApplyClothFrictionAndDamping(*Instance.Cloth);
		}
		return;
	}
	if (!ClothSourceMesh
		|| (Property.Name != "ClothSelfCollisionDistance" && Property.Name != "ClothSelfCollisionStiffness"))
	{
		return;
	}

	// 현재 자세를 유지한 채 실행 중인 천에 편집값 반영
	const FSkeletalMeshLODRenderData& LOD = ClothSourceMesh->GetRenderData().LODRenderData[0];
	for (const FClothInstance& Instance : ClothInstances)
	{
		ApplyClothSelfCollision(Instance, LOD);
	}
}

void USkeletalMeshComponent::ForceClothNextUpdateTeleportAndReset()
{
	if (!HasClothSimulation()) return;
	// RefreshClothSolver는 같은 메시의 Solver를 재사용하므로 먼저 해제한다
	ReleaseClothSolver(false);
	RefreshClothSolver();
}

void USkeletalMeshComponent::ReleaseClothSolver(bool bRefreshSkinningMode)
{
	ClothTickFunction.UnRegisterTickFunction();
	for (const FClothInstance& Instance : ClothInstances)
	{
		ClothSolver->removeCloth(Instance.Cloth.get());
	}

	ClothInstances.Empty();
	// 해제된 Solver의 몸통 충돌 기록과 표시 데이터 초기화
	ActiveClothCollisionBody.clear();
	ClothCollisionSpheres.Reset();
	ClothCollisionCapsuleIndices.Reset();
	ClothCollisionPlanes.Reset();
	ClothCollisionConvexMasks.Reset();
	if (!ClothVertexPositions.IsEmpty())
	{
		ClothVertexPositions.Reset();
		MarkBoneTransformsDirty();
	}
	ClothSolver.reset();
	ClothSourceMesh = nullptr;
	ClothPreviewAccumulatedSeconds = 0.0;
	if (bRefreshSkinningMode)
	{
		RefreshSkinningMode();
	}
}
// 현재 본 포즈에 맞춘 Cloth 입자별 제약 구 갱신
void USkeletalMeshComponent::UpdateClothMotionConstraints()
{
	const USkeletalMesh& Mesh = *ClothSourceMesh;
	const TArray<FBoneInfo>& Bones = Mesh.GetRefSkeleton();
	if (ComponentSpaceTransforms.Num() != Bones.Num())
	{
		return;
	}

	constexpr float UnboundedRadius = (std::numeric_limits<float>::max)();
	const FSkeletalMeshLODRenderData& LOD = Mesh.GetRenderData().LODRenderData[0];
	TArray<FMatrix> RefToLocal;
	for (FClothInstance& Instance : ClothInstances)
	{
		if (Instance.MotionConstraintRadii.IsEmpty())
		{
			continue;
		}

		// 컴포넌트의 Cloth가 공유하는 현재 스키닝 행렬
		if (RefToLocal.IsEmpty())
		{
			RefToLocal.SetNum(Bones.Num());
			for (int32 BoneIndex = 0; BoneIndex < Bones.Num(); ++BoneIndex)
			{
				RefToLocal[BoneIndex] = Bones[BoneIndex].InverseBindPose * ComponentSpaceTransforms[BoneIndex];
			}
		}

		nv::cloth::Cloth& Cloth = *Instance.Cloth;
		const bool bFirstUpdate = Cloth.getNumMotionConstraints() == 0;
		auto Constraints = Cloth.getMotionConstraints();
		for (int32 ParticleIndex = 0; ParticleIndex < Instance.ParticleToRenderVertex.Num(); ++ParticleIndex)
		{
			const float Radius = Instance.MotionConstraintRadii[ParticleIndex];
			if (!bFirstUpdate && Radius == UnboundedRadius)
			{
				// 이동 제한이 없는 입자의 전체 배열 기록
				Constraints.begin()[ParticleIndex] = physx::PxVec4(0.0f, 0.0f, 0.0f, Radius);
				continue;
			}
			const uint32 RenderVertex = Instance.ParticleToRenderVertex[ParticleIndex];
			const FMatrix SkinMatrix = LOD.SkinWeights[RenderVertex].BuildSkinMatrix(RefToLocal);
			const FVector SkinnedPosition = SkinMatrix.TransformPosition(LOD.BindVertices[RenderVertex].Position);
			const physx::PxVec3 Center = U2PVector(SkinnedPosition * Instance.SimulationScale);
			Constraints.begin()[ParticleIndex] = physx::PxVec4(Center, Radius);
		}

		// 첫 스텝의 현재·이전 위치 일치와 역질량 보존
		if (bFirstUpdate)
		{
			auto Current = Cloth.getCurrentParticles();
			auto Previous = Cloth.getPreviousParticles();
			for (int32 ParticleIndex = 0; ParticleIndex < Instance.ParticleToRenderVertex.Num(); ++ParticleIndex)
			{
				const physx::PxVec3 Center = Constraints.begin()[ParticleIndex].getXYZ();
				physx::PxVec4& CurrentParticle = Current.begin()[ParticleIndex];
				physx::PxVec4& PreviousParticle = Previous.begin()[ParticleIndex];
				CurrentParticle = physx::PxVec4(Center, CurrentParticle.w);
				PreviousParticle = physx::PxVec4(Center, PreviousParticle.w);
			}
		}
	}
}

void USkeletalMeshComponent::BuildClothBodyCollision()
{
	// 현재 본 포즈에서 다시 계산할 충돌 배열의 초기화
	ClothCollisionSpheres.Reset();
	ClothCollisionCapsuleIndices.Reset();
	ClothCollisionPlanes.Reset();
	ClothCollisionConvexMasks.Reset();

	const UPhysicsAsset* Asset = ClothCollisionBody.empty() ? nullptr : GetPhysicsAsset();
	const int32 BodyIndex = Asset ? Asset->FindBodyIndex(FName(ClothCollisionBody)) : INDEX_NONE;
	if (BodyIndex == INDEX_NONE)
	{
		return;
	}

	const USkeletalBodySetup& Body = *Asset->SkeletalBodySetups[BodyIndex];
	const int32 BoneIndex = FindBoneIndex(Body.BoneName.ToString());
	if (!ComponentSpaceTransforms.IsValidIndex(BoneIndex))
	{
		return;
	}

	// 본 로컬에서 Cloth 공간으로 가는 균일 스케일의 확인
	const FMatrix& BoneMatrix = ComponentSpaceTransforms[BoneIndex];
	const FVector& Scale = ClothInstances[0].SimulationScale;
	const FVector X = BoneMatrix.TransformVector(FVector(1.0f, 0.0f, 0.0f)) * Scale;
	const FVector Y = BoneMatrix.TransformVector(FVector(0.0f, 1.0f, 0.0f)) * Scale;
	const FVector Z = BoneMatrix.TransformVector(FVector(0.0f, 0.0f, 1.0f)) * Scale;
	const float ScaleSquared = X.SizeSquared();
	const float Tolerance = ScaleSquared * KINDA_SMALL_NUMBER;
	if (ScaleSquared <= SMALL_NUMBER
		|| !FMath::IsNearlyEqual(Y.SizeSquared(), ScaleSquared, Tolerance)
		|| !FMath::IsNearlyEqual(Z.SizeSquared(), ScaleSquared, Tolerance)
		|| !FMath::IsNearlyZero(X.Dot(Y), Tolerance)
		|| !FMath::IsNearlyZero(X.Dot(Z), Tolerance)
		|| !FMath::IsNearlyZero(Y.Dot(Z), Tolerance))
	{
		return;
	}

	const float UniformScale = std::sqrt(ScaleSquared);

	// 독립 구의 중심과 반경 등록
	for (const FKSphereElem& Sphere : Body.AggGeom.SphereElems)
	{
		if (ClothCollisionSpheres.Num() == MaxClothCollisionSpheres)
		{
			break;
		}
		if (Sphere.Radius > 0.0f)
		{
			const FVector Center = BoneMatrix.TransformPosition(Sphere.Center) * Scale;
			ClothCollisionSpheres.Emplace(Center, Sphere.Radius * UniformScale);
		}
	}

	// 캡슐의 양 끝 구와 인덱스 쌍 등록
	for (const FKSphylElem& Capsule : Body.AggGeom.SphylElems)
	{
		if (Capsule.Radius <= 0.0f || Capsule.Length < 0.0f)
		{
			continue;
		}

		const FVector Center = BoneMatrix.TransformPosition(Capsule.Center) * Scale;
		const float Radius = Capsule.Radius * UniformScale;
		const float HalfLength = Capsule.Length * UniformScale * 0.5f;
		if (HalfLength <= KINDA_SMALL_NUMBER)
		{
			if (ClothCollisionSpheres.Num() < MaxClothCollisionSpheres)
			{
				ClothCollisionSpheres.Emplace(Center, Radius);
			}
			continue;
		}
		if (ClothCollisionSpheres.Num() + 2 > MaxClothCollisionSpheres)
		{
			continue;
		}

		const FVector LocalAxis = Capsule.Rotation.Quaternion().RotateVector(FVector(0.0f, 0.0f, 1.0f));
		const FVector Axis = (BoneMatrix.TransformVector(LocalAxis) * Scale).GetSafeNormal();
		const FVector HalfSegment = Axis * HalfLength;
		const uint32 StartIndex = static_cast<uint32>(ClothCollisionSpheres.Num());
		ClothCollisionSpheres.Emplace(Center - HalfSegment, Radius);
		ClothCollisionSpheres.Emplace(Center + HalfSegment, Radius);
		ClothCollisionCapsuleIndices.Add(StartIndex);
		ClothCollisionCapsuleIndices.Add(StartIndex + 1);
	}

	// 박스의 바깥쪽 평면 여섯 개와 볼록체 마스크 등록
	for (const FKBoxElem& Box : Body.AggGeom.BoxElems)
	{
		if (ClothCollisionPlanes.Num() + ClothBoxPlaneCount > MaxClothCollisionPlanes)
		{
			break;
		}
		if (Box.X <= 0.0f || Box.Y <= 0.0f || Box.Z <= 0.0f)
		{
			continue;
		}

		const FVector Center = BoneMatrix.TransformPosition(Box.Center) * Scale;
		const FVector HalfExtent = FVector(Box.X, Box.Y, Box.Z) * (UniformScale * 0.5f);
		const FQuat Rotation = Box.Rotation.Quaternion();
		const uint32 FirstPlane = static_cast<uint32>(ClothCollisionPlanes.Num());
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			FVector LocalAxis = FVector::ZeroVector;
			LocalAxis[AxisIndex] = 1.0f;
			const FVector BoneAxis = Rotation.RotateVector(LocalAxis);
			const FVector Normal = (BoneMatrix.TransformVector(BoneAxis) * Scale).GetSafeNormal();
			const float CenterProjection = Normal.Dot(Center);
			const float Extent = HalfExtent[AxisIndex];
			ClothCollisionPlanes.Emplace(Normal, -CenterProjection - Extent);
			ClothCollisionPlanes.Emplace(-Normal, CenterProjection - Extent);
		}

		const uint32 BoxMask = (1u << ClothBoxPlaneCount) - 1u;
		ClothCollisionConvexMasks.Add(BoxMask << FirstPlane);
	}
}

void USkeletalMeshComponent::UploadClothBodyCollision()
{
	// 엔진 배열에서 NvCloth 입력 배열로 변환
	physx::PxVec4 Spheres[MaxClothCollisionSpheres];
	physx::PxVec4 Planes[MaxClothCollisionPlanes];
	for (int32 Index = 0; Index < ClothCollisionSpheres.Num(); ++Index)
	{
		const FVector4& Sphere = ClothCollisionSpheres[Index];
		Spheres[Index] = physx::PxVec4(Sphere.X, Sphere.Y, Sphere.Z, Sphere.W);
	}
	for (int32 Index = 0; Index < ClothCollisionPlanes.Num(); ++Index)
	{
		const FVector4& Plane = ClothCollisionPlanes[Index];
		Planes[Index] = physx::PxVec4(Plane.X, Plane.Y, Plane.Z, Plane.W);
	}

	const uint32 SphereCount = static_cast<uint32>(ClothCollisionSpheres.Num());
	const uint32 PlaneCount = static_cast<uint32>(ClothCollisionPlanes.Num());
	const uint32 CapsuleCount = static_cast<uint32>(ClothCollisionCapsuleIndices.Num() / 2);
	const uint32 ConvexCount = static_cast<uint32>(ClothCollisionConvexMasks.Num());
	const bool bBodyChanged = ActiveClothCollisionBody != ClothCollisionBody;

	for (FClothInstance& Instance : ClothInstances)
	{
		nv::cloth::Cloth& Cloth = *Instance.Cloth;
		const bool bRebuild = bBodyChanged
			|| Cloth.getNumSpheres() != SphereCount || Cloth.getNumCapsules() != CapsuleCount
			|| Cloth.getNumPlanes() != PlaneCount || Cloth.getNumConvexes() != ConvexCount;

		if (bRebuild)
		{
			// 구와 평면을 참조하는 인덱스와 마스크의 우선 해제
			if (Cloth.getNumCapsules() > 0)
			{
				Cloth.setCapsules({}, 0, Cloth.getNumCapsules());
			}
			if (Cloth.getNumConvexes() > 0)
			{
				Cloth.setConvexes({}, 0, Cloth.getNumConvexes());
			}
			if (Cloth.getNumSpheres() > 0)
			{
				Cloth.setSpheres({}, 0, Cloth.getNumSpheres());
			}
			if (Cloth.getNumPlanes() > 0)
			{
				Cloth.setPlanes({}, 0, Cloth.getNumPlanes());
			}
		}

		// 현재 본 포즈의 구와 평면 갱신
		if (SphereCount > 0)
		{
			Cloth.setSpheres({Spheres, Spheres + SphereCount}, 0, Cloth.getNumSpheres());
		}
		if (PlaneCount > 0)
		{
			Cloth.setPlanes({Planes, Planes + PlaneCount}, 0, Cloth.getNumPlanes());
		}

		// 구와 평면 등록 이후의 형상 연결
		if (bRebuild && CapsuleCount > 0)
		{
			const uint32* Indices = ClothCollisionCapsuleIndices.GetData();
			Cloth.setCapsules({Indices, Indices + ClothCollisionCapsuleIndices.Num()}, 0, 0);
		}
		if (bRebuild && ConvexCount > 0)
		{
			const uint32* Masks = ClothCollisionConvexMasks.GetData();
			Cloth.setConvexes({Masks, Masks + ConvexCount}, 0, 0);
		}
	}

	ActiveClothCollisionBody = ClothCollisionBody;
}

void USkeletalMeshComponent::RemoveClothWorldCollision(nv::cloth::Cloth& Cloth) const
{
	// 직전 프레임 몸통 배열의 끝 인덱스
	const uint32 BodySpheres = static_cast<uint32>(ClothCollisionSpheres.Num());
	const uint32 BodyPlanes = static_cast<uint32>(ClothCollisionPlanes.Num());
	const uint32 BodyCapsules = static_cast<uint32>(ClothCollisionCapsuleIndices.Num() / 2);
	const uint32 BodyConvexes = static_cast<uint32>(ClothCollisionConvexMasks.Num());

	// 월드 캡슐과 볼록체의 참조 우선 해제
	if (Cloth.getNumCapsules() > BodyCapsules)
	{
		Cloth.setCapsules({}, BodyCapsules, Cloth.getNumCapsules());
	}
	if (Cloth.getNumConvexes() > BodyConvexes)
	{
		Cloth.setConvexes({}, BodyConvexes, Cloth.getNumConvexes());
	}
	if (Cloth.getNumSpheres() > BodySpheres)
	{
		Cloth.setSpheres({}, BodySpheres, Cloth.getNumSpheres());
	}
	if (Cloth.getNumPlanes() > BodyPlanes)
	{
		Cloth.setPlanes({}, BodyPlanes, Cloth.getNumPlanes());
	}
}

void USkeletalMeshComponent::AppendClothWorldCollision(nv::cloth::Cloth& Cloth, const physx::PxTransform& ClothToWorld,
	const TArray<FPhysicsOverlapShape>& Shapes) const
{
	const physx::PxTransform WorldToCloth = ClothToWorld.getInverse();
	const float Thickness = std::isfinite(ClothCollisionThickness) ? std::max(0.0f, ClothCollisionThickness) : 0.0f;
	// 몸통 뒤의 남은 슬롯에만 기록할 월드 충돌 데이터
	const uint32 SphereBase = Cloth.getNumSpheres();
	const uint32 PlaneBase = Cloth.getNumPlanes();
	physx::PxVec4 Spheres[MaxClothCollisionSpheres];
	physx::PxVec4 Planes[MaxClothCollisionPlanes];
	uint32 CapsuleIndices[MaxClothCollisionSpheres];
	uint32 ConvexMasks[MaxClothCollisionPlanes / ClothBoxPlaneCount];
	uint32 SphereCount = 0;
	uint32 PlaneCount = 0;
	uint32 CapsuleIndexCount = 0;
	uint32 ConvexCount = 0;

	for (const FPhysicsOverlapShape& Shape : Shapes)
	{
		const physx::PxTransform Local = WorldToCloth * Shape.WorldPose;
		switch (Shape.Geometry.getType())
		{
		case physx::PxGeometryType::eSPHERE:
		{
			if (SphereBase + SphereCount < MaxClothCollisionSpheres)
			{
				Spheres[SphereCount++] = physx::PxVec4(Local.p, Shape.Geometry.sphere().radius + Thickness);
			}
			break;
		}
		case physx::PxGeometryType::eCAPSULE:
		{
			if (SphereBase + SphereCount + 2 <= MaxClothCollisionSpheres)
			{
				// PhysX 캡슐 로컬 X축의 양 끝 구와 절대 슬롯 번호
				const auto& Capsule = Shape.Geometry.capsule();
				const physx::PxVec3 Offset = Local.q.getBasisVector0() * Capsule.halfHeight;
				const uint32 FirstSphere = SphereBase + SphereCount;
				Spheres[SphereCount++] = physx::PxVec4(Local.p - Offset, Capsule.radius + Thickness);
				Spheres[SphereCount++] = physx::PxVec4(Local.p + Offset, Capsule.radius + Thickness);
				CapsuleIndices[CapsuleIndexCount++] = FirstSphere;
				CapsuleIndices[CapsuleIndexCount++] = FirstSphere + 1;
			}
			break;
		}
		case physx::PxGeometryType::eBOX:
		{
			if (PlaneBase + PlaneCount + ClothBoxPlaneCount <= MaxClothCollisionPlanes)
			{
				// 회전된 박스의 바깥쪽 평면 여섯 개와 절대 평면 마스크
				const uint32 FirstPlane = PlaneBase + PlaneCount;
				const auto& Extents = Shape.Geometry.box().halfExtents;
				const physx::PxMat33 Axes(Local.q);
				for (uint32 Axis = 0; Axis < 3; ++Axis)
				{
					const physx::PxVec3& Normal = Axes[Axis];
					const float Projection = Normal.dot(Local.p);
					Planes[PlaneCount++] = physx::PxVec4(Normal, -Projection - Extents[Axis] - Thickness);
					Planes[PlaneCount++] = physx::PxVec4(-Normal, Projection - Extents[Axis] - Thickness);
				}
				ConvexMasks[ConvexCount++] = ((1u << ClothBoxPlaneCount) - 1u) << FirstPlane;
			}
			break;
		}
		default:
		{
			break;
		}
		}
	}

	// 실제 슬롯 생성 이후의 캡슐 인덱스와 평면 마스크 연결
	if (SphereCount > 0)
	{
		Cloth.setSpheres({Spheres, Spheres + SphereCount}, SphereBase, SphereBase);
	}
	if (PlaneCount > 0)
	{
		Cloth.setPlanes({Planes, Planes + PlaneCount}, PlaneBase, PlaneBase);
	}
	if (CapsuleIndexCount > 0)
	{
		const uint32 First = Cloth.getNumCapsules();
		Cloth.setCapsules({CapsuleIndices, CapsuleIndices + CapsuleIndexCount}, First, First);
	}
	if (ConvexCount > 0)
	{
		const uint32 First = Cloth.getNumConvexes();
		Cloth.setConvexes({ConvexMasks, ConvexMasks + ConvexCount}, First, First);
	}
}

void USkeletalMeshComponent::DrawClothBodyCollision(FLineBatcher& LineBatcher) const
{
	if (ClothInstances.IsEmpty())
	{
		return;
	}

	// Cloth 공간에서 뷰포트 월드 공간으로 돌아가는 행렬
	const FVector& Scale = ClothInstances[0].SimulationScale;
	FMatrix ClothToComponent = FMatrix::Identity;
	ClothToComponent.M[0][0] = 1.0f / Scale.X;
	ClothToComponent.M[1][1] = 1.0f / Scale.Y;
	ClothToComponent.M[2][2] = 1.0f / Scale.Z;
	const FMatrix ClothToWorld = ClothToComponent * GetWorldMatrix();
	const float RadiusScale = ClothToWorld.TransformVector(FVector(1.0f, 0.0f, 0.0f)).Size();
	const FVector4 Color(0.2f, 1.0f, 0.4f, 1.0f);

	// 독립 구와 인덱스로 연결된 캡슐의 표시
	int32 CapsuleIndex = 0;
	for (int32 Index = 0; Index < ClothCollisionSpheres.Num(); ++Index)
	{
		const FVector4& Sphere = ClothCollisionSpheres[Index];
		const FVector A = ClothToWorld.TransformPosition(FVector(Sphere.X, Sphere.Y, Sphere.Z));
		const float Radius = Sphere.W * RadiusScale;
		if (CapsuleIndex >= ClothCollisionCapsuleIndices.Num()
			|| ClothCollisionCapsuleIndices[CapsuleIndex] != static_cast<uint32>(Index))
		{
			LineBatcher.AddSphere(A, Radius, Color);
			continue;
		}

		Index = static_cast<int32>(ClothCollisionCapsuleIndices[CapsuleIndex + 1]);
		CapsuleIndex += 2;
		const FVector4& End = ClothCollisionSpheres[Index];
		const FVector B = ClothToWorld.TransformPosition(FVector(End.X, End.Y, End.Z));
		const FVector Segment = B - A;
		const float Length = Segment.Size();
		if (Length <= KINDA_SMALL_NUMBER)
		{
			LineBatcher.AddSphere(A, Radius, Color);
			continue;
		}

		const FVector Up = Segment / Length;
		const FVector Reference = std::abs(Up.Z) < 0.99f
			? FVector(0.0f, 0.0f, 1.0f) : FVector(1.0f, 0.0f, 0.0f);
		const FVector Right = Reference.Cross(Up).GetSafeNormal();
		LineBatcher.AddCapsule((A + B) * 0.5f, Up, Right, Radius, Length * 0.5f + Radius, Color);
	}

	// 마주 보는 평면 쌍에서 박스 중심과 반크기 복원
	for (int32 FirstPlane = 0; FirstPlane < ClothCollisionPlanes.Num(); FirstPlane += ClothBoxPlaneCount)
	{
		FMatrix BoxToCloth = FMatrix::Identity;
		FVector Center = FVector::ZeroVector;
		FVector HalfExtent = FVector::ZeroVector;
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			const FVector4& Positive = ClothCollisionPlanes[FirstPlane + AxisIndex * 2];
			const FVector4& Negative = ClothCollisionPlanes[FirstPlane + AxisIndex * 2 + 1];
			const FVector Normal(Positive.X, Positive.Y, Positive.Z);
			Center += Normal * ((Negative.W - Positive.W) * 0.5f);
			HalfExtent[AxisIndex] = -(Positive.W + Negative.W) * 0.5f;
			BoxToCloth.SetAxis(AxisIndex, Normal);
		}
		BoxToCloth.SetOrigin(Center);
		LineBatcher.AddOrientedBox(FBox{-HalfExtent, HalfExtent}, BoxToCloth * ClothToWorld, Color);
	}
}

// 시뮬레이션 입자의 메시 로컬 위치 수집
void USkeletalMeshComponent::UpdateClothVertexPositions()
{
	ClothVertexPositions.Reset();
	for (const FClothInstance& Instance : ClothInstances)
	{
		const auto Particles = nv::cloth::readCurrentParticles(*Instance.Cloth);
		for (uint32 Index = 0; Index < Particles.size(); ++Index)
		{
			const physx::PxVec4& Particle = Particles.begin()[Index];
			const FVector Position = FVector(Particle.x, Particle.y, Particle.z)
				/ Instance.SimulationScale;
			ClothVertexPositions.Add({ Instance.ParticleToRenderVertex[Index], Position });
		}
	}
}
bool USkeletalMeshComponent::RestartClothPreview()
{
	const UWorld* World = GetWorld();

	// 프리뷰 월드와 렌더 객체 확인
	if (!World || World->GetWorldType() != EWorldType::EditorPreview || !GetMeshObject())
	{
		return false;
	}

	RefreshClothSolver(true);
	RefreshBoneTransformsNow();
	return HasClothSimulation();
}
void USkeletalMeshComponent::StopClothPreview()
{
	if (!HasClothSimulation()){	return; }
	// 변형 결과 제거와 현재 본 포즈의 렌더 버퍼 복원
	ReleaseClothSolver();
	RefreshBoneTransformsNow();
}
void USkeletalMeshComponent::TickClothPreview(float DeltaTime)
{
	if (!HasClothSimulation() || DeltaTime <= 0.0f)
	{
		return;
	}

	// 뷰어 프레임 시간의 고정 스텝 변환과 따라잡기 상한
	const FPhysicsSettings& Settings = FPhysicsSettings::Get();
	const double FixedStepSeconds = Settings.FixedStepSeconds;
	ClothPreviewAccumulatedSeconds = std::min(
		ClothPreviewAccumulatedSeconds + static_cast<double>(DeltaTime),
		FixedStepSeconds * Settings.MaxStepsPerTick);

	int32 StepCount = 0;
	while (StepCount < Settings.MaxStepsPerTick
		&& ClothPreviewAccumulatedSeconds >= FixedStepSeconds)
	{
		ClothPreviewAccumulatedSeconds -= FixedStepSeconds;
		++StepCount;
	}

	AdvanceCloth(StepCount, static_cast<float>(FixedStepSeconds));
}
void USkeletalMeshComponent::TickClothing()
{
	const UWorld* World = GetWorld();
	const FPhysicsScene* PhysicsScene = World ? World->GetPhysicsScene() : nullptr;
	if (!ClothSolver || !HasBegunPlay() || !PhysicsScene)
	{
		return;
	}

	// 완료된 게임 월드 물리 스텝의 Cloth 전달
	AdvanceCloth(PhysicsScene->GetLastTickStepCount(),	static_cast<float>(FPhysicsSettings::Get().FixedStepSeconds));
}

void USkeletalMeshComponent::AdvanceCloth(int32 StepCount, float StepDelta)
{
	// 고정 스텝 사이의 기존 Cloth 자세와 렌더 정점 유지
	if (ClothInstances.IsEmpty() || (StepCount == 0 && !ClothVertexPositions.IsEmpty()))
	{
		return;
	}
	UpdateClothMotionConstraints();

	// 섹션과 고정 스텝이 공유하는 월드 좌표계 풍속(m/s)
	const UWorld* World = GetWorld();
	const bool bUsesWind = ClothWindDragCoefficient > 0.0f || ClothWindLiftCoefficient > 0.0f;
	const physx::PxVec3 WindVelocity = World && bUsesWind ? U2PVector(World->GetWindVelocity()) : physx::PxVec3(0.0f);

	// 이전 몸통 개수로 월드 슬롯을 분리한 뒤 새 몸통 갱신
	for (FClothInstance& Instance : ClothInstances)
	{
		Instance.Cloth->setWindVelocity(WindVelocity);
		RemoveClothWorldCollision(*Instance.Cloth);
	}
	BuildClothBodyCollision();
	UploadClothBodyCollision();

	const float WorldToCloth = World && World->GetWorldType() == EWorldType::EditorPreview ? CmToWorld : 1.0f;
	const physx::PxVec3 TargetLocation = U2PVector(GetWorldLocation() * WorldToCloth);
	const FQuat TargetRotation = GetWorldRotation().Quaternion();
	// 같은 컴포넌트에서 생성되고 함께 이동하는 모든 섹션의 공통 기준 자세
	const nv::cloth::Cloth& FirstCloth = *ClothInstances[0].Cloth;
	const physx::PxVec3 StartLocation = FirstCloth.getTranslation();
	const FQuat StartRotation = P2UQuat(FirstCloth.getRotation());
	const auto GetStepPose = [&](int32 StepIndex)
	{
		const float Alpha = static_cast<float>(StepIndex + 1) / static_cast<float>(StepCount);
		return physx::PxTransform(TargetLocation + (StartLocation - TargetLocation) * (1.0f - Alpha),
			U2PQuat(FMath::Slerp(StartRotation, TargetRotation, Alpha)));
	};

	TArray<TArray<FPhysicsOverlapShape>> WorldShapes;
	const FPhysicsScene* PhysicsScene = bCollideClothWithEnvironment && World && World->IsGameWorld()
		? World->GetPhysicsScene() : nullptr;
	if (PhysicsScene && StepCount > 0)
	{
		// 이동·회전하는 섹션의 스텝별 바운드를 합쳐 프레임당 한 번 검색
		constexpr float QueryPaddingMeters = 0.10f;
		WorldShapes.SetNum(ClothInstances.Num());
		for (int32 Section = 0; Section < ClothInstances.Num(); ++Section)
		{
			const nv::cloth::Cloth& Cloth = *ClothInstances[Section].Cloth;
			const auto LocalBounds = physx::PxBounds3::centerExtents(Cloth.getBoundingBoxCenter(), Cloth.getBoundingBoxScale());
			auto WorldBounds = physx::PxBounds3::empty();
			for (int32 Step = 0; Step < StepCount; ++Step)
			{
				WorldBounds.include(physx::PxBounds3::transformFast(GetStepPose(Step), LocalBounds));
			}
			const physx::PxBoxGeometry Box(WorldBounds.getExtents() + physx::PxVec3(QueryPaddingMeters));
			PhysicsScene->GatherSimulationShapes(Box, physx::PxTransform(WorldBounds.getCenter()), GetOwner(), WorldShapes[Section]);
		}
	}
	for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
	{
		// 강체의 월드 자세를 해당 고정 스텝의 Cloth 좌표계로 변환
		const physx::PxTransform StepPose = GetStepPose(StepIndex);
		for (int32 Section = 0; Section < ClothInstances.Num(); ++Section)
		{
			nv::cloth::Cloth& Cloth = *ClothInstances[Section].Cloth;
			const bool bUpdateWorldShapes = StepIndex == 0
				|| Cloth.getTranslation() != StepPose.p || !(Cloth.getRotation() == StepPose.q);
			Cloth.setTranslation(StepPose.p);
			Cloth.setRotation(StepPose.q);
			if (bUpdateWorldShapes && !WorldShapes.IsEmpty() && !WorldShapes[Section].IsEmpty())
			{
				if (StepIndex > 0)
				{
					RemoveClothWorldCollision(Cloth);
				}
				AppendClothWorldCollision(Cloth, StepPose, WorldShapes[Section]);
			}
		}

		// 계산 대상이 있는 스텝의 시작
		if (!ClothSolver->beginSimulation(StepDelta))
		{
			return;
		}

		// 현재 스텝의 전체 청크 순차 계산
		const int32 ChunkCount = ClothSolver->getSimulationChunkCount();
		for (int32 ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
		{
			ClothSolver->simulateChunk(ChunkIndex);
		}

		// 모든 청크 계산 이후의 스텝 완료
		ClothSolver->endSimulation();
	}

	// 첫 표시 또는 마지막 스텝의 렌더 정점 반영
	UpdateClothVertexPositions();
	if (GetMeshObject())
	{
		SendRenderDynamicData();
	}
}
void USkeletalMeshComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();
	InitArticulated(GetWorld()->GetPhysicsScene());
}
void USkeletalMeshComponent::OnDestroyPhysicsState()
{
	TermArticulated();
	Super::OnDestroyPhysicsState();
}
void USkeletalMeshComponent::InstantiatePhysicsAssetBodies_Internal(const UPhysicsAsset& PhysAsset, FPhysicsScene& PhysScene)
{
	physx::PxSetGroupCollisionFlag(RagdollCollisionGroup, RagdollCollisionGroup, false);

	Bodies.Init(nullptr, PhysAsset.SkeletalBodySetups.Num());
	for (int32 i = 0; i < PhysAsset.SkeletalBodySetups.Num(); ++i)
	{
		const USkeletalBodySetup* Setup = PhysAsset.SkeletalBodySetups[i];
		const int32 Bone = FindBoneIndex(Setup->BoneName.ToString());
		if (Bone == INDEX_NONE) continue;

		const EPhysicsBodyType Type = Setup->PhysicsType == PhysType_Simulated ? EPhysicsBodyType::Dynamic : EPhysicsBodyType::Kinematic;

		FBodyInstance* BI = new FBodyInstance();
		BI->InstanceBodyIndex = i;
		BI->InstanceBoneIndex = Bone;
		if (!BI->InitBody(*Setup, FTransform::FromMatrix(GetBoneWorldMatrix(Bone)), this, PhysScene, Type))
		{
			HTR_LOG(Warning, "Ragdoll body failed: {}", Setup->BoneName.ToString());
			delete BI;
			continue;
		}
		if (physx::PxRigidDynamic* Dyn = BI->GetRigidDynamic())
		{
			// 등록된 래그돌 Shape 필터 갱신의 쓰기 잠금
			const physx::PxSceneWriteLock WriteLock(*PhysScene.GetPxScene());
			physx::PxSetGroup(*Dyn, RagdollCollisionGroup);
		}
		Bodies[i] = BI;
	}
}
void USkeletalMeshComponent::UpdateKinematicBonesToAnim()
{
	if (bBlendPhysics) return;
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->SetKinematicTarget(FTransform::FromMatrix(GetBoneWorldMatrix(BI->InstanceBoneIndex)));
		}
	}
}
void USkeletalMeshComponent::BlendPhysicsBones()
{
	const TArray<FBoneInfo>& Bones = GetBones();
	if (BoneSpaceTransforms.Num() != Bones.Num()) return;

	const FMatrix CompWorld = GetWorldMatrix();
	const FMatrix InvComp = CompWorld.Inverse();
	const FVector CompScale = FTransform::FromMatrix(CompWorld).Scale;

	TArray<FMatrix> Comp;
	Comp.SetNum(Bones.Num());
	TArray<uint8> bFromPhys;
	bFromPhys.Init(0, Bones.Num());

	for (FBodyInstance* BI : Bodies)
	{
		if (!BI || !BI->IsValidBodyInstance()) continue;
		FTransform BodyWorld = BI->GetWorldTransform();
		// PhysX 포즈에는 스케일이 없다. 바인드 포즈의 컴포넌트 공간 스케일(UAL Armature ×100)에 컴포넌트 스케일을 곱해 붙인다.
		// 현재 본 스케일을 다시 읽으면 매 프레임 분해 오차가 쌓여 메시가 계속 늘어난다
		const FVector RefScale = FTransform::FromMatrix(Bones[BI->InstanceBoneIndex].InverseBindPose.Inverse()).Scale;
		BodyWorld.Scale = FVector(RefScale.X * CompScale.X, RefScale.Y * CompScale.Y, RefScale.Z * CompScale.Z);
		Comp[BI->InstanceBoneIndex] = BodyWorld.GetLocalMatrix() * InvComp;
		bFromPhys[BI->InstanceBoneIndex] = 1;
	}

	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		const int32 P = Bones[i].ParentIndex;
		if (!bFromPhys[i])
		{
			Comp[i] = P < 0 ? BoneSpaceTransforms[i].GetLocalMatrix() : BoneSpaceTransforms[i].GetLocalMatrix() * Comp[P];
		}
		else if (P >= 0)
		{
			BoneSpaceTransforms[i] = FTransform::FromMatrix(Comp[i] * Comp[P].Inverse());
		}
		else
		{
			BoneSpaceTransforms[i] = FTransform::FromMatrix(Comp[i]);
		}
	}
	MarkBoneTransformsDirty();
}
void USkeletalMeshComponent::RegisterEndPhysicsTick(bool bRegister)
{
	if (!bRegister)
	{
		EndPhysicsTickFunction.UnRegisterTickFunction();
		return;
	}
	if (UWorld* World = GetWorld(); World && !EndPhysicsTickFunction.IsTickFunctionRegistered())
		EndPhysicsTickFunction.RegisterTickFunction(World->GetTickTaskManager());
}

void USkeletalMeshComponent::EndPhysicsTickComponent(float DeltaTime)
{
	// UE EndPhysicsTickComponent: 먼저 컴포넌트를 루트 바디로 옮기고, 옮겨진 컴포넌트 기준으로 본을 맞춘다
	SyncComponentToRBPhysics();   // bSimulatePhysics가 아니면 안에서 바로 return
	if (!bBlendPhysics) return;
	BlendPhysicsBones();
	RefreshBoneTransformsNow();  // 같은 프레임에 스키닝까지 (UE도 여기서 본 변환을 확정한다)
}

UAnimSingleNodeInstance* USkeletalMeshComponent::GetSingleNodeInstance() const
{
	return Cast<UAnimSingleNodeInstance>(AnimInstance);
}

void USkeletalMeshComponent::SetAnimation(UAnimationAsset* NewAnimToPlay)
{
	if (NewAnimToPlay && !Cast<UAnimSequence>(NewAnimToPlay) && !Cast<UBlendSpace1D>(NewAnimToPlay))
	{
		HTR_LOG(Warning, "SetAnimation: unsupported animation asset type");
		return;
	}
	AnimBlueprintPath.clear();
	AnimBlueprintError.clear();
	bBlueprintReloadRequested = false;
	auto* SingleNodeInstance = Cast<UAnimSingleNodeInstance>(AnimInstance);

	if (!SingleNodeInstance)
	{
		SingleNodeInstance = NewObject<UAnimSingleNodeInstance>(this);
		SetAnimInstance(SingleNodeInstance);
	}

	SingleNodeInstance->SetAnimationAsset(NewAnimToPlay);
	SingleNodeInstance->SetBlendSpaceInput(BlendSpaceInput);

	RefreshAnimationPose();
}

void USkeletalMeshComponent::Play(bool bLooping)
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetLooping(bLooping);
		Instance->SetPlaying(true);
	}
}

void USkeletalMeshComponent::Stop()
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetPlaying(false);
	}
}

bool USkeletalMeshComponent::IsPlaying() const
{
	const auto* Instance = GetSingleNodeInstance();
	return Instance && Instance->IsPlaying();
}

void USkeletalMeshComponent::PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping)
{
	if (NewAnimToPlay && !Cast<UAnimSequence>(NewAnimToPlay) && !Cast<UBlendSpace1D>(NewAnimToPlay))
	{
		return;
	}

	SetAnimation(NewAnimToPlay);
	Play(bLooping);
}

void USkeletalMeshComponent::RefreshAnimationPose()
{
	if (!AnimInstance)
	{
		ResetPose();
		RefreshBoneTransformsNow();
		return;
	}

	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetBlendSpaceInput(BlendSpaceInput);
	}

	FPoseContext Pose(AnimInstance, GetBones());
	AnimInstance->EvaluateAnimation(Pose);
	SetBoneSpaceTransforms(Pose.Pose);
	RefreshBoneTransformsNow();
}


FTransform USkeletalMeshComponent::ConsumeRootMotion()
{
	const FTransform Result = PendingRootMotion;
	PendingRootMotion = FTransform();
	bHasPendingRootMotion = false;
	return Result;
}

FTransform USkeletalMeshComponent::ConvertRootMotionToWorld(const FTransform& ParentSpaceDelta) const
{
	// delta는 root 본의 부모 공간 값이다. UAL은 Armature → root 구조라 부모(Armature)를 거쳐야 컴포넌트 공간이 된다.
	const TArray<FBoneInfo>& Bones = GetBones();
	const int32 RootIndex = GetRootMotionBoneIndex();
	const int32 ParentIndex = RootIndex >= 0 ? Bones[RootIndex].ParentIndex : -1;

	// 이동: 부모(바인드 포즈) → 컴포넌트 → 월드 행렬로 방향·스케일만 적용 (Armature 스케일, 메시 0.01, yaw −180 모두 여기서)
	const FMatrix ComponentToWorld = GetWorldMatrix();
	const FMatrix ParentToWorld = ParentIndex >= 0
		? Bones[ParentIndex].InverseBindPose.Inverse() * ComponentToWorld
		: ComponentToWorld;

	FTransform Result;
	Result.Location = ParentToWorld.TransformVector(ParentSpaceDelta.Location);

	// 회전: 부모 공간 회전을 월드 공간 회전으로 바꾼다 (P × Δ × P⁻¹, P = 컴포넌트 월드 회전 × 부모 본 회전)
	FQuat ParentRotation = FQuat(GetWorldRotation());
	FQuat BoneChainRotation;
	for (int32 Index = ParentIndex; Index >= 0; Index = Bones[Index].ParentIndex)
	{
		BoneChainRotation = Bones[Index].LocalBindPose.Rotation * BoneChainRotation;
	}
	ParentRotation = (ParentRotation * BoneChainRotation).Normalized();
	Result.Rotation = (ParentRotation * ParentSpaceDelta.Rotation * ParentRotation.Inverse()).Normalized();
	return Result;
}

int32 USkeletalMeshComponent::GetRootMotionBoneIndex() const
{
	const int32 RootIndex = FindBoneIndex("root");
	if (RootIndex >= 0)
	{
		return RootIndex;
	}
	return GetBones().IsEmpty() ? -1 : 0;
}

void USkeletalMeshComponent::DrawRootDebug(FLineBatcher* LineBatcher) const
{
	const int32 RootIndex = GetRootMotionBoneIndex();
	if (!LineBatcher || RootIndex < 0 || RootIndex >= ComponentSpaceTransforms.Num())
	{
		return;
	}

	// root 본 위치에 구체
	const FVector Root = GetBoneWorldMatrix(RootIndex).TransformPosition(FVector::ZeroVector);
	const FVector4 Color(1.0f, 0.3f, 0.8f, 1.0f);
	LineBatcher->AddSphere(Root, 0.08f, Color);

	// 액터 위치를 root 높이로 내린 점 → root. 선 길이 = root가 액터(캡슐)에서 벗어난 거리
	if (const AActor* Owner = GetOwner())
	{
		const FVector ActorLocation = Owner->GetActorLocation();
		const FVector Base(ActorLocation.X, ActorLocation.Y, Root.Z);
		LineBatcher->AddLine(Base, Root, Color);
		LineBatcher->AddSphere(Base, 0.03f, Color);
	}
}
bool GShowSkeletalRootDebug = false;

void DrawSkeletalRootDebug(UWorld* World, FLineBatcher* LineBatcher)
{
	if (!World || !LineBatcher) return;
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		const USkeletalMeshComponent* Mesh = *It;
		if (!Mesh || Mesh->GetWorld() != World || !Mesh->IsRegistered()) continue;
		Mesh->DrawRootDebug(LineBatcher);
	}
}

USkeletalMeshComponent::USkeletalMeshComponent()
{
	EndPhysicsTickFunction.Target = this;
	EndPhysicsTickFunction.bCanEverTick = true;
	EndPhysicsTickFunction.TickGroup = ETickingGroup::PostPhysics;
	ClothTickFunction.Target = this;
	ClothTickFunction.bCanEverTick = true;
	ClothTickFunction.TickGroup = ETickingGroup::PostUpdateWork;
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
	ReleaseClothSolver(false);
	if (ManagedAnimGraph)
	{
		if (AnimInstance == ManagedAnimGraph.get())
		{
			SetAnimInstance(nullptr);
		}
		else
		{
			ManagedAnimGraph->UninitializeAnimation();
		}
	}
}

const FString& USkeletalMeshComponent::GetAnimBlueprintPath() const
{
	return AnimBlueprintPath;
}

const FString& USkeletalMeshComponent::GetAnimBlueprintError() const
{
	return AnimBlueprintError;
}

bool USkeletalMeshComponent::SetAnimBlueprint(const FString& Path)
{
	if (bUpdatingAnimation)
	{
		AnimBlueprintError = "Load or reload Animation Blueprints from actor Tick, outside animation callbacks.";
		return false;
	}

	if (Path.empty())
	{
		const bool bHadBlueprint = !AnimBlueprintPath.empty();
		AnimBlueprintPath.clear();
		AnimBlueprintError.clear();
		bBlueprintReloadRequested = false;

		if (ManagedAnimGraph && bHadBlueprint)
		{
			SetAnimInstance(nullptr);
			ResetPose();
			RefreshBoneTransformsNow();
		}

		return true;
	}

	AnimBlueprintError.clear();
	const FString Key = FAssetUtils::GetAssetKey(Path);
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Key);
	FAnimBlueprintData Data;

	if (!AnimBlueprintFile::IsAnimBlueprintPath(Key) || !AnimBlueprintFile::Read(FPaths::FromUtf8(FilePath.empty() ? Key : FilePath), Data, AnimBlueprintError))
	{
		if (AnimBlueprintError.empty())
		{
			AnimBlueprintError = "Use an .animblueprint asset.";
		}

		return false;
	}

	TUniquePtr<UAnimGraphInstance> Instance;
	AnimBlueprintCompiler::FNodeMap NodeIndices;

	if (!AnimBlueprintCompiler::Build(Data, *this, Instance, NodeIndices, AnimBlueprintError))
	{
		return false;
	}

	SetAnimInstance(Instance.get());
	ManagedAnimGraph = std::move(Instance);
	AnimBlueprintPath = Key;
	bBlueprintReloadRequested = false;
	AnimBlueprintError.clear();
	RefreshAnimationPose();
	return true;
}

bool USkeletalMeshComponent::ReloadAnimBlueprint()
{
	const FString Path = AnimBlueprintPath;
	bBlueprintReloadRequested = false;

	if (Path.empty())
	{
		return false;
	}

	if (!SetAnimBlueprint(Path))
	{
		HTR_LOG(Warning, "AnimBlueprint load failed: {} ({})", Path, AnimBlueprintError);
		return false;
	}

	return true;
}

UAnimGraphInstance* USkeletalMeshComponent::CreateAnimGraph()
{
	if (bUpdatingAnimation)
	{
		AnimBlueprintError = "Create animation graphs outside animation callbacks.";
		return nullptr;
	}

	if (!AnimBlueprintPath.empty())
	{
		AnimBlueprintError = "An Animation Blueprint is assigned. Use GetAnimGraph to update its variables.";
		return nullptr;
	}

	AnimBlueprintError.clear();

	if (UAnimGraphInstance* Existing = Cast<UAnimGraphInstance>(AnimInstance))
	{
		Existing->ResetGraph();
		return Existing;
	}

	TUniquePtr<UAnimGraphInstance> Instance(NewObject<UAnimGraphInstance>(this));
	SetAnimInstance(Instance.get());
	ManagedAnimGraph = std::move(Instance);
	return ManagedAnimGraph.get();
}

void FSkeletalMeshComponentEndPhysicsTickFunction::ExecuteTick(float DeltaTime)
{
	if (Target) Target->EndPhysicsTickComponent(DeltaTime);
}
void FSkeletalMeshComponentClothTickFunction::ExecuteTick(float /*DeltaTime*/)
{
	if (Target)
	{
		Target->TickClothing();
	}
}
