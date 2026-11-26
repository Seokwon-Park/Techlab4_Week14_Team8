#include "EnginePCH.h"
#include "SkeletalMeshData.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <string>

bool FSkeletalMeshData::Validate(FString& OutError) const
{
	const int32 NumVertices = BindVertices.Num();
	const int32 NumBones = Bones.Num();

	if (NumVertices == 0 || Indices.Num() == 0)
	{
		OutError = "SkeletalMeshData: no vertices or indices";
		return false;
	}
	if (Indices.Num() % 3 != 0)
	{
		OutError = "SkeletalMeshData: index count is not a multiple of 3";
		return false;
	}
	if (SkinWeight.Num() != NumVertices)
	{
		OutError = "SkeletalMeshData: SkinWeight count (" + std::to_string(SkinWeight.Num())
			+ ") != vertex count (" + std::to_string(NumVertices) + ")";
		return false;
	}
	if (NumBones == 0 || NumBones > 256)
	{
		// FSkinWeight::BoneIndex가 uint8이라 256개까지
		OutError = "SkeletalMeshData: bone count must be 1..256, got " + std::to_string(NumBones);
		return false;
	}

	for (const uint32 Index : Indices)
	{
		if (Index >= static_cast<uint32>(NumVertices))
		{
			OutError = "SkeletalMeshData: index " + std::to_string(Index) + " out of range";
			return false;
		}
	}

	// 부모가 항상 앞 인덱스여야 RefToLocal을 한 번의 순회로 계산할 수 있다
	for (int32 I = 0; I < NumBones; ++I)
	{
		const int32 Parent = Bones[I].ParentIndex;
		if (Parent >= I || Parent < -1)
		{
			OutError = "SkeletalMeshData: bone " + std::to_string(I) + " (" + Bones[I].Name + ") has invalid parent " + std::to_string(Parent);
			return false;
		}
	}

	for (int32 V = 0; V < NumVertices; ++V)
	{
		float Sum = 0.0f;
		for (int32 K = 0; K < MaxBoneInfluences; ++K)
		{
			const FSkinWeight& W = SkinWeight[V];
			if (W.Weight[K] > 0.0f && W.BoneIndex[K] >= NumBones)
			{
				OutError = "SkeletalMeshData: vertex " + std::to_string(V) + " references bone " + std::to_string(W.BoneIndex[K]);
				return false;
			}
			Sum += W.Weight[K];
		}
		if (std::fabs(Sum - 1.0f) > 1e-3f)
		{
			OutError = "SkeletalMeshData: vertex " + std::to_string(V) + " weights sum to " + std::to_string(Sum) + " (expected 1)";
			return false;
		}
	}

	for (const FSkeletalMeshSection& Section : Sections)
	{
		if (static_cast<uint64>(Section.StartIndex) + Section.IndexCount > static_cast<uint64>(Indices.Num()))
		{
			OutError = "SkeletalMeshData: section range exceeds index count";
			return false;
		}
		if (Section.MaterialSlotIndex >= static_cast<uint32>(MaterialSlots.Num()))
		{
			OutError = "SkeletalMeshData: section material slot " + std::to_string(Section.MaterialSlotIndex) + " out of range";
			return false;
		}
	}
	// 메시 범위 검증 이후의 Cloth 바인딩 검증
	if (!ValidateClothSectionBindings(ClothSectionBindings, Sections, Indices, NumVertices))
	{
		OutError = "SkeletalMeshData: invalid cloth section bindings";
		return false;
	}
	return true;
}
bool FSkeletalMeshData::ValidateClothSectionBindings(const TArray<FClothSectionBinding>& Bindings,
	const TArray<FSkeletalMeshSection>& Sections, const TArray<uint32>& Indices, int32 NumVertices)
{
	if (Bindings.IsEmpty())
	{
		return true;
	}

	// 섹션 중복과 렌더 정점의 소속을 확인할 테이블
	TArray<uint8> BoundSections;
	BoundSections.Init(0, Sections.Num());
	TArray<int32> ClothSectionByVertex;
	ClothSectionByVertex.Init(INDEX_NONE, NumVertices);

	for (const FClothSectionBinding& Binding : Bindings)
	{
		const int32 SectionIndex = Binding.SectionIndex;
		if (!Sections.IsValidIndex(SectionIndex) || BoundSections[SectionIndex])
		{
			return false;
		}

		const FSkeletalMeshSection& Section = Sections[SectionIndex];
		if (Section.IndexCount == 0 || Section.IndexCount % 3 != 0)
		{
			return false;
		}

		const uint32 EndIndex = Section.StartIndex + Section.IndexCount;
		for (uint32 Index = Section.StartIndex; Index < EndIndex; ++Index)
		{
			ClothSectionByVertex[Indices[Index]] = SectionIndex;
		}

		// 고정점과 이동 반경의 섹션 소속 확인
		for (const uint32 Vertex : Binding.PinnedRenderVertices)
		{
			if (Vertex >= static_cast<uint32>(NumVertices) || ClothSectionByVertex[Vertex] != SectionIndex)
			{
				return false;
			}
		}
		for (const auto& [Vertex, Radius] : Binding.MaxDistanceByRenderVertex)
		{
			if (Vertex >= static_cast<uint32>(NumVertices) || ClothSectionByVertex[Vertex] != SectionIndex
				|| !std::isfinite(Radius) || Radius <= 0.0f)
			{
				return false;
			}
		}
		BoundSections[SectionIndex] = 1;
	}

	// 하나의 Cloth 정점을 공유하는 다른 렌더 섹션 확인
	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		const FSkeletalMeshSection& Section = Sections[SectionIndex];
		const uint32 EndIndex = Section.StartIndex + Section.IndexCount;
		for (uint32 Index = Section.StartIndex; Index < EndIndex; ++Index)
		{
			const int32 ClothSection = ClothSectionByVertex[Indices[Index]];
			if (ClothSection != INDEX_NONE && ClothSection != SectionIndex)
			{
				return false;
			}
		}
	}
	return true;
}
bool FSkeletalMeshData::HasSameClothGeometry(const TArray<FVertex>& OtherVertices,
	const TArray<uint32>& OtherIndices, const TArray<FSkeletalMeshSection>& OtherSections) const
{
	// 저장된 렌더 정점 번호를 그대로 사용할 수 있는 배열 대응
	if (BindVertices.Num() != OtherVertices.Num() || !(Indices == OtherIndices)
		|| Sections.Num() != OtherSections.Num())
	{
		return false;
	}

	for (int32 Index = 0; Index < Sections.Num(); ++Index)
	{
		const FSkeletalMeshSection& A = Sections[Index];
		const FSkeletalMeshSection& B = OtherSections[Index];
		if (A.StartIndex != B.StartIndex || A.IndexCount != B.IndexCount
			|| A.MaterialSlotIndex != B.MaterialSlotIndex)
		{
			return false;
		}
	}

	return BindVertices.IsEmpty()
		|| std::memcmp(BindVertices.GetData(), OtherVertices.GetData(),
			sizeof(FVertex) * static_cast<size_t>(BindVertices.Num())) == 0;
}
namespace
{
	// 관절에서 ±BlendHalf 안이면 아래·위 본을 선형으로 섞고, 아니면 자기 칸의 본 하나
	FSkinWeight MakeStickWeight(float Z, int32 Segments, float SegmentLength, float BlendHalf)
	{
		FSkinWeight Result;
		const int32 Joint = static_cast<int32>(std::lround(Z / SegmentLength)); // 가장 가까운 관절 번호
		const float JointZ = Joint * SegmentLength; // 그 관절의 높이
		if (Joint >= 1 && Joint < Segments && std::fabs(Z - JointZ) < BlendHalf) // 관절이 끝부분이 아니다 0의 끝이나 Seg의 끝부분
		{
			const float T = (Z - (JointZ - BlendHalf)) / (2.0f * BlendHalf);
			Result.BoneIndex[0] = static_cast<uint8>(Joint - 1);
			Result.Weight[0] = 1.0f - T;
			Result.BoneIndex[1] = static_cast<uint8>(Joint);
			Result.Weight[1] = T;
			return Result;
		}
		const int32 Segment = std::clamp(static_cast<int32>(Z / SegmentLength), 0, Segments - 1);
		Result.BoneIndex[0] = static_cast<uint8>(Segment);
		Result.Weight[0] = 1.0f;
		return Result;
	}

