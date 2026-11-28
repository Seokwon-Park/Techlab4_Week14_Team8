#include "EnginePCH.h"
#include "GameFramework/Actor/MyCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Animation/MyAnimInstance.h"
#include "Components/CharacterMovementComponent.h"
#include "Collision/HitResult.h"
#include "Animation/AnimGraphInstance.h"
#include "Input/EnhancedInputComponent.h"
#include "Input/InputAction.h"
#include "Input/InputMappingContext.h"
#include "Input/InputAssetLibrary.h"
#include "Input/InputActionValue.h"
#include "AI/Navigation/NavigationSubsystem.h"
#include "Engine/World.h"
#include "Debug/DrawDebugHelpers.h"

AMyCharacter::AMyCharacter()
{
	SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>("SpringArmComponent");
	SpringArmComponent->SetupAttachment(CapsuleComponent);
	// UE 3인칭 템플릿: 팔이 ControlRotation(Yaw + Pitch)을 그대로 따라 캐릭터 주위를 구면으로 공전한다
	// (템플릿 400cm / 캡슐 반높이 96cm 비율 ≈ 4, 이 엔진 캡슐 반높이 1)
	SpringArmComponent->TargetArmLength = 4.0f;
	SpringArmComponent->TargetOffset = FVector(0.0f, 0.0f, 0.0f);
	SpringArmComponent->SocketOffset = FVector(0.0f, 0.0f, 0.0f);
	SpringArmComponent->bUsePawnControlRotation = true;
	SpringArmComponent->bInheritPitch = true;
	SpringArmComponent->bInheritYaw = true;
	SpringArmComponent->bInheritRoll = true;
	SpringArmComponent->bEnableCameraLag = false;
	SpringArmComponent->bEnableCameraRotationLag = false;

	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	CameraComponent->SetupAttachment(SpringArmComponent);
	CameraComponent->SetExternalInputManaged(true);                  // 카메라 자체 자유 비행 입력 끔
	CameraComponent->bUsePawnControlRotation = false;                // 템플릿: 카메라는 팔 끝 회전을 그대로 쓴다

	// UE 3인칭 템플릿 값: 몸은 마우스가 아니라 이동(가속) 방향을 향해 초당 500도까지 돈다
	CharacterMovementComponent->bOrientRotationToMovement = true;
	CharacterMovementComponent->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	CapsuleComponent->SetCapsuleRadius(0.41f);
}

void AMyCharacter::SetupPlayerInputComponent(UEnhancedInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (!PlayerInputComponent) return;

	CharacterMappingContext = FInputAssetLibrary::LoadMappingContext(CharacterMappingContextPath);
	JumpAction = FInputAssetLibrary::LoadInputAction(JumpActionPath);
	AttackAction = FInputAssetLibrary::LoadInputAction(AttackActionPath);
	NavMoveAction = FInputAssetLibrary::LoadInputAction(NavMoveActionPath);

	AddInputMappingContext(CharacterMappingContext, 1);
	// 이전 MyPlayerController처럼 눌린 순간에만 한 번 실행한다
	PlayerInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AMyCharacter::Jump);
	PlayerInputComponent->BindAction(AttackAction, ETriggerEvent::Started, this, &AMyCharacter::Attack);
	PlayerInputComponent->BindAction(NavMoveAction, ETriggerEvent::Started, this, &AMyCharacter::StartTestNavMove);
}

bool AMyCharacter::RequestMoveTo(const FVector& Goal)
{
	ClearPathFollowing();

	const UWorld* World = GetWorld();
	const FNavigationSubsystem* Navigation = World ? World->GetSubsystem<FNavigationSubsystem>() : nullptr;
	if (!Navigation || !CapsuleComponent)
	{
		return false;
	}

	const FVector CapsuleCenter = CapsuleComponent->GetWorldLocation();
	const float HalfHeight = CapsuleComponent->GetScaledCapsuleHalfHeight();
	const FVector Start = CapsuleCenter - FVector(0.0f, 0.0f, HalfHeight);

	ActiveNavPath = Navigation->FindPath(Start, Goal);
	DrawDebugNavPath(World, Goal, ActiveNavPath, AcceptanceRadius, 5.0f);
	if (!ActiveNavPath.IsValid() || ActiveNavPath.Points.Num() == 0)
	{
		HTR_LOG(Warning, "Move request failed: path status={}", static_cast<int>(ActiveNavPath.Status));
		return false;
	}

	CurrentPathPointIndex = 0;
	ActiveNavigationRevision = Navigation->GetNavigationRevision();

	FVector Delta = ActiveNavPath.Points[0] - Start;
	Delta.Z = 0.0f;
	BestDistanceToPathPoint = Delta.Size();

	HTR_LOG(Info, "Move request accepted: path status={}, points={}", static_cast<int>(ActiveNavPath.Status), ActiveNavPath.Points.Num());
	return true;
}

void AMyCharacter::BeginPlay()
{
	// Super::BeginPlay 안에서 Lua BeginPlay가 돈다. 스크립트가 AnimGraph를 만들었으면 그대로 쓴다.
	Super::BeginPlay();

	if (SkeletalMeshComponent->GetAnimBlueprintPath().empty() && !Cast<UAnimGraphInstance>(SkeletalMeshComponent->GetAnimInstance()))
	{
		UMyAnimInstance* Instance = NewObject<UMyAnimInstance>(SkeletalMeshComponent);
		SkeletalMeshComponent->SetAnimInstance(Instance);
	}
}

void AMyCharacter::ClearPathFollowing()
{
	ActiveNavPath = {};
	CurrentPathPointIndex = INDEX_NONE;
	ActiveNavigationRevision = 0;
	BestDistanceToPathPoint = 0.0f;
	SecondsWithoutProgress = 0.0f;
}

void AMyCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!ActiveNavPath.Points.IsValidIndex(CurrentPathPointIndex))
	{
		return;
	}

	const UWorld* World = GetWorld();
	const FNavigationSubsystem* Navigation = World ? World->GetSubsystem<FNavigationSubsystem>() : nullptr;
	if (!Navigation || !CapsuleComponent || Navigation->GetNavigationRevision() != ActiveNavigationRevision)
	{
		HTR_LOG(Info, "Path following cancelled: navigation changed or became unavailable.");
		ClearPathFollowing();
		return;
	}

	constexpr float WaypointRadius = 0.10f;
	constexpr float HeightTolerance = 0.35f;
	constexpr float ProgressThreshold = 0.03f;
	constexpr float BlockedTimeout = 1.5f;

	const FVector Feet = CapsuleComponent->GetWorldLocation() - FVector(0.0f, 0.0f, CapsuleComponent->GetScaledCapsuleHalfHeight());
	const int32 LastPointIndex = ActiveNavPath.Points.Num() - 1;
	bool bAdvanced = false;

	while (CurrentPathPointIndex <= LastPointIndex)
	{
		const FVector Delta = ActiveNavPath.Points[CurrentPathPointIndex] - Feet;
		const FVector HorizontalDelta(Delta.X, Delta.Y, 0.0f);
		const float Distance = HorizontalDelta.Size();
		const bool bLastPoint = CurrentPathPointIndex == LastPointIndex;
		const float ReachRadius = bLastPoint ? AcceptanceRadius : WaypointRadius;

		if (Distance <= ReachRadius && std::abs(Delta.Z) <= HeightTolerance)
		{
			if (bLastPoint)
			{
				const bool bPartial = ActiveNavPath.Status == ENavPathStatus::Partial;
				HTR_LOG(Info, "Path following finished: {}", bPartial ? "PartialReached" : "Arrived");
				ClearPathFollowing();
				return;
			}

			++CurrentPathPointIndex;
			bAdvanced = true;
			continue;
		}

		if (bAdvanced || 
			Distance + ProgressThreshold < BestDistanceToPathPoint)
		{
			BestDistanceToPathPoint = Distance;
			SecondsWithoutProgress = 0.0f;
		}
		else
		{
			SecondsWithoutProgress += DeltaTime;
		}

		if (SecondsWithoutProgress >= BlockedTimeout)
		{
			HTR_LOG(Warning, "Path following blocked at waypoint {}.", CurrentPathPointIndex);
			ClearPathFollowing();
			return;
		}

		AddMovementInput(HorizontalDelta.GetSafeNormal());
		return;
	}
}

void AMyCharacter::Attack()
{
	UAnimInstance* AnimInstance = SkeletalMeshComponent->GetAnimInstance();
	if (UMyAnimInstance* MyInstance = Cast<UMyAnimInstance>(AnimInstance))
	{
		MyInstance->RequestAttack();
	}
	// Lua AnimGraph: 공격 중이면 무시, false 복귀는 그래프(Attack 상태 OnExit)가 맡는다
	else if (UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(AnimInstance))
	{
		if (!Graph->GetBool(AttackingVariable))
			Graph->SetBool(AttackingVariable, true);
	}
}

void AMyCharacter::Jump()
{
	// 공중이면 무시 (이중 점프 없음)
	if (!CanJump())
		return;

	Super::Jump();

	// C++ MyAnimInstance에는 점프 상태가 없다. AnimGraph(AnimBlueprint/Lua)만 변수를 켠다
	if (UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(SkeletalMeshComponent->GetAnimInstance()))
		Graph->SetBool(JumpingVariable, true);
}

void AMyCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(SkeletalMeshComponent->GetAnimInstance()))
		Graph->SetBool(JumpingVariable, false);
}

void AMyCharacter::DetectedHit(const FHitResult& Hit)
{
	AActor* HitActor = Hit.HitComponent ? Hit.HitComponent->GetOwner() : nullptr;
	if (AMyCharacter* HitCharacter = Cast<AMyCharacter>(HitActor))
	{
		HTR_LOG(Info, "[Attack] {} -> {}", GetName(), HitCharacter->GetName());
		HitCharacter->SetIsHit();
	}
}

void AMyCharacter::SetIsHit()
{
	UAnimInstance* AnimInstance = SkeletalMeshComponent->GetAnimInstance();
	if (UMyAnimInstance* MyInstance = Cast<UMyAnimInstance>(AnimInstance))
	{
		MyInstance->SetIsHit(true);
	}
	else if (UAnimGraphInstance* Graph = Cast<UAnimGraphInstance>(AnimInstance))
	{
		Graph->SetBool(HitVariable, true);
	}
}

FRotator AMyCharacter::GetMovementInputRotation() const
{
	// UE 3인칭 템플릿처럼 컨트롤 회전의 Yaw만 써서 수평 이동 방향을 만든다
	if (const APlayerController* PC = GetController())
		return FRotator(0.0f, PC->ControlRotation.Yaw, 0.0f);
	return Super::GetMovementInputRotation();
}

void AMyCharacter::StartTestNavMove()
{
	RequestMoveTo(FVector(4.0f, 0.0f, 2.4834f));
}

void AMyCharacter::Move(const FInputActionValue& Value)
{
	const FVector2 Axis = Value.Get<FVector2>();
	if ((Axis.X != 0.0f || Axis.Y != 0.0f) && CurrentPathPointIndex != INDEX_NONE)
	{
		ClearPathFollowing();
		HTR_LOG(Info, "Path following cancelled by manual input.");
	}

	Super::Move(Value);
}