#include "EnginePCH.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h" 
#include "Engine/World.h"               
#include "Camera/CameraShakeBase.h"
#include "Input/EnhancedInputComponent.h"
#include "Input/InputAction.h"
#include "Input/InputMappingContext.h"
#include "Input/InputAssetLibrary.h"
#include <cstring>

APlayerController::APlayerController()
	: ControlRotation(FRotator::Identitiy)
{
	PrimaryActorTick.bCanEverTick = true;
}

APlayerController::~APlayerController()
{
	// Pawn이 넣은 Context는 Pawn이 이미 지워졌을 수 있으므로 역참조하지 않고 Subsystem과 함께 버린다
	delete PawnInputComponent;
	delete InputComponent;
	PawnInputComponent = InputComponent = nullptr;
}

void APlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!InputComponent)
	{
		InputComponent = NewObject<UEnhancedInputComponent>(this);
		SetupInputComponent();
	}
}

void APlayerController::SetupInputComponent()
{
	// 카메라 셰이크 테스트 키(F6/F7). Controller 전용 Context라 Pawn이 바뀌어도 유지된다
	DebugMappingContext = FInputAssetLibrary::LoadMappingContext(DebugMappingContextPath);
	UInputAction* ShakeAction = FInputAssetLibrary::LoadInputAction(DebugCameraShakeActionPath);
	UInputAction* StopShakeAction = FInputAssetLibrary::LoadInputAction(DebugStopCameraShakeActionPath);

	EnhancedInputSubsystem.AddMappingContext(DebugMappingContext, 100);
	InputComponent->BindAction(ShakeAction, ETriggerEvent::Started, this, &APlayerController::TestCameraShake);
	InputComponent->BindAction(StopShakeAction, ETriggerEvent::Started, this, &APlayerController::StopCameraShakes);
}

void APlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ProcessPlayerInput(DeltaTime);
}

void APlayerController::ProcessPlayerInput(float DeltaTime)
{
	// UE의 입력 스택처럼 Pawn의 바인딩을 먼저, Controller의 바인딩을 나중에 호출한다
	std::vector<UEnhancedInputComponent*> InputStack;
	if (PossessedPawn && PawnInputComponent) InputStack.push_back(PawnInputComponent);
	if (InputComponent) InputStack.push_back(InputComponent);
	EnhancedInputSubsystem.ProcessInput(DeltaTime, InputStack);
}

void APlayerController::AddYawInput(float Value)
{
	ControlRotation.Yaw += Value;
}

void APlayerController::AddPitchInput(float Value)
{
	ControlRotation.Pitch = FMath::Clamp(ControlRotation.Pitch + Value, -89.9f, 89.9f);
}

void APlayerController::Possess(APawn* InPawn)
{
	if (PossessedPawn == InPawn) return;
	UnPossess();
	PossessedPawn = InPawn;
	if (PossessedPawn)
	{
		PossessedPawn->PossessedBy(this);
		ControlRotation = PossessedPawn->GetActorRotation();

		// UE의 APawn::SetupPlayerInputComponent 호출 시점과 같다
		if (!PawnInputComponent) PawnInputComponent = NewObject<UEnhancedInputComponent>(this);
		PawnInputComponent->ClearActionBindings();
		PossessedPawn->SetupPlayerInputComponent(PawnInputComponent);
	}
	if (PlayerCameraManager) PlayerCameraManager->SetViewTarget(PossessedPawn);
}

void APlayerController::UnPossess()
{
	if (PossessedPawn)
	{
		PossessedPawn->RemovePlayerInputMappings();
		PossessedPawn->UnPossessed();
	}
	if (PawnInputComponent) PawnInputComponent->ClearActionBindings();
	PossessedPawn = nullptr;
}

void APlayerController::InputKey(int32 Key, bool bDown)
{
	if (Key < 0 || Key >= 256) return;
	bKeyDown[Key] = bDown;
	EnhancedInputSubsystem.InputKey(FKey(Key), bDown);
}

void APlayerController::InputAxis(EGameInputAxis Axis, float Delta)
{
	switch (Axis)
	{
	case EGameInputAxis::MouseX:     EnhancedInputSubsystem.InputAxis(EKeys::MouseX, Delta); break;
	case EGameInputAxis::MouseY:     EnhancedInputSubsystem.InputAxis(EKeys::MouseY, Delta); break;
	case EGameInputAxis::MouseWheel: EnhancedInputSubsystem.InputAxis(EKeys::MouseWheel, Delta); break;
	}
}

void APlayerController::ClearInputState()
{
	std::memset(bKeyDown, 0, sizeof(bKeyDown));
	EnhancedInputSubsystem.FlushPressedKeys();
}

void APlayerController::TestCameraShake()
{
	if (!PlayerCameraManager) return;

	UClass* ShakeClass = UCameraShakeBase::StaticClass();
	auto* Settings = static_cast<UCameraShakeBase*>(ShakeClass->GetDefaultObject());
	if (!Settings) return;

	// 새 Shake를 생성할 때 복사할 테스트용 기본 설정.
	Settings->Duration = 1.0f;
	Settings->BlendInTime = 0.05f;
	Settings->BlendOutTime = 0.3f;

	Settings->RotationPitch.Amplitude = 2.0f;
	Settings->RotationPitch.Frequency = 10.0f;

	Settings->RotationYaw.Amplitude = 1.0f;
	Settings->RotationYaw.Frequency = 13.0f;

	// 이전 테스트를 끊고 새로 실행한다.
	PlayerCameraManager->StopAllCameraShakes(true);
	PlayerCameraManager->StartCameraShake(ShakeClass, 1.0f);
}

void APlayerController::StopCameraShakes()
{
	if (PlayerCameraManager) PlayerCameraManager->StopAllCameraShakes(true);
}

void APlayerController::SpawnPlayerCameraManager()
{
	// 중복생성방지
	if (PlayerCameraManager || !GetWorld()) return;

	PlayerCameraManager = GetWorld()->SpawnActor<APlayerCameraManager>();
	PlayerCameraManager->InitializeFor(this);
}
void APlayerController::UpdateCameraManager(float DeltaTime)
{
	if (PlayerCameraManager)
	{
		PlayerCameraManager->UpdateCamera(DeltaTime);
	}
}
