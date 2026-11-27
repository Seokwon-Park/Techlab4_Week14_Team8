#pragma once
#include "Math/Transform.h"
#include "Mesh.h"
#include "Shader.h"
#include "Material.h"
#include "Particles/ParticleHelper.h"
#include "Render/VertexFactory.h"
#include <array>
#include <deque>
#include <utility>
#include <algorithm>
#include <bit>
#include <span>

class FPrimitiveSceneProxy;
inline constexpr uint32 InvalidObjectSlot = ~0u;

struct FRenderPacket
{
	const FPrimitiveSceneProxy* Proxy = nullptr;
	const FMatrix* Model = nullptr;

	UStaticMesh* Mesh = nullptr; // static. VertexFactory가 없으면 Mesh의 LOD VertexFactory를 쓴다
	// 동적 정점(스킨, 파티클). 소유자(프록시, FRenderQueue)가 그 프레임 동안 살려 둔다
	const FVertexFactory* VertexFactory = nullptr;

	// 그릴 정점 스트림. VertexFactory가 우선이고, 없으면 스태틱 메시의 LOD
	const FVertexFactory* GetVertexFactory() const
	{
		if (VertexFactory) return VertexFactory;
		return Mesh ? Mesh->GetVertexFactory(LODIndex) : nullptr;
	}

	UMaterial* Material = nullptr;
	const void* MaterialParamData = nullptr;
	float CameraToParticleDistance = 0.0f;
	int16 TranslucencySortPriority = 0;   // 클수록 나중에 그린다
	uint32 MaterialParamDataSize = 0;
	uint32 StartIndex = 0;
	uint32 IndexCount = 0; // 0이면 인덱스 버퍼 전체를 사용한다
	uint32 Slot = InvalidObjectSlot;
	uint8 LODIndex = 0;
	bool bOccludedByGpu = false;
	int32 DebugWeightBone = -1; // 웨이트 히트맵 본 번호. -1이면 끔
};

struct FViewContext
{
	int32 ViewIndex = 0;
	uint32 Width = 0;
	uint32 Height = 0;
	FVector CameraPosition = FVector::ZeroVector;
	FVector CameraForward = FVector::ZeroVector;
	float ProjectionScaleSquared = 0.0f;
	// 투영 행렬의 세로 배율 (원근: cot(FOV/2), 직교: 2 / 화면 세로 길이). 0이면 모른다.
	// 화면 크기 기준으로 줄어드는 에디터 아이콘(UBillboardComponent)이 쓴다.
	float ProjectionScaleY = 0.0f;
	float NearZ = 0.0f;
	bool bOrthographic = false;
	bool bGameView = false;
	bool bShowSkeletalMesh = true;
	bool bShowParticles = true;
	float CameraDepth = 0.0f;
	FMatrix ViewProjection = FMatrix::Identity;
	void Prepare()
	{
		CameraDepth = CameraPosition.X * CameraForward.X
			+ CameraPosition.Y * CameraForward.Y + CameraPosition.Z * CameraForward.Z;
	}

