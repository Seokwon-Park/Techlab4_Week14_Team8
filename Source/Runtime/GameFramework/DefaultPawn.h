#pragma once

#include "GameFramework/Pawn.h"
#include "DefaultPawn.generated.h"

class UCameraComponent;
class UStaticMeshComponent;

UCLASS()
class ADefaultPawn : public APawn
{
	GENERATED_BODY()
public:
	ADefaultPawn();
	// MovementComponent가 없으므로 에디터 카메라와 동일한 방식(WASD + EQ + Shift)으로 이동한다.
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void AddControllerYawInput(float Value) override;
	void AddControllerPitchInput(float Value) override;
	UCameraComponent* GetCameraComponent() const { return CameraComponent; }
	UStaticMeshComponent* GetStaticMeshComponent() const { return StaticMeshComponent; }
private:
	void SyncViewRotationFromActor();

	UCameraComponent* CameraComponent = nullptr;
	UStaticMeshComponent* StaticMeshComponent = nullptr;
	FRotator ViewRotation = FRotator::Identitiy;
	bool bViewRotationSynced = false;
	float MoveSpeed = 10.0f;
	float FastSpeedScale = 3.0f;
};
