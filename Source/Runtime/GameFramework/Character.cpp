#include "EnginePCH.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character.h"

ACharacter::ACharacter()
{
	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>("CapsuleComponent");
	// Shape 컴포넌트 기본값(QueryOnly)과 달리 시뮬레이션 바디가 캐릭터에 막히게 한다
	CapsuleComponent->BodyInstance.CollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	SetRootComponent(CapsuleComponent);

	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
	SkeletalMeshComponent->SetupAttachment(CapsuleComponent);

	CharacterMovementComponent = CreateDefaultSubobject<UCharacterMovementComponent>("CharacterMovementComponent");
}

void ACharacter::AddControllerYawInput(float Value)
{
	if (!GetRootComponent()) return;
	FRotator Rotation = GetActorRotation();
	Rotation.Yaw += Value;
	GetRootComponent()->SetRelativeRotation(Rotation);
}

void ACharacter::AddControllerPitchInput(float Value)
{
	if (!GetRootComponent()) return;
	FRotator Rotation = GetActorRotation();
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Value, -89.0f, 89.0f);
	GetRootComponent()->SetRelativeRotation(Rotation);
}

FVector ACharacter::GetVelocity() const
{
	return CharacterMovementComponent->Velocity;
}

void ACharacter::Jump()
{
	if (CanJump())
		CharacterMovementComponent->DoJump();
}

bool ACharacter::CanJump() const
{
	return CharacterMovementComponent && CharacterMovementComponent->IsMovingOnGround();
}

bool ACharacter::IsFalling() const
{
	return CharacterMovementComponent && CharacterMovementComponent->IsFalling();
}