	FMatrix BuildBillboardMatrix(const FVector& WorldPosition, float Width, float Height) const
	{
		// 카메라 화면에 나란하게 세운다. 면은 카메라 쪽(앞쪽의 반대)을 보고, 가로는 화면 오른쪽, 세로는 화면 위쪽이다.
		// 카메라 위치 대신 카메라 방향을 쓰므로 화면 가장자리에서도 기울지 않고, 텍스처 좌우도 뒤집히지 않는다.
		FVector Forward = CameraForward.Normalized();
		if (Forward.Length() <= 1e-6f)
			Forward = (WorldPosition - CameraPosition).Normalized(); // 방향 정보가 없는 뷰는 카메라에서 빌보드 쪽을 앞으로 본다
		// 왼손 좌표계(X 앞, Y 오른쪽, Z 위)에서 Up × Forward가 화면 오른쪽이다.
		FVector Right = FVector::Cross(FVector(0.0f, 0.0f, 1.0f), Forward).Normalized();
		if (Right.Length() <= 1e-6f)
			Right = FVector(0.0f, 1.0f, 0.0f); // 수직으로 내려다보거나 올려다보는 카메라
		const FVector Up = FVector::Cross(Forward, Right).Normalized();
		const FVector Facing = Forward * -1.0f;
		FMatrix Matrix = FMatrix::Identity;
		Matrix.M[0][0] = Facing.X;  Matrix.M[0][1] = Facing.Y;  Matrix.M[0][2] = Facing.Z;
		Matrix.M[1][0] = Right.X * Width;  Matrix.M[1][1] = Right.Y * Width;  Matrix.M[1][2] = Right.Z * Width;
		Matrix.M[2][0] = Up.X * Height; Matrix.M[2][1] = Up.Y * Height; Matrix.M[2][2] = Up.Z * Height;
		Matrix.M[3][0] = WorldPosition.X; Matrix.M[3][1] = WorldPosition.Y; Matrix.M[3][2] = WorldPosition.Z;
		return Matrix;
	}
};

// 바인딩을 공유하며 아이템마다 일반 인덱스 드로우를 한 번씩 실행한다
struct FStaticDrawItem
{
	const FPrimitiveSceneProxy* Proxy;
	uint32 Slot;
	uint32 StartIndex;
	uint32 IndexCount;
	uint32 bOccludedByGpu;
};

struct FStaticDrawGroup
{
	UMaterial* Material = nullptr;
	UStaticMesh* Mesh = nullptr;
	uint8 LODIndex = 0;
	std::vector<FStaticDrawItem> Items;
};

// 현재 프레임의 빌보드 및 파티클 행렬을 해당 패킷과 함께 보관한다
class FRenderQueue : public TArray<FRenderPacket>
{
	using FPackets = TArray<FRenderPacket>;
	static constexpr uint32 MatricesPerBlock = 256;
	using FMatrixBlock = std::array<FMatrix, MatricesPerBlock>;
	TArray<TUniquePtr<FMatrixBlock>> MatrixBlocks;
	TArray<TUniquePtr<TArray<FMeshParticleInstanceVertex>>> MeshParticleInstanceArrays;
	// 메시 파티클 VertexFactory는 인스턴스 배열과 수명이 같다. deque라 추가해도 주소가 바뀌지 않는다
	std::deque<FMeshParticleVertexFactory> MeshParticleFactories;
	uint32 MatrixCount = 0;
	uint32 FirstTranslucentIndex = 0;

