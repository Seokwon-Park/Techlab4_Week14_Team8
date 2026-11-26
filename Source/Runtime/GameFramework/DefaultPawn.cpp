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

void ADefaultPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	USceneComponent* Root = GetRootComponent();
	if (!Root) return;

	// WASD는 IA_Move로 들어와 바라보는 방향(Pitch 포함) 기준으로 누적되어 있다.
	FVector Input = ConsumeMovementInputVector();

	// 에디터 카메라처럼 E는 위, Q는 아래 (월드 Z축). Shift를 누르면 빨라진다.
	float SpeedScale = 1.0f;
	if (APlayerController* PC = GetController())
	{
		if (PC->IsKeyDown(static_cast<int32>(EKeyCode::E))) Input += FVector(0.0f, 0.0f, 1.0f);
		if (PC->IsKeyDown(static_cast<int32>(EKeyCode::Q))) Input -= FVector(0.0f, 0.0f, 1.0f);
		if (PC->IsKeyDown(static_cast<int32>(EKeyCode::Shift))) SpeedScale = FastSpeedScale;
	}

	// 대각선으로 움직여도 빨라지지 않도록 길이를 1로 묶는다.
	const float LengthSq = Input.SizeSquared();
	if (LengthSq <= 0.0f) return;
	if (LengthSq > 1.0f) Input = Input / std::sqrt(LengthSq);

	Root->SetRelativeLocation(GetActorLocation() + Input * (MoveSpeed * SpeedScale * DeltaTime));
}

void ADefaultPawn::AddControllerYawInput(float Value)
{
	if (!GetRootComponent()) return;
	FRotator Rotation = GetActorRotation();
	Rotation.Yaw += Value;
	Rotation.Roll = 0.0f;
	GetRootComponent()->SetRelativeRotation(Rotation);
}

void ADefaultPawn::AddControllerPitchInput(float Value)
{
	if (!GetRootComponent()) return;
	FRotator Rotation = GetActorRotation();
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Value, -89.0f, 89.0f);
	Rotation.Roll = 0.0f;
	GetRootComponent()->SetRelativeRotation(Rotation);
}
