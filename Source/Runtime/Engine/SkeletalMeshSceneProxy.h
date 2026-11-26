#pragma once

#include "PrimitiveSceneProxy.h"
#include "Render/VertexFactory.h"

class USkinnedMeshComponent;

class FSkeletalMeshSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FSkeletalMeshSceneProxy(USkinnedMeshComponent* InComponent);

	void UpdateRenderState() override;
	bool HasDynamicMeshElements() const override { return true; }

	void GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View) override;
private:
	// GPU 스킨이면 본 버퍼까지, CPU 스킨(본 버퍼 없음)이면 스킨 결과 버퍼만 묶는다
	FGpuSkinVertexFactory GpuSkinFactory;
	FLocalVertexFactory CpuSkinFactory;
};