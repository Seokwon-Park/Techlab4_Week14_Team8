#include "EnginePCH.h"
#include "SkeletalMeshObject.h"
#include "RenderCommand.h"
#include "Tasks/Tasks.h"
#include "Tasks/TaskScheduler.h"
#include "Render/SkinningStats.h"
#include <cmath>

// GpuSkinCommon.hlsli가 이 배치를 가정한다
static_assert(sizeof(FMatrix) == 64);

namespace
{
	// 비균등 스케일에서도 면에 수직이 되도록 3x3 부분의 여인수 행렬로 노멀을 옮긴다.
	// 미러(행렬식 음수)면 여인수 결과가 뒤집히므로 부호를 되돌린다.
	FVector TransformNormal(const FMatrix& M, const FVector& N)
	{
		const FVector R0(M.M[0][0], M.M[0][1], M.M[0][2]);
		const FVector R1(M.M[1][0], M.M[1][1], M.M[1][2]);
		const FVector R2(M.M[2][0], M.M[2][1], M.M[2][2]);
		const FVector C0 = FVector::Cross(R1, R2);
		const FVector Result = C0 * N.X + FVector::Cross(R2, R0) * N.Y + FVector::Cross(R0, R1) * N.Z;
		return (R0.Dot(C0) < 0.0f ? Result * -1.0f : Result).Normalized();
	}

	FVector4 WeightToColor(float Weight)
	{
		if (Weight <= 0.0f)
			return FVector4(0.08f, 0.08f, 0.10f, 1.0f);
		const float T = std::min(Weight, 1.0f);
		const FVector Low(0.0f, 0.2f, 1.0f);
		const FVector Mid(0.0f, 1.0f, 0.2f);
		const FVector High(1.0f, 0.1f, 0.0f);
		const FVector C = T < 0.5f ? Low + (Mid - Low) * (T * 2.0f) : Mid + (High - Mid) * (T * 2.0f - 1.0f);
		return FVector4(C.X, C.Y, C.Z, 1.0f);
	}
}

FSkeletalMeshObject::FSkeletalMeshObject(USkeletalMesh* InMesh)
	:SkeletalMesh(InMesh)
{
	RefToLocal.SetNum(InMesh->GetNumBones());
	Bounds = SkinnedBounds = InMesh->GetRenderData().LODRenderData[0].BindAABB;
}

void FSkeletalMeshObject::SkinVerticesCPU(TArray<FVertex>& OutVertices, FBox& OutBounds) const
{
	const FSkeletalMeshLODRenderData& LOD = SkeletalMesh->GetRenderData().LODRenderData[0];
	const uint32 Count = static_cast<uint32>(LOD.BindVertices.Num());
	const uint32 NumThreads = Tasks::FTaskScheduler::Get().GetNumWorkers() + 1;
	const uint32 ChunkCount = std::clamp(Count / 2048u, 1u, NumThreads * 2);

	TArray<FBox> ChunkBounds;
	ChunkBounds.SetNum(ChunkCount);

	//Liner Blend Skinning LBS
	Tasks::ParallelForChunks(Count, ChunkCount, [&](uint32 Begin, uint32 End, uint32 Chunk)
		{
			FBox ChunkBox{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX,-FLT_MAX,-FLT_MAX) };
			for (uint32 v = Begin; v < End; ++v)
			{
				const FVertex& Bind = LOD.BindVertices[v];
				const FSkinWeight& W = LOD.SkinWeights[v];

				const FMatrix M = W.BuildSkinMatrix(RefToLocal);

				FVertex& Out = OutVertices[v];
				Out = Bind;
				Out.Position = M.TransformPosition(Bind.Position);
				Out.Normal = TransformNormal(M, Bind.Normal);
				const FVector Tangent = M.TransformVector(FVector(Bind.Tangent.X, Bind.Tangent.Y, Bind.Tangent.Z)).Normalized();
				Out.Tangent = FVector4(Tangent.X, Tangent.Y, Tangent.Z, Bind.Tangent.W);

				if (DebugWeightBone >= 0)
				{
					float Weight = 0.0f;
					for (int32 k = 0; k < MaxBoneInfluences; ++k)
					{
						if (W.BoneIndex[k] == DebugWeightBone)
							Weight += W.Weight[k];
					}
					Out.Color = WeightToColor(Weight);
				}

				for (int Axis = 0; Axis < 3; ++Axis)
				{
					ChunkBox.Min[Axis] = FMath::Min(ChunkBox.Min[Axis], Out.Position[Axis]);
					ChunkBox.Max[Axis] = FMath::Max(ChunkBox.Max[Axis], Out.Position[Axis]);
				}
			}
			ChunkBounds[Chunk] = ChunkBox;
		});
	OutBounds = ChunkBounds[0];
	for (uint32 i = 1; i < ChunkCount; ++i)
		OutBounds.Expand(ChunkBounds[i]);
}