	// 패킷 정렬 키 생성
	static uint64 MakeSortKey(const FRenderPacket& Packet)
	{
		if (Packet.Material->PSOType != EPSOType::StaticMesh_Opaque)
		{
			const uint64 DistanceBits = static_cast<uint64>(std::bit_cast<uint32>(Packet.CameraToParticleDistance));
			const uint64 PriorityBits = static_cast<uint16>(static_cast<int32>(Packet.TranslucencySortPriority) + 32768);
			return (1ull << 63) | (PriorityBits << 32) | (~DistanceBits & 0xFFFFFFFFull);
		}
		const uint64 MeshKey = Packet.Mesh ? Packet.Mesh->SortID : 0xFFFFu;
		return (static_cast<uint64>(Packet.Material->SortID) << 47)
			| (static_cast<uint64>(MeshKey) << 31)
			| (static_cast<uint64>(Packet.LODIndex & 0x3) << 29);
	}

public:
	FRenderQueue() = default;
	FRenderQueue(const FRenderQueue&) = delete;
	FRenderQueue& operator=(const FRenderQueue&) = delete;
	FRenderQueue(FRenderQueue&& Other) noexcept
		: FPackets(std::move(Other)), MatrixBlocks(std::move(Other.MatrixBlocks)),
		  MeshParticleInstanceArrays(std::move(Other.MeshParticleInstanceArrays)),
		  MeshParticleFactories(std::move(Other.MeshParticleFactories)),
		  MatrixCount(std::exchange(Other.MatrixCount, 0)),
		  FirstTranslucentIndex(std::exchange(Other.FirstTranslucentIndex, 0)) {}
	FRenderQueue& operator=(FRenderQueue&& Other) noexcept
	{
		if (this != &Other)
		{
			FPackets::operator=(std::move(Other));
			MatrixBlocks = std::move(Other.MatrixBlocks);
			MeshParticleInstanceArrays = std::move(Other.MeshParticleInstanceArrays);
			MeshParticleFactories = std::move(Other.MeshParticleFactories);
			MatrixCount = std::exchange(Other.MatrixCount, 0);
			FirstTranslucentIndex = std::exchange(Other.FirstTranslucentIndex, 0);
		}
		return *this;
	}
	const FMatrix* StoreWorldMatrix(const FMatrix& World)
	{
		const uint32 BlockIndex = MatrixCount / MatricesPerBlock;
		if (BlockIndex >= static_cast<uint32>(MatrixBlocks.Num()))
			MatrixBlocks.Add(MakeUnique<FMatrixBlock>());
		FMatrix& Stored = (*MatrixBlocks[BlockIndex])[MatrixCount % MatricesPerBlock];
		Stored = World;
		++MatrixCount;
		return &Stored;
	}

	TArray<FMeshParticleInstanceVertex>& AllocateMeshParticleInstances()
	{
		MeshParticleInstanceArrays.Add(MakeUnique<TArray<FMeshParticleInstanceVertex>>());
		return *MeshParticleInstanceArrays.Last();
	}

	// 이 프레임 동안 살아 있는 메시 파티클 VertexFactory를 만든다
	const FVertexFactory* AllocateMeshParticleFactory(FVertexBuffer* VB, FIndexBuffer* IB,
		std::span<const FMeshParticleInstanceVertex> Instances)
	{
		FMeshParticleVertexFactory& Factory = MeshParticleFactories.emplace_back();
		Factory.Init(VB, IB, Instances);
		return &Factory;
	}

	// 렌더 패킷 정렬
	void Sort()
	{
		if (IsEmpty())
		{
			FirstTranslucentIndex = 0;
			return;
		}

		struct FSortItem
		{
			uint64 Key = 0;
			int32 Index = 0;
		};

		const int32 TotalPackets = Num();
		TArray<FSortItem> SortItems;
		SortItems.Reserve(TotalPackets);
		for (int32 Index = 0; Index < TotalPackets; ++Index)
		{
			const FRenderPacket& Packet = (*this)[Index];
			const FVertexFactory* Factory = Packet.GetVertexFactory();
			if (!Factory || !Factory->IsValid() || !Packet.Material) continue;
			SortItems.Add({ MakeSortKey(Packet), Index });
		}

		std::stable_sort(SortItems.begin(), SortItems.end(), [](const FSortItem& A, const FSortItem& B)
			{
				return A.Key < B.Key;
			});

		FirstTranslucentIndex = 0;
		while (FirstTranslucentIndex < static_cast<uint32>(SortItems.Num()) && !(SortItems[FirstTranslucentIndex].Key >> 63))
		{
			++FirstTranslucentIndex;
		}

		TArray<FRenderPacket> Sorted;
		Sorted.Reserve(Num());
		for (const FSortItem& Item : SortItems)
		{
			Sorted.Add(std::move((*this)[Item.Index]));
		}

		*static_cast<FPackets*>(this) = std::move(Sorted);
	}

	uint32 GetFirstTranslucentIndex() const { return FirstTranslucentIndex; }

	void Reset()
	{
		FPackets::Reset();
		MeshParticleInstanceArrays.Reset();
		MeshParticleFactories.clear();
		MatrixCount = 0;
		FirstTranslucentIndex = 0;
	}
};
