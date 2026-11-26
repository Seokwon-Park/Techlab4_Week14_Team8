#pragma once

#include "UObject/Object.h"
#include "Physics/PhysicsSettingsEnums.h"
#include "PhysicalMaterial.generated.h"

UCLASS()
class UPhysicalMaterial : public UObject
{
    GENERATED_BODY()

public:
    // 이동 마찰계수
    UPROPERTY(EditAnywhere)
    float Friction = 0.5f;

    // 정지 마찰계수
    UPROPERTY(EditAnywhere)
    float StaticFriction = 0.5f;

    // 반발계수
    UPROPERTY(EditAnywhere)
    float Restitution = 0.1f;

    // 재질 밀도(g/cm³)
    UPROPERTY(EditAnywhere)
    float Density = 0.1f;

    // 재질의 마찰 결합 방식
    UPROPERTY(EditAnywhere)
    EPhysicalMaterialCombineMode FrictionCombineMode = EPhysicalMaterialCombineMode::Average;

    // 마찰 결합 방식의 재질 우선 적용 여부
    UPROPERTY(EditAnywhere)
    bool bOverrideFrictionCombineMode = false;

    // 재질의 반발 결합 방식
    UPROPERTY(EditAnywhere)
    EPhysicalMaterialCombineMode RestitutionCombineMode = EPhysicalMaterialCombineMode::Average;

    // 반발 결합 방식의 재질 우선 적용 여부
    UPROPERTY(EditAnywhere)
    bool bOverrideRestitutionCombineMode = false;
};