bool FSkeletalMeshObject::UpdateBounds()
{
	bool bInside = true;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (SkinnedBounds.Min[Axis] < Bounds.Min[Axis] || SkinnedBounds.Max[Axis] > Bounds.Max[Axis])
			bInside = false;
	}
	if (bInside) return false;

	const FVector Margin = (SkinnedBounds.Max - SkinnedBounds.Min) * 0.25f;
	Bounds.Min = SkinnedBounds.Min - Margin;
	Bounds.Max = SkinnedBounds.Max + Margin;
	return true;
}

void FSkeletalMeshObject::UpdateRefToLocalMatrices(const TArray<FMatrix>& ComponentSpaceTransforms)
{
	const TArray<FBoneInfo>& Bones = SkeletalMesh->GetRefSkeleton();
	const int32 Count = std::min(Bones.Num(), ComponentSpaceTransforms.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		RefToLocal[i] = Bones[i].InverseBindPose * ComponentSpaceTransforms[i];
	}
}

FSkeletalMeshObjectCPUSkin::FSkeletalMeshObjectCPUSkin(USkeletalMesh* InMesh)
	:FSkeletalMeshObject(InMesh)
{
	const FSkeletalMeshLODRenderData& LOD = InMesh->GetRenderData().LODRenderData[0];
	SkinnedVertices = LOD.BindVertices;

	const uint32 Size = sizeof(FVertex) * static_cast<uint32>(SkinnedVertices.Num());
	FinalVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(Size, sizeof(FVertex));

	UploadVertices();
}

