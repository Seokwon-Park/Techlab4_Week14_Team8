#pragma once

#include "Math/Frustum.h"
#include "Render/MeshRenderData.h"
#include "Render/RenderPacket.h"

class FScene;
class UPrimitiveComponent;
class UStaticMesh;
class UMaterial;
class FRenderQueue;
struct FViewContext;

class FPrimitiveSceneProxy
{
public:
	explicit FPrimitiveSceneProxy(UPrimitiveComponent* InComponent);
	
	virtual ~FPrimitiveSceneProxy();
	virtual void UpdateRenderState();

	virtual bool HasDynamicMeshElements() const { return false; }
	virtual void GetDynamicMeshElements(FRenderQueue& Queue, const FViewContext& View);

	bool IsSkinned() const { return bSkinned; }
	
	void UpdateTransform();
	void OnMeshRenderDataChanged();
	void OnMeshDestroyed();

	UPrimitiveComponent* GetComponent() const { return Component; }
	// FScene::PrimitiveSceneProxies 안의 위치. 다른 프록시가 지워지면 바뀔 수 있다.
	int32 GetPackedIndex() const { return PackedIndex; }
	const FAABB& GetBounds() const { return Bounds; }
	const FMatrix& GetLocalToWorld() const { return LocalToWorld; }
	const FMatrix& GetWorldToLocal() const { return WorldToLocal; }
	const FLODSphere& GetLODSphere() const { return LODSphere; }
	UStaticMesh* GetMesh() const { return Mesh; }
	bool IsVisible() const { return bVisible; }
	bool IsShown(bool bGameView) const { return bVisible && !(bGameView && bHiddenInGame); }
	uint8 GetLODCount() const { return RenderState ? RenderState->LODCount : 1; }
	const FMeshRenderState* GetRenderState() const { return RenderState.get(); }
	const float* GetLODThresholdsSq() const { return RenderState->LODThresholdSq; }
	const FCachedMeshLOD& GetLOD(uint32 Index) const { return RenderState->LODs[Index]; }
	const FCachedMeshSection& GetSection(uint32 Index) const { return RenderState->Sections[Index]; }
	FScene* GetScene() const { return Scene; }

	void SetCastShadow(bool NewValue);
	void SetReceiveShadow(bool NewValue);

	bool bCastsShadow = true;
	bool bReceiveShadow = true;
	bool bReceivesDecals = true;
	// 화면 크기에 따라 뷰마다 줄여 그리는 에디터 아이콘 메시 (라이트 방향 화살표 등).
	// 고정 행렬을 쓰는 빠른 경로 대신 느린 경로에서 UStaticMeshComponent::SubmitScreenSizedIcon으로 그린다.
	bool bScreenSizeScaledIcon = false;
protected:
	bool bSkinned = false;
	TSharedPtr<const FMeshRenderState> RenderState;
	void UpdateCommonFlags();
private:
	friend class FScene;
	FScene* Scene = nullptr;
	UPrimitiveComponent* Component = nullptr;
	UStaticMesh* Mesh = nullptr;
	FMatrix LocalToWorld;
	FMatrix WorldToLocal;
	FAABB Bounds;
	FLODSphere LODSphere;
	int32 PackedIndex = INDEX_NONE;
	bool bQueuedForUpdate = false;
	bool bRenderStateQueued = false;
	bool bVisible = true;
	bool bHiddenInGame = false;
};
