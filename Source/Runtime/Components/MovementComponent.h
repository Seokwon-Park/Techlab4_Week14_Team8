#pragma once
#include "ActorComponent.h"
#include "SceneComponent.h"
#include "MovementComponent.generated.h"

UCLASS()
class UMovementComponent : public UActorComponent
{
	GENERATED_BODY()

protected:
	USceneComponent* UpdatedComponent;

public:
	UPROPERTY(EditAnywhere)
	FVector Velocity;

	UPROPERTY(EditAnywhere)
	float Gravity = 9.81f;

	// 실제로 쓸 중력 크기(아래 방향이 양수). World Settings에서 월드 중력을 덮어쓰면 그 값을, 아니면 Gravity를 쓴다.
	float GetEffectiveGravity() const;

	// ActorComponent  Interface
	virtual void InitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime) override;

	virtual void SetUpdatedComponent(USceneComponent* NewComponent);
	virtual void UpdateComponentVelocity();

	virtual void HandleImpact(const FHitResult& Hit, float TimeSlice = 0.0f, const FVector& MoveDelta = FVector::ZeroVector);
	virtual FVector ComputeSlideVector(const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& HIt) const;

	virtual bool MoveUpdatedComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult* Hit);
	virtual void StopMovementImmediately();

protected:
	UPROPERTY(EditAnywhere)
	bool bConstrainToPlane = false;
	FVector PlaneConstraintNormal = FVector::ZeroVector;
	FVector PlaneConstraintOrigin = FVector::ZeroVector;

	virtual FVector ConstrainDirectionToPlane(FVector Direction) const;
};