// 변형된 Cloth 삼각형의 노멀과 UV 탄젠트 재계산
void FSkeletalMeshObjectCPUSkin::RebuildClothTangentSpace(const TArray<FClothVertexPosition>& ClothPositions)
{
	constexpr float UVTolerance = 1.0e-8f;
	constexpr float VectorToleranceSquared = 1.0e-12f;
	const FSkeletalMeshLODRenderData& LOD = SkeletalMesh->GetRenderData().LODRenderData[0];

	// 활성 Cloth 정점의 배열 인덱스와 누적 버퍼 준비
	TArray<int32> RenderVertexToClothIndex;
	RenderVertexToClothIndex.Init(INDEX_NONE, SkinnedVertices.Num());
	TArray<FVector> NormalSums;
	NormalSums.Init(FVector::ZeroVector, ClothPositions.Num());
	TArray<FVector> TangentSums;
	TangentSums.Init(FVector::ZeroVector, ClothPositions.Num());
	for (int32 Index = 0; Index < ClothPositions.Num(); ++Index)
	{
		RenderVertexToClothIndex[ClothPositions[Index].RenderVertexIndex] = Index;
	}

	for (const FSkeletalMeshSection& Section : LOD.Sections)
	{
		// 다른 섹션과 정점을 공유하지 않는 활성 Cloth 섹션 확인
		if (Section.IndexCount == 0
			|| RenderVertexToClothIndex[LOD.Indices[Section.StartIndex]] == INDEX_NONE)
		{
			continue;
		}

		const uint32 End = Section.StartIndex + Section.IndexCount;
		for (uint32 Index = Section.StartIndex; Index < End; Index += 3)
		{
			const uint32 Corners[] = { LOD.Indices[Index], LOD.Indices[Index + 1], LOD.Indices[Index + 2] };
			const FVertex& A = SkinnedVertices[Corners[0]];
			const FVertex& B = SkinnedVertices[Corners[1]];
			const FVertex& C = SkinnedVertices[Corners[2]];
			const FVector EdgeAB = B.Position - A.Position;
			const FVector EdgeAC = C.Position - A.Position;
			const FVector FaceNormal = EdgeAB.Cross(EdgeAC);
			if (FaceNormal.SizeSquared() <= VectorToleranceSquared)
			{
				continue;
			}

			// 면적을 반영한 면 노멀과 UV의 U 방향 계산
			const FVector2 UVAB = B.UV - A.UV;
			const FVector2 UVAC = C.UV - A.UV;
			const float Determinant = UVAB.X * UVAC.Y - UVAB.Y * UVAC.X;
			FVector FaceTangent = FVector::ZeroVector;
			if (std::abs(Determinant) > UVTolerance)
			{
				FaceTangent = (EdgeAB * UVAC.Y - EdgeAC * UVAB.Y) / Determinant;
			}

			for (const uint32 RenderVertex : Corners)
			{
				const int32 ClothIndex = RenderVertexToClothIndex[RenderVertex];
				NormalSums[ClothIndex] += FaceNormal;
				TangentSums[ClothIndex] += FaceTangent;
			}
		}
	}

	for (int32 Index = 0; Index < ClothPositions.Num(); ++Index)
	{
		const FVector Normal = NormalSums[Index].GetSafeNormal(VectorToleranceSquared);
		if (Normal.SizeSquared() <= VectorToleranceSquared)
		{
			continue;
		}

		// 새 노멀에 탄젠트를 직교화하고 원본 UV의 좌우 부호 유지
		FVertex& Vertex = SkinnedVertices[ClothPositions[Index].RenderVertexIndex];
		FVector Tangent = TangentSums[Index] - Normal * Normal.Dot(TangentSums[Index]);
		if (Tangent.SizeSquared() <= VectorToleranceSquared)
		{
			const FVector PreviousTangent(Vertex.Tangent.X, Vertex.Tangent.Y, Vertex.Tangent.Z);
			Tangent = PreviousTangent - Normal * Normal.Dot(PreviousTangent);
			if (Tangent.SizeSquared() <= VectorToleranceSquared)
			{
				const FVector Axis = std::abs(Normal.X) < 0.9f
					? FVector(1.0f, 0.0f, 0.0f) : FVector(0.0f, 1.0f, 0.0f);
				Tangent = Axis - Normal * Normal.Dot(Axis);
			}
		}

		Vertex.Normal = Normal;
		Vertex.Tangent = FVector4(Tangent.GetSafeNormal(VectorToleranceSquared), Vertex.Tangent.W);
	}
}

void FSkeletalMeshObjectCPUSkin::Update(const TArray<FMatrix>& ComponentSpaceTransforms, const TArray<FClothVertexPosition>* ClothPositions)
{
	UpdateRefToLocalMatrices(ComponentSpaceTransforms);
	{
		SCOPE_CYCLE_COUNTER(SkinningStats::STAT_CpuSkinning);
		SkinVerticesCPU(SkinnedVertices, SkinnedBounds);
	}
	if (ClothPositions && !ClothPositions->IsEmpty())
	{
		for (const FClothVertexPosition& Vertex : *ClothPositions)
		{
			SkinnedVertices[Vertex.RenderVertexIndex].Position = Vertex.Position;
			SkinnedBounds.Expand(FBox{ Vertex.Position, Vertex.Position });
		}
		RebuildClothTangentSpace(*ClothPositions);
	}
	UploadVertices();
}

