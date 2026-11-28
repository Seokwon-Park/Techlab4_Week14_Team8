#pragma once

#include "PrimitiveComponent.h"
#include "TerrainCollisionComponent.generated.h"

UCLASS()
class UTerrainCollisionComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UTerrainCollisionComponent();

protected:
	bool ShouldCreateRenderState() const override { return false; }
	bool ShouldCreatePhysicsState() const override;
	void OnCreatePhysicsState() override;
};