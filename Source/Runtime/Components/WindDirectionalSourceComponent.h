#pragma once

#include "Components/SceneComponent.h"
#include "WindDirectionalSourceComponent.generated.h"

UCLASS()
class UWindDirectionalSourceComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	// 월드 좌표계의 풍속(m/s)
	FVector GetWindVelocity() const;

protected:
	void OnRegister() override;
	void OnUnregister() override;

private:
	// 지정한 풍속을 보존하는 바람 활성 설정
	UPROPERTY(EditAnywhere)
	bool bWindEnabled = true;

	// 컴포넌트 전방으로 부는 풍속(m/s)
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.1))
	float Speed = 5.0f;
};