void FSkeletalMeshObjectCPUSkin::UploadVertices()
{
	SCOPE_CYCLE_COUNTER(SkinningStats::STAT_SkinUpload);
	if (!FinalVertexBuffer) return;
	void* Dst = RenderCommand::MapWriteDiscard(FinalVertexBuffer.get());
	if (!Dst)return;
	std::memcpy(Dst, SkinnedVertices.GetData(), sizeof(FVertex) * SkinnedVertices.Num());
	RenderCommand::Unmap(FinalVertexBuffer.get());
}

FSkeletalMeshObjectGPUSkin::FSkeletalMeshObjectGPUSkin(USkeletalMesh* InMesh)
	:FSkeletalMeshObject(InMesh)
{
	CPUVertices = InMesh->GetRenderData().LODRenderData[0].BindVertices;
	BoneMatrixBuffer = RenderCommand::CreateDynamicStructuredBuffer(static_cast<uint32>(InMesh->GetNumBones()), sizeof(FMatrix));

	// 첫 Update 전에도 바인드 포즈로 그려지도록 단위행렬을 올려 둔다
	for (FMatrix& Matrix : RefToLocal)
		Matrix = FMatrix::Identity;
	if (BoneMatrixBuffer && BoneMatrixBuffer->GetSRV())
		UploadBoneMatrices();
}

bool FSkeletalMeshObjectGPUSkin::IsValid() const
{
	const FSkeletalMeshLODRenderData& LOD = SkeletalMesh->GetRenderData().LODRenderData[0];
	return BoneMatrixBuffer && BoneMatrixBuffer->GetSRV()
		&& LOD.GpuSkinVertexBuffer && LOD.GpuSkinVertexBuffer->GetBuffer();
}

void FSkeletalMeshObjectGPUSkin::Update(const TArray<FMatrix>& ComponentSpaceTransforms, const TArray<FClothVertexPosition>* /*ClothPositions*/)
{
	UpdateRefToLocalMatrices(ComponentSpaceTransforms);
	UpdateSkinnedBoundsFromBones();
	UploadBoneMatrices();
	bCPUVerticesDirty = true;
}

FVertexBuffer* FSkeletalMeshObjectGPUSkin::GetVertexBuffer(uint32 LOD) const
{
	return SkeletalMesh->GetRenderData().LODRenderData[LOD].GpuSkinVertexBuffer.get();
}

const TArray<FVertex>* FSkeletalMeshObjectGPUSkin::GetCPUSkinnedVertices() const
{
	if (bCPUVerticesDirty)
	{
		SCOPE_CYCLE_COUNTER(SkinningStats::STAT_CpuSkinningPicking); 
		FBox Unused;
		SkinVerticesCPU(CPUVertices, Unused);
		bCPUVerticesDirty = false;
	}
	return &CPUVertices;
}

void FSkeletalMeshObjectGPUSkin::UpdateSkinnedBoundsFromBones()
{
	// LBS 결과는 영향 본들이 옮긴 위치의 볼록 결합이라, 본별 바운드의 합집합 안에 항상 들어간다
	const TArray<FBox>& BoneBounds = SkeletalMesh->GetRenderData().LODRenderData[0].BoneBounds;
	bool bFirst = true;
	for (int32 i = 0; i < BoneBounds.Num(); ++i)
	{
		if (BoneBounds[i].Min.X > BoneBounds[i].Max.X) continue;
		const FBox Box = BoneBounds[i].GetWorldAABB(RefToLocal[i]);
		if (bFirst) SkinnedBounds = Box;
		else SkinnedBounds.Expand(Box);
		bFirst = false;
	}
}

void FSkeletalMeshObjectGPUSkin::UploadBoneMatrices()
{
	SCOPE_CYCLE_COUNTER(SkinningStats::STAT_BoneUpload);
	void* Dst = RenderCommand::MapWriteDiscard(BoneMatrixBuffer.get());
	if (!Dst) return;
	std::memcpy(Dst, RefToLocal.GetData(), sizeof(FMatrix) * RefToLocal.Num());
	RenderCommand::Unmap(BoneMatrixBuffer.get());
}


