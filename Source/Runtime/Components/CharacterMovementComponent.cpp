#include "EnginePCH.h"
#include "CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Collision/HitResult.h"
#include "Engine/World.h"
#include "UObject/Casts.h"
#include "Physics/PhysicsScene.h"
#include "AI/Navigation/NavigationTypes.h"
#include <algorithm>
#include <cmath>

namespace
{
	// 바닥 위에 띄워 두는 틈 (cm). 바닥에 붙어 있으면 수평 sweep이 겹친 채 시작해서 막힌다
	constexpr float MIN_FLOOR_DIST = 1.9f;
	constexpr float MAX_FLOOR_DIST = 2.4f;
}

UCharacterMovementComponent::UCharacterMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	Velocity = FVector::ZeroVector;
}

void UCharacterMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();
	const FNavAgentProperties& Agent = GetDefaultNavAgent();
	SetWalkableFloorAngle(Agent.AgentMaxSlope);
	MaxStepHeight = Agent.AgentStepHeight;
}

void UCharacterMovementComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (!PawnOwner || !UpdatedComponent || DeltaTime <= 0.0f) return;

	const FVector InputVector = ConsumeInputVector();
	ControlledCharacterMove(InputVector, DeltaTime);
}

void UCharacterMovementComponent::SetWalkableFloorAngle(float InAngle)
{
	WalkableFloorAngle = std::clamp(InAngle, 0.0f, 90.0f);
	WalkableFloorZ = std::cos(FMath::DegreesToRadians(WalkableFloorAngle));
}

bool UCharacterMovementComponent::IsWalkable(const FHitResult& Hit) const
{
	if (!Hit.bBlockingHit)
		return false;
	return Hit.ImpactNormal.Z > KINDA_SMALL_NUMBER && Hit.ImpactNormal.Z >= WalkableFloorZ;
}

bool UCharacterMovementComponent::MoveUpdatedComponent(const FVector& Delta, const FRotator& NewRotation,
	bool bSweep, FHitResult* OutHit)
{
	const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(UpdatedComponent);
	FPhysicsScene* Physics = CanUseFloor() ? GetWorld()->GetPhysicsScene() : nullptr;
	const float Distance = Delta.Size();
	if (!bSweep || !Capsule || !Physics || Distance <= KINDA_SMALL_NUMBER)
		return Super::MoveUpdatedComponent(Delta, NewRotation, bSweep, OutHit);

	const FVector Start = UpdatedComponent->GetWorldLocation();
	FHitResult Hit(1.0f);
	const bool bHit = Physics->SweepCapsule(FRay(Start, Delta * (1.0f / Distance)), Distance,
		Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight(),
		FQuat(UpdatedComponent->GetWorldRotation()), Hit, GetOwner());

	const FVector Destination = bHit ? Start + Delta * Hit.Time + Hit.ImpactNormal * 0.001f : Start + Delta;
	UpdatedComponent->SetWorldLocationAndRotation(Destination, NewRotation, false);
	if (OutHit)
		*OutHit = bHit ? Hit : FHitResult(1.0f);
	return !bHit;
}

void UCharacterMovementComponent::ControlledCharacterMove(const FVector& InputVector, float DeltaTime)
{
	Acceleration = ScaleInputAcceleration(ConstrainInputAcceleration(InputVector));
	PerformMovement(DeltaTime);
}

FVector UCharacterMovementComponent::ConstrainInputAcceleration(const FVector& InputAcceleration) const
{
	return FVector(InputAcceleration.X, InputAcceleration.Y, 0.0f);
}

FVector UCharacterMovementComponent::ScaleInputAcceleration(const FVector& InputAcceleration) const
{
	return InputAcceleration.GetClampedToMaxSize(1.0f) * (MaxAcceleration * CmToWorld);
}

void UCharacterMovementComponent::PerformMovement(float DeltaTime)
{
	if (MovementMode == ECharacterMovementMode::Falling)
	{
		DiscardRootMotion();
		PhysFalling(DeltaTime);
		PhysicsRotation(DeltaTime);
		UpdateComponentVelocity();
		return;
	}

	// 루트 모션은 회전까지 직접 정하므로 PhysicsRotation을 하지 않는다
	const bool bRootMotion = ApplyRootMotion(DeltaTime);
	if (!bRootMotion)
		PhysWalking(DeltaTime);

	MaintainFloor();

	if (!bRootMotion)
		PhysicsRotation(DeltaTime);
	UpdateComponentVelocity();
}

