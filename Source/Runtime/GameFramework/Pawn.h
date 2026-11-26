#pragma once

#include <vector>

#include "GameFramework/Actor.h"
#include "Input/InputActionValue.h"
#include "Pawn.generated.h"

class APlayerController;
class UPawnMovementComponent;
class UEnhancedInputComponent;
class UInputAction;
class UInputMappingContext;

UCLASS()
class APawn : public AActor
{
	GENERATED_BODY()
public:
	APawn();
	virtual ~APawn() override;
	virtual void EndPlay() override;

	// UE와 같이 PlayerController가 빙의할 때 호출한다. 여기서 Mapping Context를 추가하고 Action을 바인딩한다.
	// 기본 구현은 WASD 이동(IA_Move)과 마우스 시점(IA_Look)을 등록한다. 하위 클래스는 Super를 부른 뒤 자기 Action을 추가한다
	virtual void SetupPlayerInputComponent(UEnhancedInputComponent* PlayerInputComponent);
	// 빙의 해제 시 PlayerController가 호출한다. 이 Pawn이 추가한 Mapping Context를 모두 뺀다
	void RemovePlayerInputMappings();

	// UE 규칙: MovementComponent가 있으면 그쪽(AddInputVector)으로, 없으면 Pawn의 ControlInputVector에 누적한다.
	// 누적값은 MovementComponent(또는 Pawn 자신)가 Tick에서 Consume해서 쓴다.
	UFUNCTION(BlueprintCallable)
	virtual void AddMovementInput(const FVector& WorldDirection, float ScaleValue = 1.0f);
	virtual void AddControllerYawInput(float Value) {}
	virtual void AddControllerPitchInput(float Value) {}

	// 첫 번째 UPawnMovementComponent. 없으면 nullptr
	virtual UPawnMovementComponent* GetMovementComponent() const;

	FVector GetPendingMovementInputVector() const;
	FVector GetLastMovementInputVector() const;
	FVector ConsumeMovementInputVector();

	// UPawnMovementComponent 전용. 실제 누적값은 항상 Pawn이 들고 있다
	void Internal_AddMovementInput(const FVector& WorldAccel);
	FVector Internal_GetPendingMovementInputVector() const { return ControlInputVector; }
	FVector Internal_GetLastMovementInputVector() const { return LastControlInputVector; }
	FVector Internal_ConsumeMovementInputVector();

	void PossessedBy(APlayerController* NewController) { Controller = NewController; }
	void UnPossessed() { Controller = nullptr; }
	APlayerController* GetController() const { return Controller; }

	virtual FVector GetVelocity() const { return FVector::ZeroVector; }

	// PlayerController가 WASD를 월드 방향으로 바꿀 때 쓰는 기준 회전. 기본은 액터 회전
	virtual FRotator GetMovementInputRotation() const { return GetActorRotation(); }

	// 입력 에셋 경로 (UE에서 캐릭터 BP에 IMC/IA 에셋을 지정하는 자리). 디테일 패널에서 바꿀 수 있다
	UPROPERTY(EditAnywhere)
	FString DefaultMappingContextPath = "Assets/Input/IMC_Default.inputcontext";
	UPROPERTY(EditAnywhere)
	FString MoveActionPath = "Assets/Input/IA_Move.inputaction";
	UPROPERTY(EditAnywhere)
	FString LookActionPath = "Assets/Input/IA_Look.inputaction";

protected:
	// Controller의 FEnhancedInputSubsystem에 Context를 추가하고, 빙의 해제 때 자동으로 빠지도록 기억한다
	void AddInputMappingContext(UInputMappingContext* Context, int32 Priority = 0);

	// 기본 이동/시점 입력. 감도 등 값 가공은 IMC_Default의 Modifier가 한다
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

	// SetupPlayerInputComponent에서 위 경로로 읽어 둔다. 에셋은 FInputAssetLibrary가 소유한다
	UInputAction* MoveAction = nullptr;
	UInputAction* LookAction = nullptr;
	UInputMappingContext* DefaultMappingContext = nullptr;

	// 이번 프레임에 누적 중인 입력
	FVector ControlInputVector;
	// 직전에 Consume된 입력
	FVector LastControlInputVector;
private:
	APlayerController* Controller = nullptr;
	std::vector<UInputMappingContext*> AppliedInputContexts;
};
