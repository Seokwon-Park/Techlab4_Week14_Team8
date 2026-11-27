#include "EnginePCH.h"
#include "StaticMeshComponent.h"
#include "Asset/AssetManager.h"
#include "Render/RenderCommand.h"

#include "Engine/PrimitiveSceneProxy.h"
#include "Asset/LOD/StaticMeshLODSelector.h"

#include <algorithm>
#include <iterator>


// StaticMesh 컴포넌트를 초기화한다.
UStaticMeshComponent::UStaticMeshComponent()
{
	StaticMesh = UAssetManager::GetAssetByPath<UStaticMesh>("Cube");
}

// StaticMesh 컴포넌트의 소멸을 처리한다.
UStaticMeshComponent::~UStaticMeshComponent()
{
}

// 부모 컴포넌트의 시작 처리를 호출한다.
void UStaticMeshComponent::BeginPlay()
{
	Super::BeginPlay();
}

// 부모 컴포넌트의 프레임 갱신을 호출한다.
void UStaticMeshComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
}

void UStaticMeshComponent::SetStaticMesh(UStaticMesh* InStaticMesh)
{
	if (StaticMesh == InStaticMesh)
		return;

	StaticMesh = InStaticMesh;
	RenderMeshOverride = nullptr;
	ClearOverrideMaterials();
	MarkRenderStateDirty();
	OnTransformDirty();
}

void UStaticMeshComponent::SetRenderMeshOverride(UStaticMesh* InMesh)
{
	if (RenderMeshOverride == InMesh) return;
	RenderMeshOverride = InMesh;
	MarkRenderStateDirty();
}

void UStaticMeshComponent::SetRenderTransformOverride(const FTransform& WorldTransform)
{
	if (RenderTransformOverride)
	{
		const FTransform& Previous = *RenderTransformOverride;
		if (Previous.Location == WorldTransform.Location && Previous.Scale == WorldTransform.Scale &&
			std::equal(std::begin(Previous.Rotation.V), std::end(Previous.Rotation.V), WorldTransform.Rotation.V))
			return;
		*RenderTransformOverride = WorldTransform;
	}
	else
		RenderTransformOverride = MakeUnique<FTransform>(WorldTransform);
	OnTransformDirty();
}

void UStaticMeshComponent::ClearRenderTransformOverride()
{
	if (RenderTransformOverride)
	{
		RenderTransformOverride.reset();
		OnTransformDirty();
	}
}

int32 UStaticMeshComponent::GetNumMaterials() const
{
	return StaticMesh ? static_cast<int32>(StaticMesh->GetMeshData().MaterialSlots.Num()) : 0;
}

FString UStaticMeshComponent::GetMaterialSlotName(int32 SlotIndex) const
{
	if (!StaticMesh || SlotIndex < 0 || SlotIndex >= GetNumMaterials())
		return FString();
	return StaticMesh->GetMeshData().MaterialSlots[SlotIndex].Name;
}

UMaterial* UStaticMeshComponent::GetDefaultMaterial(int32 SlotIndex) const
{
	return StaticMesh ? StaticMesh->GetMaterial(static_cast<uint32>(SlotIndex)) : nullptr;
}

void UStaticMeshComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	if (!StaticMesh) return;
	uint32 SelectedLOD = 0;
	if (const FPrimitiveSceneProxy* Proxy = GetSceneProxy())
	{
		SelectedLOD = SelectLOD(*Proxy, ViewContext);
	}
	SubmitToRenderQueue(RenderQueue, SelectedLOD);
}

void UStaticMeshComponent::SubmitScreenSizedIcon(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	const FPrimitiveSceneProxy* Proxy = GetSceneProxy();
	if (!Proxy || !Proxy->GetMesh()) return;
	const FMeshRenderState* State = Proxy->GetRenderState();
	if (!State || State->LODCount == 0) return;

	// 메시 원점(부모 라이트 위치)을 기준으로 줄인다. 행 벡터 규약이라 0~2행이 축이다.
	FMatrix World = Proxy->GetLocalToWorld();
	const FVector Origin(World.M[3][0], World.M[3][1], World.M[3][2]);
	const float Scale = ComputeScreenSizeClampScale(Origin, ViewContext.CameraPosition, ViewContext.CameraForward,
		ViewContext.bOrthographic, ViewContext.ProjectionScaleY, ScreenSizeRefSize, ScreenSize);
	for (int32 Row = 0; Row < 3; ++Row)
		for (int32 Col = 0; Col < 3; ++Col)
			World.M[Row][Col] *= Scale;
	const FMatrix* Model = RenderQueue.StoreWorldMatrix(World);

	const FCachedMeshLOD& LOD = State->LODs[0];
	for (uint32 I = 0; I < LOD.NumSections; ++I)
	{
		const FCachedMeshSection& Section = State->Sections[LOD.FirstSection + I];
		FRenderPacket& Packet = RenderQueue.AddDefaulted_GetRef();
		Packet.Mesh = Proxy->GetMesh();
		Packet.Model = Model;
		Packet.Material = Section.Material;
		Packet.StartIndex = Section.StartIndex;
		Packet.IndexCount = Section.IndexCount;
		Packet.LODIndex = 0;
	}
}

// 지정한 LOD의 Section으로 패킷을 만든다.
void UStaticMeshComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, uint32 LODIndex)
{
    const FPrimitiveSceneProxy* Proxy = GetSceneProxy();
    if (!Proxy || !Proxy->GetMesh()) return;
    const FMeshRenderState* State = Proxy->GetRenderState();
    if (!State) return;
    if (LODIndex >= State->LODCount) LODIndex = 0;
    const FCachedMeshLOD& LOD = State->LODs[LODIndex];
    for (uint32 I = 0; I < LOD.NumSections; ++I)
    {
        const FCachedMeshSection& Section = State->Sections[LOD.FirstSection + I];
        FRenderPacket& Packet = RenderQueue.AddDefaulted_GetRef();
        Packet.Proxy = Proxy;
        Packet.Mesh = Proxy->GetMesh();
        Packet.Material = Section.Material;
        Packet.StartIndex = Section.StartIndex;
        Packet.IndexCount = Section.IndexCount;
        Packet.LODIndex = static_cast<uint8>(LODIndex);
    }
}
/*
// Section별 Material·Texture와 인덱스 범위를 보존해 패킷을 제출한다.
void UStaticMeshComponent::SubmitToRenderQueue(TQueue<FRenderPacket>& RenderQueue)
{
	if (!StaticMesh)
		return;

	const FStaticMeshData& MeshData = StaticMesh->GetMeshData();

	for (const FStaticMeshSection& Section : MeshData.Sections)
	{
		// 슬롯마다 덮어쓰기가 있으면 그것, 없으면 메시(OBJ/MTL)의 기본 머티리얼
		UMaterial* SectionMaterial = GetMaterial(static_cast<int32>(Section.MaterialSlotIndex));
		if (!SectionMaterial)
			continue;

		FRenderPacket rp;
		rp.mesh = StaticMesh;
		rp.model = GetWorldMatrix();
		rp.StartIndex = Section.StartIndex;
		rp.IndexCount = Section.IndexCount;
		rp.material = SectionMaterial;

		RenderQueue.Enqueue(rp);
	}
}
*/
