#include "EnginePCH.h"
#include "GameFramework/Pawn.h"
#include "Components/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Input/EnhancedInputComponent.h"
#include "Input/EnhancedInputSubsystem.h"
#include "Input/InputAction.h"
#include "Input/InputMappingContext.h"
#include "Input/InputAssetLibrary.h"

APawn::APawn()
{
	PrimaryActorTick.bCanEverTick = true;
}

APawn::~APawn()
{
	// 지우기 전에 Subsystem이 Context를 들고 있지 않도록 뺀다 (보통은 EndPlay/UnPossess에서 이미 빠져 있다)
	RemovePlayerInputMappings();
}

void APawn::EndPlay()
{
	// 게임 중 Pawn이 파괴되면 Controller가 지워진 Pawn과 그 Context를 들고 있지 않게 빙의를 푼다
	if (Controller && Controller->GetPawn() == this)
		Controller->UnPossess();
	Super::EndPlay();
}

void APawn::SetupPlayerInputComponent(UEnhancedInputComponent* PlayerInputComponent)
{
	if (!PlayerInputComponent) return;

	// 매번 읽어도 캐시에서 같은 객체가 나온다. 파일을 고쳤다면 이때 다시 읽힌다
	DefaultMappingContext = FInputAssetLibrary::LoadMappingContext(DefaultMappingContextPath);
	MoveAction = FInputAssetLibrary::LoadInputAction(MoveActionPath);
	LookAction = FInputAssetLibrary::LoadInputAction(LookActionPath);

	AddInputMappingContext(DefaultMappingContext, 0);
	PlayerInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APawn::Move);
	PlayerInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &APawn::Look);
}

void APawn::RemovePlayerInputMappings()
{
	if (Controller)
	{
		FEnhancedInputSubsystem& Subsystem = Controller->GetEnhancedInputSubsystem();
		for (UInputMappingContext* Context : AppliedInputContexts)
			Subsystem.RemoveMappingContext(Context);
	}
	AppliedInputContexts.clear();
}

void APawn::AddInputMappingContext(UInputMappingContext* Context, int32 Priority)
{
	if (!Context || !Controller) return;
	Controller->GetEnhancedInputSubsystem().AddMappingContext(Context, Priority);
	if (std::find(AppliedInputContexts.begin(), AppliedInputContexts.end(), Context) == AppliedInputContexts.end())
		AppliedInputContexts.push_back(Context);
}

void APawn::Move(const FInputActionValue& Value)
{
	const FVector2 Axis = Value.Get<FVector2>();
	// UE와 같이 축 값(-1~1)만 넘긴다. DeltaTime은 입력을 소비해 실제로 움직이는 쪽(MovementComponent / Pawn Tick)이 곱한다.
	const FQuat Rotation = GetMovementInputRotation().Quaternion();
	if (Axis.Y != 0.0f) AddMovementInput(Rotation.GetForwardVector(), Axis.Y);
	if (Axis.X != 0.0f) AddMovementInput(Rotation.GetRightVector(), Axis.X);
}

void APawn::Look(const FInputActionValue& Value)
{
	const FVector2 Axis = Value.Get<FVector2>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
	if (Controller)
	{
		Controller->AddYawInput(Axis.X);
		Controller->AddPitchInput(Axis.Y);
	}
}

void APawn::AddMovementInput(const FVector& WorldDirection, float ScaleValue)
{
	if (UPawnMovementComponent* MovementComponent = GetMovementComponent())
	{
		MovementComponent->AddInputVector(WorldDirection * ScaleValue);
	}
	else
	{
		Internal_AddMovementInput(WorldDirection * ScaleValue);
	}
}

UPawnMovementComponent* APawn::GetMovementComponent() const
{
	for (UActorComponent* Component : GetComponents())
	{
		if (UPawnMovementComponent* MovementComponent = Cast<UPawnMovementComponent>(Component))
			return MovementComponent;
	}
	return nullptr;
}

FVector APawn::GetPendingMovementInputVector() const
{
	return ControlInputVector;
}

FVector APawn::GetLastMovementInputVector() const
{
	return LastControlInputVector;
}

FVector APawn::ConsumeMovementInputVector()
{
	if (UPawnMovementComponent* MovementComponent = GetMovementComponent())
	{
		return MovementComponent->ConsumeInputVector();
	}
	return Internal_ConsumeMovementInputVector();
}

void APawn::Internal_AddMovementInput(const FVector& WorldAccel)
{
	ControlInputVector += WorldAccel;
}

FVector APawn::Internal_ConsumeMovementInputVector()
{
	LastControlInputVector = ControlInputVector;
	ControlInputVector = FVector::ZeroVector;
	return LastControlInputVector;
}