void UCharacterMovementComponent::PhysWalking(float DeltaTime)
{
	CalcVelocity(DeltaTime, GroundFriction, BrakingDecelerationWalking * CmToWorld);
	Velocity.Z = 0.0f;

	const FVector Delta = Velocity * DeltaTime;
	if (Delta.SizeSquared() > SMALL_NUMBER)
		SafeMoveWithSlide(Delta, UpdatedComponent->GetWorldRotation());
}

void UCharacterMovementComponent::PhysFalling(float DeltaTime)
{
	// 수평: 입력은 AirControl 비율만 쓰고, 마찰·제동은 없다
	const float VerticalSpeed = Velocity.Z;
	Velocity.Z = 0.0f;
	const FVector InputAcceleration = Acceleration;
	Acceleration = InputAcceleration * AirControl;
	CalcVelocity(DeltaTime, 0.0f, 0.0f);
	Acceleration = InputAcceleration;   // 회전은 원래 입력 방향을 쓴다

	const FRotator Rotation = UpdatedComponent->GetWorldRotation();
	const FVector Horizontal = Velocity * DeltaTime;
	if (Horizontal.SizeSquared() > SMALL_NUMBER)
		SafeMoveWithSlide(Horizontal, Rotation);   // 벽에 막히면 미끄러진다

	// 수직: 중력 적분
	const float GravityZ = -GetEffectiveGravity() * GravityScale;
	const float NewVerticalSpeed = std::max(VerticalSpeed + GravityZ * DeltaTime, -TerminalVelocity * CmToWorld);
	const float DeltaZ = 0.5f * (VerticalSpeed + NewVerticalSpeed) * DeltaTime;
	Velocity.Z = NewVerticalSpeed;

	if (DeltaZ > 0.0f)
	{
		// 올라가는 중: 천장에 막히면 SafeMoveWithSlide가 위쪽 속도를 없앤다
		SafeMoveWithSlide(FVector(0.0f, 0.0f, DeltaZ), Rotation);
		return;
	}

	// 내려가는 중: 걸을 수 있는 면에 닿으면 착지, 가파른 면이면 면을 따라 미끄러진다
	FHitResult Hit(1.0f);
	if (MoveUpdatedComponent(FVector(0.0f, 0.0f, DeltaZ), Rotation, true, &Hit))
		return;

	if (IsWalkable(Hit))
	{
		ProcessLanded(Hit);
		return;
	}

	const float VDotN = Velocity.Dot(Hit.ImpactNormal);
	if (VDotN < 0.0f) Velocity = Velocity - Hit.ImpactNormal * VDotN;
	const FVector Slide = ComputeSlideVector(FVector(0.0f, 0.0f, DeltaZ), 1.0f - Hit.Time, Hit.ImpactNormal, Hit);
	if (Slide.SizeSquared() > SMALL_NUMBER)
		MoveUpdatedComponent(Slide, Rotation, true, &Hit);
}

bool UCharacterMovementComponent::FindFloor(float SweepDistance, FFindFloorResult& OutFloor) const
{
	OutFloor = FFindFloorResult();
	const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(UpdatedComponent);
	FPhysicsScene* Physics = CanUseFloor() ? GetWorld()->GetPhysicsScene() : nullptr;
	if (!Capsule || !Physics || SweepDistance <= 0.0f)
		return false;

	FHitResult Hit;
	const FVector Start = UpdatedComponent->GetWorldLocation();
	if (!Physics->SweepCapsule(FRay(Start, FVector(0.0f, 0.0f, -1.0f)), SweepDistance,
		Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight(),
		FQuat(UpdatedComponent->GetWorldRotation()), Hit, GetOwner()))
		return false;

	OutFloor.bBlockingHit = true;
	// 겹친 채 시작했으면 그 깊이만큼 음수
	OutFloor.FloorDist = Hit.Distance - Hit.PenetrationDepth;
	OutFloor.bWalkableFloor = IsWalkable(Hit);
	OutFloor.HitResult = Hit;
	return true;
}

