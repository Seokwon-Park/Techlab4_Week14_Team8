// UE ULandscapeComponent 자리. 다만 청크마다가 아니라 지형 하나에 하나만 둔다.
// 엔진 렌더 경로(프록시 → GetDynamicMeshElements → SubmitToRenderQueue)에 지형을 등록하는 창구다.
// 청크 데이터는 ATerrain이 갖고, 이 컴포넌트는 청크마다 렌더 패킷을 낸다. 청크 컬링도 여기서 한다

#pragma once

#include "Components/PrimitiveComponent.h"
#include "TerrainComponent.generated.h"

class ATerrain;

UCLASS()
class UTerrainComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UTerrainComponent() = default;

	void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;

	FBox CalcLocalBounds() const override;

private:
	ATerrain* GetTerrain() const;
};