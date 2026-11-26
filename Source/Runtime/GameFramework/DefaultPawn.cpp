#include "EnginePCH.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Input/KeyCode.h"

// UE의 ADefaultPawn처럼 충돌 없이 날아다니는 카메라다.
// 화면을 가리지 않도록 메시는 비워 두고, 카메라는 루트 자리(오프셋 0)에 둔다.
// 메시가 필요하면 Settings 패널의 Pawn Mesh에서 고를 수 있다.
ADefaultPawn::ADefaultPawn()
{
	SetActorEnableCollision(false);
	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("StaticMeshComponent");
	SetRootComponent(StaticMeshComponent);
	CameraComponent->SetupAttachment(StaticMeshComponent);
	CameraComponent->SetRelativeLocation({ 0.0f, 0.0f, 0.0f });
	CameraComponent->SetRelativeRotation({ 0.0f, 0.0f, 0.0f });
	CameraComponent->SetExternalInputManaged(true);
}

void ADefaultPawn::SyncViewRotationFromActor()
{
	ViewRotation = GetActorRotation();
	ViewRotation.Pitch = FMath::Clamp(ViewRotation.Pitch, -89.0f, 89.0f);
	ViewRotation.Roll = 0.0f;
	bViewRotationSynced = true;
	if (USceneComponent* Root = GetRootComponent())
		Root->SetRelativeRotation(ViewRotation);
}

void ADefaultPawn::BeginPlay()
{
	Super::BeginPlay();
	SyncViewRotationFromActor();
}

void ADefaultPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	USceneComponent* Root = GetRootComponent();
	if (!Root) return;

	if (!bViewRotationSynced)
		SyncViewRotationFromActor();

	// 남아 있을 수 있는 누적 입력을 비우고, 에디터 카메라와 동일한 기준으로 직접 이동한다.
	ConsumeMovementInputVector();

	APlayerController* PC = GetController();
	if (!PC) return;

	const FQuat Orientation = ViewRotation.Quaternion();
	const FVector Forward = Orientation.GetForwardVector();
	const FVector Right = Orientation.GetRightVector();
	const FVector Up = Orientation.GetUpVector();

	FVector Direction = FVector::ZeroVector;
	if (PC->IsKeyDown(static_cast<int32>(EKeyCode::W))) Direction += Forward;
	if (PC->IsKeyDown(static_cast<int32>(EKeyCode::S))) Direction -= Forward;
	if (PC->IsKeyDown(static_cast<int32>(EKeyCode::D))) Direction += Right;
	if (PC->IsKeyDown(static_cast<int32>(EKeyCode::A))) Direction -= Right;
	if (PC->IsKeyDown(static_cast<int32>(EKeyCode::E))) Direction += Up;
	if (PC->IsKeyDown(static_cast<int32>(EKeyCode::Q))) Direction -= Up;

	const float LengthSq = Direction.SizeSquared();
	if (LengthSq <= 0.000001f) return;

	const float SpeedScale = PC->IsKeyDown(static_cast<int32>(EKeyCode::Shift)) ? FastSpeedScale : 1.0f;
	Root->SetRelativeLocation(GetActorLocation() + (Direction / std::sqrt(LengthSq)) * (MoveSpeed * SpeedScale * DeltaTime));
}

void ADefaultPawn::AddControllerYawInput(float Value)
{
	if (!GetRootComponent()) return;
	if (!bViewRotationSynced)
		SyncViewRotationFromActor();
	ViewRotation.Yaw += Value;
	ViewRotation.Roll = 0.0f;
	GetRootComponent()->SetRelativeRotation(ViewRotation);
}

void ADefaultPawn::AddControllerPitchInput(float Value)
{
	if (!GetRootComponent()) return;
	if (!bViewRotationSynced)
		SyncViewRotationFromActor();
	ViewRotation.Pitch = FMath::Clamp(ViewRotation.Pitch + Value, -89.0f, 89.0f);
	ViewRotation.Roll = 0.0f;
	GetRootComponent()->SetRelativeRotation(ViewRotation);
}
