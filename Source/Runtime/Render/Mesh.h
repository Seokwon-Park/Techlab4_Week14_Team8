#pragma once

#include "Asset/RenderAsset.h"
#include "Render/Buffer.h"
#include "Render/StaticMeshData.h"
#include "Render/MeshRenderData.h"
#include "Render/VertexFactory.h"
#include <unordered_map>
#include <vector>
#include "Mesh.generated.h"

class FPrimitiveSceneProxy;

struct FStaticMeshLODResource
{
	FStaticMeshData Data;
	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;
	// 버퍼를 다시 만들 수 있으므로 UStaticMesh::GetVertexFactory가 꺼낼 때마다 현재 버퍼로 맞춘다
	mutable FLocalVertexFactory VertexFactory;
};

UCLASS()
class UStaticMesh : public URenderAsset
{
	GENERATED_BODY()
public:
    UStaticMesh() : SortID(NextSortID++) {}
	virtual ~UStaticMesh() override;

	FStaticMeshData MeshData;

	// Renderer가 실제로 Bind할 런타임 재질 객체
	TArray<UMaterial*> Materials;

	TUniquePtr<FVertexBuffer> VertexBuffer;
	TUniquePtr<FIndexBuffer> IndexBuffer;

	const FStaticMeshData& GetMeshData() const { return MeshData; }
	UMaterial* GetMaterial(uint32 SlotIndex) const;

public:
    TArray<FStaticMeshLODResource> AdditionalLODs;
    std::array<float, 3> ScreenThresholds{ 0.30f, 0.10f, 0.03f };
    FString SourceObjPath;

    uint32 GetLODCount() const
    {
        return 1u + static_cast<uint32>(AdditionalLODs.Num());
    }

    const FStaticMeshData& GetMeshData(uint32 LODIndex) const
    {
        return LODIndex == 0 ? MeshData : AdditionalLODs[LODIndex - 1].Data;
    }

    FVertexBuffer* GetVertexBuffer(uint32 LODIndex) const
    {
        return LODIndex == 0 ? VertexBuffer.get() : AdditionalLODs[LODIndex - 1].VertexBuffer.get();
    }

    FIndexBuffer* GetIndexBuffer(uint32 LODIndex) const
    {
        return LODIndex == 0 ? IndexBuffer.get() : AdditionalLODs[LODIndex - 1].IndexBuffer.get();
    }

    // LOD별 정점 스트림. 버퍼를 다시 만들 수 있으므로 꺼낼 때마다 현재 버퍼로 맞춘다
    const FVertexFactory* GetVertexFactory(uint32 LODIndex) const
    {
        FLocalVertexFactory& Factory = LODIndex == 0 ? VertexFactory : AdditionalLODs[LODIndex - 1].VertexFactory;
        Factory.Init(GetVertexBuffer(LODIndex), GetIndexBuffer(LODIndex));
        return &Factory;
    }

    void RebuildRenderData();
    const FLODSphere& GetLocalLODSphere() const { return LocalLODSphere; }
    const FBox& GetRenderBounds() const { return RenderBounds; }
    TSharedPtr<const FMeshRenderState> GetRenderState(const std::vector<UMaterial*>& Materials);
    void RegisterProxy(FPrimitiveSceneProxy* Proxy);
    void UnregisterProxy(FPrimitiveSceneProxy* Proxy);

    const uint16 SortID;

    class UBodySetup* GetBodySetup();
    FString GetBodySetupFilePath() const;
    bool SaveBodySetup(FString* OutError = nullptr);
    bool LoadBodySetupIfExists();
private:
    static inline uint16 NextSortID = 0;
    mutable FLocalVertexFactory VertexFactory; // LOD0
    struct FMaterialSlotsHash
    {
        size_t operator()(const std::vector<UMaterial*>& Slots) const;
    };
    FLODSphere LocalLODSphere;
    FBox RenderBounds;
    std::unordered_map<std::vector<UMaterial*>, TWeakPtr<const FMeshRenderState>, FMaterialSlotsHash> RenderStates;
    TArray<FPrimitiveSceneProxy*> RenderProxies;

    class UBodySetup* BodySetup = nullptr;
};