void UCharacterMovementComponent::MaintainFloor()
{
	if (!CanUseFloor()) return;

	const float MinDist = MIN_FLOOR_DIST * CmToWorld;
	const float MaxDist = MAX_FLOOR_DIST * CmToWorld;
	FindFloor(MaxStepHeight * CmToWorld + MaxDist, CurrentFloor);

	if (CurrentFloor.bWalkableFloor)
	{
		if (CurrentFloor.FloorDist < MinDist || CurrentFloor.FloorDist > MaxDist)
		{
			const float TargetDist = 0.5f * (MinDist + MaxDist);
			MoveUpdatedComponent(FVector(0.0f, 0.0f, TargetDist - CurrentFloor.FloorDist),
				UpdatedComponent->GetWorldRotation(), false, nullptr);
		}
		return;
	}

	// 바닥이 없거나 너무 가파르다: 떨어지면서 경사를 따라 미끄러진다
	if (GravityScale > 0.0f)
	{
		Velocity.Z = 0.0f;
		SetMovementMode(ECharacterMovementMode::Falling);
	}
}

float UCharacterMovementComponent::GetCapsuleHalfHeight() const
{
	if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(UpdatedComponent))
		return Capsule->GetScaledCapsuleHalfHeight();
	return UpdatedComponent ? UpdatedComponent->GetCollisionRadius() : 0.0f;
}

bool UCharacterMovementComponent::CanUseFloor() const
{
	const AActor* Owner = GetOwner();
	return UpdatedComponent && Owner && Owner->GetActorEnableCollision() && Owner->GetWorld();
}

void UCharacterMovementComponent::SetMovementMode(ECharacterMovementMode NewMode)
{
	MovementMode = NewMode;
}

bool UCharacterMovementComponent::DoJump()
{
	if (!IsMovingOnGround() || !CanUseFloor() || JumpZVelocity <= 0.0f)
		return false;

	Velocity.Z = JumpZVelocity * CmToWorld;
	SetMovementMode(ECharacterMovementMode::Falling);
	return true;
}

void UCharacterMovementComponent::ProcessLanded(const FHitResult& Hit)
{
	Velocity.Z = 0.0f;
	SetMovementMode(ECharacterMovementMode::Walking);

	if (ACharacter* Character = Cast<ACharacter>(PawnOwner))
		Character->Landed(Hit);
}

void UCharacterMovementComponent::DiscardRootMotion()
{
	const ACharacter* Character = Cast<ACharacter>(PawnOwner);
	USkeletalMeshComponent* Mesh = Character ? Character->SkeletalMeshComponent : nullptr;
	if (Mesh && Mesh->HasPendingRootMotion())
		Mesh->ConsumeRootMotion();
}

bool UCharacterMovementComponent::ApplyRootMotion(float DeltaTime)
{
	const ACharacter* Character = Cast<ACharacter>(PawnOwner);
	USkeletalMeshComponent* Mesh = Character ? Character->SkeletalMeshComponent : nullptr;
	if (!Mesh || !Mesh->HasPendingRootMotion())
	{
		return false;
	}

	// 월드 공간 delta. 걷기 중이므로 UE처럼 수직 이동은 버린다
	const FTransform Delta = Mesh->ConsumeRootMotion();
	const FVector Move(Delta.Location.X, Delta.Location.Y, 0.0f);
	Velocity = Move * (1.0f / DeltaTime);   // 애니메이션 등에서 속도를 읽을 수 있게 남긴다

	// Root Motion의 전체 회전을 현재 회전 앞에 곱해 누적한다 (Pitch, Yaw, Roll).
	const FQuat NewQuat = (Delta.Rotation * FQuat(UpdatedComponent->GetWorldRotation())).Normalized();
	const FRotator NewRotation = NewQuat.ToFRotator();

	if (Move.SizeSquared() > SMALL_NUMBER)
		SafeMoveWithSlide(Move, NewRotation);   // Sweep: 벽에 막힌다
	else
		MoveUpdatedComponent(FVector::ZeroVector, NewRotation, false, nullptr);

	UpdateComponentVelocity();
	return true;
}

namespace
{
	// UE FMath::FixedTurn: Current에서 Desired로 최단 방향으로 최대 DeltaRate도까지만 돈다
	float FixedTurn(float Current, float Desired, float DeltaRate)
	{
		if (DeltaRate <= 0.0f) return Current;
		const float Delta = FMath::UnwindDegrees(Desired - Current);
		if (std::abs(Delta) <= DeltaRate) return Desired;
		return FMath::NormalizeAxis(Current + (Delta > 0.0f ? DeltaRate : -DeltaRate));
	}
}

void UCharacterMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (!bOrientRotationToMovement || !UpdatedComponent) return;

	const FRotator CurrentRotation = UpdatedComponent->GetWorldRotation();
	const FRotator DesiredRotation = ComputeOrientToMovementRotation(CurrentRotation);

	// 캐릭터는 수직을 유지한다 (UE ShouldRemainVertical): Pitch/Roll은 0, Yaw만 RotationRate로 돌린다
	FRotator NewRotation(0.0f, FixedTurn(CurrentRotation.Yaw, DesiredRotation.Yaw, RotationRate.Yaw * DeltaTime), 0.0f);
	if (std::abs(FMath::UnwindDegrees(NewRotation.Yaw - CurrentRotation.Yaw)) <= KINDA_SMALL_NUMBER &&
		std::abs(CurrentRotation.Pitch) <= KINDA_SMALL_NUMBER
		&& std::abs(CurrentRotation.Roll) <= KINDA_SMALL_NUMBER)
		return;

	// 위치는 그대로, 회전만 바꾼다
	MoveUpdatedComponent(FVector::ZeroVector, NewRotation, false, nullptr);
}

FRotator UCharacterMovementComponent::ComputeOrientToMovementRotation(const FRotator& CurrentRotation) const
{
	if (Acceleration.SizeSquared() < KINDA_SMALL_NUMBER * KINDA_SMALL_NUMBER)
		return CurrentRotation;

	FRotator Result;
	Result.LookDirection(Acceleration);
	return Result;
}

void UCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, float BrakingDeceleration)
{
	if (Acceleration.SizeSquared() > SMALL_NUMBER)
	{
		// UE와 같이 가속 방향이 아닌 속도 성분을 Friction만큼 깎아 방향 전환을 빠르게 한다
		const float Speed = Velocity.Size();
		const FVector AccelDir = Acceleration.GetSafeNormal();
		Velocity = Velocity - (Velocity - AccelDir * Speed) * std::min(DeltaTime * Friction, 1.0f);
		Velocity = Velocity + Acceleration * DeltaTime;
		Velocity = Velocity.GetClampedToMaxSize(MaxWalkSpeed * CmToWorld);
		return;
	}

	// 입력이 없으면 Friction 비율 감속 + 일정 감속. 0을 넘어 반대로 가지 않게 멈춘다
	const float Speed = Velocity.Size();
	if (Speed <= KINDA_SMALL_NUMBER)
	{
		Velocity = FVector::ZeroVector;
		return;
	}
	const float NewSpeed = std::max(Speed - (Speed * Friction + BrakingDeceleration) * DeltaTime, 0.0f);
	Velocity = Velocity * (NewSpeed / Speed);
}

void UCharacterMovementComponent::HandleImpact(const FHitResult& Hit, float TimeSlice, const FVector& MoveDelta)
{
	Super::HandleImpact(Hit, TimeSlice, MoveDelta);
	ApplyImpactPhysicsForces(Hit, Acceleration, Velocity);
}

namespace
{
	bool IsNearlyZero(const FVector& V, float Tolerance = KINDA_SMALL_NUMBER)
	{
		return std::abs(V.X) <= Tolerance && std::abs(V.Y) <= Tolerance && std::abs(V.Z) <= Tolerance;
	}
}

void UCharacterMovementComponent::ApplyImpactPhysicsForces(const FHitResult& Impact, const FVector& ImpactAcceleration, const FVector& ImpactVelocity)
{
	if (!bEnablePhysicsInteraction || !Impact.HitComponent) return;

	UPrimitiveComponent* ImpactComponent = Impact.HitComponent;
	FBodyInstance* BI = ImpactComponent->GetBodyInstance();
	if (!BI || !BI->IsInstanceSimulatingPhysics()) return;

	// 계수가 cm 단위라 속도 비교도 cm로 하고, 바디에 줄 때 월드 단위(m)로 바꾼다
	const FVector ComponentVelocityCm = BI->GetUnrealWorldVelocity() * (1.0f / CmToWorld);
	const FVector ImpactVelocityCm = ImpactVelocity * (1.0f / CmToWorld);
	const FVector VirtualVelocity = IsNearlyZero(ImpactAcceleration) ? ImpactVelocityCm : ImpactAcceleration.GetSafeNormal() * MaxWalkSpeed;

	float PushForceModificator = 1.0f;
	if (bScalePushForceToVelocity && !IsNearlyZero(ComponentVelocityCm))
	{
		const float Dot = ComponentVelocityCm.Dot(VirtualVelocity);
		if (Dot > 0.0f && Dot < 1.0f)
			PushForceModificator *= Dot;
	}
	if (bPushForceScaledToMass)
		PushForceModificator *= std::max(BI->GetBodyMass(), 1.0f);

	const FVector Force = Impact.ImpactNormal * (-PushForceModificator * CmToWorld);
	if (IsNearlyZero(ComponentVelocityCm, 1.0f))
		BI->AddImpulseAtPosition(Force * InitialPushForceFactor, Impact.ImpactPoint);
	else
		BI->AddForceAtPosition(Force * PushForceFactor, Impact.ImpactPoint);
}

