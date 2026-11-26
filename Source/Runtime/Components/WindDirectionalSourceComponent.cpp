#include "EnginePCH.h"
#include "Components/WindDirectionalSourceComponent.h"
#include "Engine/World.h"

FVector UWindDirectionalSourceComponent::GetWindVelocity() const
{
	if (!bWindEnabled || Speed <= 0.0f)
	{
		return FVector::ZeroVector;
	}

	// 월드 전방 단위벡터와 풍속의 결합
	return GetWorldRotation().Quaternion().GetForwardVector() * Speed;
}

void UWindDirectionalSourceComponent::OnRegister()
{
	Super::OnRegister();

	// 소유 월드의 방향성 바람 등록
	GetWorld()->RegisterWindSource(*this);
}

void UWindDirectionalSourceComponent::OnUnregister()
{
	// 월드 연결 해제 전 바람 목록에서 제거
	GetWorld()->UnregisterWindSource(*this);
	Super::OnUnregister();
}
