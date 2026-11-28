#pragma once

#include "GameFramework/Actor.h"
#include "AI/Navigation/NavMeshDebugGeometry.h"
#include "NavMeshDebugPreviewActor.generated.h"

UCLASS()
class ANavDebugPreviewActor : public AActor
{
	GENERATED_BODY()
public:
	ANavDebugPreviewActor();

	UPROPERTY(EditAnywhere)
	int32 GridCells = 8;
	UPROPERTY(EditAnywhere)
	float CellSize = 1.0f;

	void BeginPlay() override;
	void Tick(float DeltaTime) override;

private:
	void BuildFakeGeometry();
	FNavMeshDebugGeometry FakeGeometry;
};