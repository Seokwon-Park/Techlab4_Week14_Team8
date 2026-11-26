#include "EnginePCH.h"
#include "ShapeComponent.h"
#include "Render/LineBatcher.h"
#include "UObject/UObjectIterator.h"
#include "Physics/BodySetup.h"

UShapeComponent::UShapeComponent()
{
	bCastShadow = false;
	bReceiveShadow = false;
	// 시뮬레이션에 넣으면 움직이는 Shape가 Dynamic 바디를 밀고, 캡슐이 자기 래그돌과 부딪힌다
	BodyInstance.CollisionEnabled = ECollisionEnabled::QueryOnly;
}

UShapeComponent::~UShapeComponent() = default;

UBodySetup* UShapeComponent::GetBodySetup()
{
	if (!ShapeBodySetup)
		ShapeBodySetup.reset(NewObject<UBodySetup>(this));

	ShapeBodySetup->AggGeom = FKAggregateGeom();
	UpdateBodySetup(ShapeBodySetup->AggGeom);
	return ShapeBodySetup.get();
}

FBox UShapeComponent::GetWorldAABB() const
{
	// 자식이 CalcLocalBounds를 채우면 월드 변환은 SceneComponent가 처리한다
	return CalcBounds();
}


FVector4 UShapeComponent::GetDebugColor() const
{
	if (!GetOverlapInfos().IsEmpty())  
		return FVector4(1.0f, 0.25f, 0.25f, 1.0f);
	return FVector4(ShapeColor.R / 255.0f, ShapeColor.G / 255.0f, ShapeColor.B / 255.0f, 1.0f);
}

void UShapeComponent::AddWireArc(FLineBatcher* LineBatcher, const FVector& Center, const FVector& AxisA, const FVector& AxisB,
	float Radius, float StartRad, float EndRad, const FVector4& Color, int32 Segments)
{
	LineBatcher->AddWireArc(Center, AxisA, AxisB, Radius, StartRad, EndRad, Color, Segments);
}

void DrawCollisionShapes(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected)
{
	if (!World || !LineBatcher) return;
	for (TObjectIterator<UShapeComponent> It; It; ++It)
	{
		UShapeComponent* Shape = *It;
		if (!Shape || Shape->GetWorld() != World || !Shape->IsRegistered()) continue;
		if (Shape->bDrawOnlyIfSelected && Shape->GetOwner() != Selected) continue;
		Shape->DrawDebug(LineBatcher, Shape->GetDebugColor());
	}
}