void UCharacterMovementComponent::SafeMoveWithSlide(const FVector& Delta, const FRotator& NewRotation)
{
	const FVector Start = UpdatedComponent->GetWorldLocation();
	FHitResult Hit(1.0f);
	if (MoveUpdatedComponent(Delta, NewRotation, true, &Hit)) return;

	// 시작부터 겹친 표면이 이동 방향을 막지 않으면 sweep 없이 간다
	if (Hit.Time <= KINDA_SMALL_NUMBER && Hit.ImpactNormal.Dot(Delta) >= 0.0f)
	{
		MoveUpdatedComponent(Start + Delta - UpdatedComponent->GetWorldLocation(), NewRotation, false, nullptr);
		return;
	}

	HandleImpact(Hit, 0.0f, Delta);
	const FVector Remaining = Delta * (1.0f - Hit.Time);

	FVector Normal = Hit.ImpactNormal;
	if (IsMovingOnGround() && !IsWalkable(Hit))
	{
		if (CanStepUp(Hit) && StepUp(Remaining))
			return;
		// 걸을 수 없는 경사에 밀려 올라가지 않게 노멀을 수평으로 눕힌다
		if (Normal.Z > 0.0f)
			Normal = FVector(Normal.X, Normal.Y, 0.0f).GetSafeNormal();
	}

	const float VDotN = Velocity.Dot(Normal);
	if (VDotN < 0.0f) Velocity = Velocity - Normal * VDotN;

	const FVector Slide = ComputeSlideVector(Delta, 1.0f - Hit.Time, Normal, Hit);
	if (Slide.SizeSquared() > SMALL_NUMBER)
		MoveUpdatedComponent(Slide, NewRotation, true, &Hit);
}

bool UCharacterMovementComponent::CanStepUp(const FHitResult& Hit) const
{
	if (!Hit.bBlockingHit || !IsMovingOnGround()) return false;
	const float FootZ = UpdatedComponent->GetWorldLocation().Z - GetCapsuleHalfHeight();
	const float StepZ = Hit.ImpactPoint.Z - FootZ;
	return StepZ > 0.0f && StepZ <= MaxStepHeight * CmToWorld;
}

bool UCharacterMovementComponent::StepUp(const FVector& Delta)
{
	const FRotator Rotation = UpdatedComponent->GetWorldRotation();
	const FVector Start = UpdatedComponent->GetWorldLocation();
	const float StepZ = MaxStepHeight * CmToWorld;
	const auto Revert = [&]()
		{
			MoveUpdatedComponent(Start - UpdatedComponent->GetWorldLocation(), Rotation, false, nullptr);
			return false;
		};

	FHitResult UpHit(1.0f);
	MoveUpdatedComponent(FVector(0.0f, 0.0f, StepZ), Rotation, true, &UpHit);
	const float RaisedZ = UpdatedComponent->GetWorldLocation().Z - Start.Z;

	FHitResult ForwardHit(1.0f);
	MoveUpdatedComponent(Delta, Rotation, true, &ForwardHit);
	if (ForwardHit.bBlockingHit && ForwardHit.Time <= KINDA_SMALL_NUMBER)
		return Revert();

	FHitResult DownHit(1.0f);
	MoveUpdatedComponent(FVector(0.0f, 0.0f, -(RaisedZ + MAX_FLOOR_DIST * CmToWorld)), Rotation, true, &DownHit);
	if (!DownHit.bBlockingHit || !IsWalkable(DownHit))
		return Revert();

	const float FootZ = Start.Z - GetCapsuleHalfHeight();
	if (DownHit.ImpactPoint.Z - FootZ > StepZ)
		return Revert();

	return true;
}