	void AddStickVertex(FSkeletalMeshData& Data, const FVector& Position, const FVector& Normal, const FVector2& UV,
		int32 Segments, float SegmentLength, float BlendHalf)
	{
		FVertex Vertex;
		Vertex.Position = Position;
		Vertex.Normal = Normal;
		Vertex.UV = UV;
		Data.BindVertices.Add(Vertex);
		Data.SkinWeight.Add(MakeStickWeight(Position.Z, Segments, SegmentLength, BlendHalf));
	}

	// A-B-C-D 둘레 순서의 사각형. 엔진 규칙(앞면 법선 = Cross(B-A, C-A))에 맞게 감는 방향을 고른다
	void AddStickQuad(FSkeletalMeshData& Data, uint32 A, uint32 B, uint32 C, uint32 D, const FVector& Normal)
	{
		const FVector& PA = Data.BindVertices[A].Position;
		const FVector Face = FVector::Cross(Data.BindVertices[B].Position - PA, Data.BindVertices[C].Position - PA);
		if (Face.Dot(Normal) < 0.0f)
			std::swap(B, D);
		for (const uint32 Index : { A, B, C, A, C, D })
			Data.Indices.Add(Index);
	}
}

FSkeletalMeshData MakeTestBoneStick(int32 Segments, float SegmentLength)
{
	Segments = std::clamp(Segments, 1, 256);
	const float HalfWidth = SegmentLength * 0.1f;
	const float BlendHalf = SegmentLength * 0.2f;
	const float Height = Segments * SegmentLength;
	const int32 RingsPerSegment = 10; // 관절이 부드럽게 휘어 보이도록 칸마다 10등분
	const int32 NumRings = Segments * RingsPerSegment + 1;

	FSkeletalMeshData Data;
	Data.SourcePath = "TestBoneStick";

	// 본 i는 Z = i * SegmentLength에 있고 부모는 i - 1. 바인드 포즈에 회전이 없으니 역행렬은 반대 방향 이동
	for (int32 I = 0; I < Segments; ++I)
	{
		FBoneInfo Bone;
		Bone.Name = "Bone_" + std::to_string(I);
		Bone.ParentIndex = I - 1;
		Bone.LocalBindPose.Location = FVector(0.0f, 0.0f, I == 0 ? 0.0f : SegmentLength);
		Bone.InverseBindPose = FMatrix::MakeTranslation(FVector(0.0f, 0.0f, -I * SegmentLength));
		Data.Bones.Add(Bone);
	}

	// 옆면 4개. 면마다 정점을 따로 둬서 모서리 법선이 섞이지 않게 한다
	const FVector Normals[4] = { FVector(1, 0, 0), FVector(0, 1, 0), FVector(-1, 0, 0), FVector(0, -1, 0) };
	for (const FVector& N : Normals)
	{
		// 면 위의 두 모서리: 법선 방향으로 HalfWidth, 옆 방향으로 ±HalfWidth
		const FVector Side(-N.Y, N.X, 0.0f);
		const FVector Left = N * HalfWidth - Side * HalfWidth;
		const FVector Right = N * HalfWidth + Side * HalfWidth;
		const uint32 Base = static_cast<uint32>(Data.BindVertices.Num());
		for (int32 Ring = 0; Ring < NumRings; ++Ring)
		{
			const float Z = Height * Ring / (NumRings - 1);
			const float V = 1.0f - Z / Height;
			AddStickVertex(Data, Left + FVector(0, 0, Z), N, FVector2(0.0f, V), Segments, SegmentLength, BlendHalf);
			AddStickVertex(Data, Right + FVector(0, 0, Z), N, FVector2(1.0f, V), Segments, SegmentLength, BlendHalf);
		}
		for (int32 Ring = 0; Ring + 1 < NumRings; ++Ring)
		{
			const uint32 L0 = Base + Ring * 2, R0 = L0 + 1, L1 = L0 + 2, R1 = L0 + 3;
			AddStickQuad(Data, L0, R0, R1, L1, N);
		}
	}

	// 아래·위 뚜껑
	for (const float Z : { 0.0f, Height })
	{
		const FVector N(0.0f, 0.0f, Z == 0.0f ? -1.0f : 1.0f);
		const uint32 Base = static_cast<uint32>(Data.BindVertices.Num());
		AddStickVertex(Data, FVector(-HalfWidth, -HalfWidth, Z), N, FVector2(0, 0), Segments, SegmentLength, BlendHalf);
		AddStickVertex(Data, FVector(HalfWidth, -HalfWidth, Z), N, FVector2(1, 0), Segments, SegmentLength, BlendHalf);
		AddStickVertex(Data, FVector(HalfWidth, HalfWidth, Z), N, FVector2(1, 1), Segments, SegmentLength, BlendHalf);
		AddStickVertex(Data, FVector(-HalfWidth, HalfWidth, Z), N, FVector2(0, 1), Segments, SegmentLength, BlendHalf);
		AddStickQuad(Data, Base, Base + 1, Base + 2, Base + 3, N);
	}

	FSkeletalMeshSection Section;
	Section.StartIndex = 0;
	Section.IndexCount = static_cast<uint32>(Data.Indices.Num());
	Section.MaterialSlotIndex = 0;
	Data.Sections.Add(Section);

	FSkeletalMaterialSlot Slot;
	Slot.Name = "TestBoneStick";
	Data.MaterialSlots.Add(Slot);

	Data.BindAABB.Min = FVector(-HalfWidth, -HalfWidth, 0.0f);
	Data.BindAABB.Max = FVector(HalfWidth, HalfWidth, Height);
	return Data;
}
