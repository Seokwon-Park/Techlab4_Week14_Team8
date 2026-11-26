#pragma once

#include "GameFramework/Actor.h"
#include "Input/InputSystem.h"
#include "Input/EnhancedInputSubsystem.h"
#include "PlayerController.generated.h"

class APawn;
class APlayerCameraManager;
class UCurveFloat;
class UEnhancedInputComponent;
class UInputAction;
class UInputMappingContext;

UCLASS()
class APlayerController : public AActor
{
	GENERATED_BODY()
public:
	APlayerController();
	virtual ~APlayerController() override;
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	// 적용된 Mapping Context를 평가해 Pawn / Controller의 Input Component 바인딩을 호출한다
	void ProcessPlayerInput(float DeltaTime);
	void Possess(APawn* InPawn);
	void UnPossess();
	APawn* GetPawn() const { return PossessedPawn; }
	virtual void InputKey(int32 Key, bool bDown);
	bool IsKeyDown(int32 Key) const { return Key >= 0 && Key < 256 && bKeyDown[Key]; }
	void InputAxis(EGameInputAxis Axis, float Delta);
	void ClearInputState();

	// UE의 UEnhancedInputLocalPlayerSubsystem. Mapping Context 추가/제거는 여기서 한다
	FEnhancedInputSubsystem& GetEnhancedInputSubsystem() { return EnhancedInputSubsystem; }
	UEnhancedInputComponent* GetInputComponent() const { return InputComponent; }

	// UE의 APlayerController::AddYawInput / AddPitchInput. ControlRotation을 돌린다
	void AddYawInput(float Value);
	void AddPitchInput(float Value);

	FRotator ControlRotation;
	//PCM을 만들고 주인을 알려준다.
	void SpawnPlayerCameraManager();
	//Wolrd Tick으로부터 전달
	void UpdateCameraManager(float DeltaTime);
	APlayerCameraManager* GetCameraManager() const { return PlayerCameraManager; }

	UPROPERTY(EditAnywhere)
	FString DebugMappingContextPath = "Assets/Input/IMC_Debug.inputcontext";
	UPROPERTY(EditAnywhere)
	FString DebugCameraShakeActionPath = "Assets/Input/IA_DebugCameraShake.inputaction";
	UPROPERTY(EditAnywhere)
	FString DebugStopCameraShakeActionPath = "Assets/Input/IA_DebugStopCameraShake.inputaction";
protected:
	// UE의 SetupInputComponent. Controller 자체가 쓰는 Action(디버그 키 등)을 바인딩한다
	virtual void SetupInputComponent();

private:
	void TestCameraShake();
	void StopCameraShakes();

	FEnhancedInputSubsystem EnhancedInputSubsystem;
	// Controller 자신의 바인딩
	UEnhancedInputComponent* InputComponent = nullptr;
	// 빙의한 Pawn이 SetupPlayerInputComponent에서 채우는 바인딩. 빙의가 바뀌면 비운다
	UEnhancedInputComponent* PawnInputComponent = nullptr;
	UInputMappingContext* DebugMappingContext = nullptr;

	APlayerCameraManager* PlayerCameraManager = nullptr;
	APawn* PossessedPawn = nullptr;
	bool bKeyDown[256]{};
};
