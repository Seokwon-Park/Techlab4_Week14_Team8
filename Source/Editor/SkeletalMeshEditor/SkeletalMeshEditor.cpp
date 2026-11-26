#include "EnginePCH.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "SkeletalMeshEditorTab.h"
#include "Editor/AssetEditor/EditorManager.h"
#include "Asset/AssetUtils.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Collision/Ray.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/LineBatcher.h"
#include "Render/SkeletalMesh.h"
#include "Render/SkeletalMeshObject.h"
#include <algorithm>
#include <cmath>

bool FSkeletalMeshEditor::InitializeResources()
{
	if (!InitializePreview()) return false;
	BoneGizmoTarget = Cast<USceneComponent>(PreviewScene.AddComponent(USceneComponent::StaticClass()));
	if (!BoneGizmoTarget) return false;
	BoneGizmoTarget->DetachFromParent();
	return true;
}

void FSkeletalMeshEditor::ShutdownResources()
{
	BoneGizmo.EndDrag();
	BoneGizmo.SetTarget(nullptr);
	BoneGizmoTarget = nullptr;
	FSkeletalPreviewEditor::ShutdownResources();
}

void FSkeletalMeshEditor::TickAfterWorld(float DeltaTime)
{
	if (bViewportVisible && MeshComponent)
	{
		MeshComponent->TickClothPreview(DeltaTime);
	}
	if (bClothPaintMode)
	{
		UpdateClothPaintStroke();
		return;
	}

	// 기즈모가 입력을 쓰지 않았을 때만 본을 피킹하고, 마지막에 기즈모를 선택한 본 위치로 맞춘다
	if (!UpdateBoneGizmo())
		UpdateBonePicking();
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::SelectClothSection(int32 SectionIndex)
{
	// 새 섹션의 고유 렌더 정점과 적용된 이동 반경 복원
	bClothStrokeActive = false;
	ClothSectionIndex = SectionIndex;
	ClothSectionVertices.Reset();
	ClothPinnedVertices.Reset();
	ClothMaxDistances.Reset();

	const FSkeletalMeshLODRenderData& LOD = Mesh->GetRenderData().LODRenderData[0];
	const FSkeletalMeshSection& Section = LOD.Sections[SectionIndex];
	TSet<uint32> SeenVertices;
	for (uint32 Index = Section.StartIndex; Index < Section.StartIndex + Section.IndexCount; ++Index)
	{
		const uint32 VertexIndex = LOD.Indices[Index];
		if (SeenVertices.Add(VertexIndex))
		{
			ClothSectionVertices.Add(VertexIndex);
		}
	}
	if (ClothSectionVertices.IsEmpty())
	{
		bClothPaintMode = false;
	}

	for (const FClothSectionBinding& Binding : Mesh->GetClothSectionBindings())
	{
		if (Binding.SectionIndex == SectionIndex)
		{
			for (const uint32 VertexIndex : Binding.PinnedRenderVertices)
			{
				ClothPinnedVertices.Add(VertexIndex);
			}
			ClothMaxDistances = Binding.MaxDistanceByRenderVertex;
			break;
		}
	}
}

void FSkeletalMeshEditor::SetClothPaintMode(bool bEnabled)
{
	// 브러시 활성화 전 스키닝 포즈 복원
	CancelViewportInteraction();
	if (bEnabled && MeshComponent)
	{
		MeshComponent->StopClothPreview();
	}
	bClothPaintMode = bEnabled;
}

void FSkeletalMeshEditor::SetClothBrushMaxDistance(float MaxDistance)
{
	// 유한한 미터 단위 입력과 0의 고정점 의미 유지
	if (std::isfinite(MaxDistance))
	{
		ClothBrushMaxDistance = FMath::Max(0.0f, MaxDistance);
	}
}

bool FSkeletalMeshEditor::ApplyClothBinding(FString& OutError)
{
	// 에셋 키로부터 기존 바이너리와 임포트 메타데이터 로드
	const FString Path = FAssetUtils::ResolveAssetFilePath(Mesh->GetPath());
	FFbxBakedMesh Baked;
	if (!FFbxMeshBake::Read(Path, Baked, OutError))
	{
		return false;
	}

	const FSkeletalMeshLODRenderData& LOD = Mesh->GetRenderData().LODRenderData[0];
	if (Baked.Kind != EFbxMeshKind::Skeletal
		|| !Baked.SkeletalMesh.HasSameClothGeometry(LOD.BindVertices, LOD.Indices, LOD.Sections))
	{
		OutError = "The mesh on disk differs from the open mesh. Restart the editor before saving Cloth.";
		return false;
	}

	// 디스크의 다른 섹션을 보존한 선택 섹션 교체
	auto& Bindings = Baked.SkeletalMesh.ClothSectionBindings;
	// 저장된 동일 섹션 바인딩 제거
	for (int32 Index = 0; Index < Bindings.Num(); ++Index)
	{
		if (Bindings[Index].SectionIndex == ClothSectionIndex)
		{
			Bindings.RemoveAt(Index);
			break;
		}
	}

	FClothSectionBinding Binding;
	Binding.SectionIndex = ClothSectionIndex;
	Binding.MaxDistanceByRenderVertex = ClothMaxDistances;
	Binding.PinnedRenderVertices.Reserve(ClothPinnedVertices.Num());
	for (const uint32 Vertex : ClothSectionVertices)
	{
		if (ClothPinnedVertices.Contains(Vertex))
		{
			Binding.PinnedRenderVertices.Add(Vertex);
		}
	}
	Bindings.Add(std::move(Binding));

	// 저장 성공 이후에만 메모리와 프리뷰 상태 반영
	if (!Baked.SkeletalMesh.Validate(OutError) || !FFbxMeshBake::WriteAtomically(Path, Baked, OutError))
	{
		return false;
	}

	if (!Mesh->SetClothSectionBindings(std::move(Bindings)))
	{
		OutError = "Cloth was saved. Restart the editor to reload the mesh.";
		return false;
	}

	if (MeshComponent && MeshComponent->HasClothSimulation())
	{
		MeshComponent->RestartClothPreview();
	}
	return true;
}

void FSkeletalMeshEditor::UpdateClothPaintStroke()
{
	// 프리뷰에서 시작한 드래그를 다음 마우스 위치까지 연결
	if (!CanProcessViewportInput() || !PointerInput.bHovered || PointerInput.bCancel)
	{
		bClothStrokeActive = false;
		return;
	}
	if (PointerInput.bMousePressed)
	{
		PreviousClothMouseUV = PointerInput.MouseUV;
		bClothStrokeActive = true;
	}
	if (!bClothStrokeActive)
	{
		return;
	}

	if (PointerInput.bMousePressed || PreviousClothMouseUV != PointerInput.MouseUV
		|| bPreviousClothErase != PointerInput.bShiftDown)
	{
		const FVector2 Start(PreviousClothMouseUV.X * ViewportWidth, PreviousClothMouseUV.Y * ViewportHeight);
		const FVector2 End(PointerInput.MouseUV.X * ViewportWidth, PointerInput.MouseUV.Y * ViewportHeight);
		PaintClothVertices(Start, End);
	}
	PreviousClothMouseUV = PointerInput.MouseUV;
	bPreviousClothErase = PointerInput.bShiftDown;
	bClothStrokeActive = PointerInput.bMouseDown;
}

void FSkeletalMeshEditor::PaintClothVertices(const FVector2& Start, const FVector2& End)
{
	const FSkeletalMeshObject* MeshObject = MeshComponent ? MeshComponent->GetMeshObject() : nullptr;
	const TArray<FVertex>* Vertices = MeshObject ? MeshObject->GetCPUSkinnedVertices() : nullptr;
	if (!Vertices || ClothSectionVertices.IsEmpty())
	{
		return;
	}

	constexpr float MinClipW = 0.0001f;
	const FMatrix World = MeshComponent->GetWorldMatrix();
	const FMatrix LocalToClip = World * ViewportClient->GetViewProjectionMatrix();
	const auto ToScreen = [this](const FVector4& Clip)
	{
		return FVector2((0.5f + 0.5f * Clip.X / Clip.W) * ViewportWidth,
			(0.5f - 0.5f * Clip.Y / Clip.W) * ViewportHeight);
	};

	// 브러시가 지나간 범위에서 상태가 바뀔 정점 수집
	struct FBrushCandidate
	{
		uint32 VertexIndex;
		FVector2 Screen;
		FRay LocalRay;
		float OcclusionLimit;
		bool bOccluded = false;
		bool bOnVisibleFace = false;
	};

	TArray<FBrushCandidate> Candidates;
	const bool bErase = PointerInput.bShiftDown;
	const bool bPin = ClothBrushMaxDistance == 0.0f;
	const FMatrix WorldToLocal = World.Inverse();
	for (const uint32 VertexIndex : ClothSectionVertices)
	{
		const bool bPinned = ClothPinnedVertices.Contains(VertexIndex);
		const float* OldMaxDistance = ClothMaxDistances.Find(VertexIndex);
		const bool bHasSameValue = bPin
			? bPinned && !OldMaxDistance
			: !bPinned && OldMaxDistance && *OldMaxDistance == ClothBrushMaxDistance;
		if (bErase ? !bPinned && !OldMaxDistance : bHasSameValue)
		{
			continue;
		}

		const FVector Position = (*Vertices)[VertexIndex].Position;
		const FVector4 Clip = FVector4(Position, 1.0f) * LocalToClip;
		if (Clip.W < MinClipW || Clip.Z < 0.0f || Clip.Z > Clip.W)
		{
			continue;
		}

		const FVector2 Screen = ToScreen(Clip);
		if (Screen.X < 0.0f || Screen.X >= ViewportWidth || Screen.Y < 0.0f || Screen.Y >= ViewportHeight
			|| DistanceToSegment(Screen, Start, End) > ClothBrushRadius)
		{
			continue;
		}

		const FRay Ray = ViewportClient->DeprojectScreenToWorld(Screen);
		const float Distance = (World.TransformPosition(Position) - Ray.Origin).Dot(Ray.Direction);
		const FRay LocalRay{ WorldToLocal.TransformPosition(Ray.Origin), WorldToLocal.TransformVector(Ray.Direction) };
		const float Tolerance = 0.0001f * FMath::Max(1.0f, Distance);
		Candidates.Add({ VertexIndex, Screen, LocalRay, Distance - Tolerance });
	}
	if (Candidates.IsEmpty())
	{
		return;
	}

	// 브러시 영역과 겹치는 삼각형으로 후보 정점의 가림 검사
	const FVector2 BrushMin(FMath::Min(Start.X, End.X) - ClothBrushRadius, FMath::Min(Start.Y, End.Y) - ClothBrushRadius);
	const FVector2 BrushMax(FMath::Max(Start.X, End.X) + ClothBrushRadius, FMath::Max(Start.Y, End.Y) + ClothBrushRadius);
	TArray<FVector4> ClipVertices;
	ClipVertices.Reserve(Vertices->Num());
	for (const FVertex& Vertex : *Vertices)
	{
		ClipVertices.Add(FVector4(Vertex.Position, 1.0f) * LocalToClip);
	}

	const FSkeletalMeshLODRenderData& LOD = Mesh->GetRenderData().LODRenderData[0];
	const bool bCullBackFaces = !ViewportClient->IsWireframe();
	for (const FSkeletalMeshSection& Section : LOD.Sections)
	{
		const uint32 EndIndex = Section.StartIndex + Section.IndexCount;
		for (uint32 Index = Section.StartIndex; Index + 2 < EndIndex; Index += 3)
		{
			const uint32 A = LOD.Indices[Index];
			const uint32 B = LOD.Indices[Index + 1];
			const uint32 C = LOD.Indices[Index + 2];
			const FVector4& ClipA = ClipVertices[A];
			const FVector4& ClipB = ClipVertices[B];
			const FVector4& ClipC = ClipVertices[C];
			// D3D 화면의 시계 방향 앞면 판정
			if (bCullBackFaces && FVector(ClipA.X, ClipA.Y, ClipA.W).Dot(
				FVector(ClipB.X, ClipB.Y, ClipB.W).Cross(FVector(ClipC.X, ClipC.Y, ClipC.W))) >= 0.0f)
			{
				continue;
			}

			FVector2 Min = BrushMin;
			FVector2 Max = BrushMax;
			if (ClipA.W >= MinClipW && ClipB.W >= MinClipW && ClipC.W >= MinClipW)
			{
				const FVector2 ScreenA = ToScreen(ClipA);
				const FVector2 ScreenB = ToScreen(ClipB);
				const FVector2 ScreenC = ToScreen(ClipC);
				Min = FVector2(std::min({ ScreenA.X, ScreenB.X, ScreenC.X }), std::min({ ScreenA.Y, ScreenB.Y, ScreenC.Y }));
				Max = FVector2(std::max({ ScreenA.X, ScreenB.X, ScreenC.X }), std::max({ ScreenA.Y, ScreenB.Y, ScreenC.Y }));
				if (Max.X < BrushMin.X || Min.X > BrushMax.X || Max.Y < BrushMin.Y || Min.Y > BrushMax.Y)
				{
					continue;
				}
			}

			for (FBrushCandidate& Candidate : Candidates)
			{
				const uint32 VertexIndex = Candidate.VertexIndex;
				if (VertexIndex == A || VertexIndex == B || VertexIndex == C)
				{
					Candidate.bOnVisibleFace = true;
					continue;
				}
				const FVector2& Screen = Candidate.Screen;
				if (Candidate.bOccluded
					|| Screen.X < Min.X || Screen.X > Max.X || Screen.Y < Min.Y || Screen.Y > Max.Y)
				{
					continue;
				}

				float HitDistance = 0.0f;
				if (RayIntersectsTriangle(Candidate.LocalRay, (*Vertices)[A].Position, (*Vertices)[B].Position,
					(*Vertices)[C].Position, HitDistance) && HitDistance < Candidate.OcclusionLimit)
				{
					const FVector4 HitClip = FVector4(Candidate.LocalRay.PointAt(HitDistance), 1.0f) * LocalToClip;
					Candidate.bOccluded = HitClip.Z >= 0.0f && HitClip.Z <= HitClip.W;
				}
			}
		}
	}

	for (const FBrushCandidate& Candidate : Candidates)
	{
		if (!Candidate.bOnVisibleFace || Candidate.bOccluded)
		{
			continue;
		}

		// 가시 정점의 기존 값 삭제 또는 교체
		if (bErase)
		{
			ClothPinnedVertices.Remove(Candidate.VertexIndex);
			ClothMaxDistances.Remove(Candidate.VertexIndex);
		}
		else if (bPin)
		{
			ClothMaxDistances.Remove(Candidate.VertexIndex);
			ClothPinnedVertices.Add(Candidate.VertexIndex);
		}
		else
		{
			ClothPinnedVertices.Remove(Candidate.VertexIndex);
			ClothMaxDistances.Add(Candidate.VertexIndex, ClothBrushMaxDistance);
		}
	}
}

void FSkeletalMeshEditor::OnRenderLine(FLineBatcher& LineBatcher)
{
	// Cloth Solver에 전달한 충돌 형상의 와이어 표시
	if (MeshComponent)
	{
		MeshComponent->DrawClothBodyCollision(LineBatcher);
	}

	if (!bClothPaintMode)
	{
		FSkeletalPreviewEditor::OnRenderLine(LineBatcher);
		return;
	}

	// 고정점과 거리 제한점의 프리뷰 마커
	const FSkeletalMeshObject* MeshObject = MeshComponent ? MeshComponent->GetMeshObject() : nullptr;
	const TArray<FVertex>* Vertices = MeshObject ? MeshObject->GetCPUSkinnedVertices() : nullptr;
	if (!Vertices || (ClothPinnedVertices.IsEmpty() && ClothMaxDistances.IsEmpty()))
	{
		return;
	}

	const FMatrix World = MeshComponent->GetWorldMatrix();
	const FVector4 PinColor(1.0f, 0.75f, 0.1f, 1.0f);
	const FVector4 CapColor(0.2f, 0.75f, 1.0f, 1.0f);
	const FVector X(BoneSphereSize, 0.0f, 0.0f);
	const FVector Y(0.0f, BoneSphereSize, 0.0f);
	const FVector Z(0.0f, 0.0f, BoneSphereSize);
	const auto DrawMarker = [&](uint32 VertexIndex, const FVector4& Color)
	{
		const FVector Position = World.TransformPosition((*Vertices)[VertexIndex].Position);
		LineBatcher.AddLine(Position - X, Position + X, Color);
		LineBatcher.AddLine(Position - Y, Position + Y, Color);
		LineBatcher.AddLine(Position - Z, Position + Z, Color);
	};
	for (const uint32 VertexIndex : ClothPinnedVertices)
	{
		DrawMarker(VertexIndex, PinColor);
	}
	for (const auto& MaxDistance : ClothMaxDistances)
	{
		DrawMarker(MaxDistance.first, CapColor);
	}
}

void FSkeletalMeshEditor::OpenPhysicsAssetEditor()
{
	EditorManager.OpenPhysicsAssetEditor(GetAssetPath(), Mesh);
}

TUniquePtr<IEditorTab> FSkeletalMeshEditor::CreateTab()
{
	return MakeUnique<FSkeletalMeshEditorTab>(*this);
}

void FSkeletalMeshEditor::SetGizmoMode(UE::Widget::EWidgetMode Mode)
{
	BoneGizmo.EndDrag();
	ModeTools.SetWidgetMode(Mode);
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::SetGizmoSpace(ECoordSystem Space)
{
	BoneGizmo.EndDrag();
	ModeTools.SetCoordSystem(Space);
	SyncBoneGizmo();
}

void FSkeletalMeshEditor::SyncBoneGizmo()
{
	if (!MeshComponent || !BoneGizmoTarget || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		BoneGizmo.SetTarget(nullptr);
		return;
	}

	if (BoneGizmo.IsUsing())
	{
		return;
	}

	const FMatrix BoneWorld = MeshComponent->GetBoneWorldMatrix(SelectedBoneIndex);
	const FVector Location = BoneWorld.TransformPosition(FVector::ZeroVector);
	const FRotator Rotation = GetBoneWorldRotation(SelectedBoneIndex).ToFRotator();

	// 스케일은 조작 배율로만 사용합니다. 부모의 비균일 스케일과 전단을 축 회전에 섞지 않습니다.
	BoneGizmoTarget->SetTransform(FTransform(Rotation, Location));
	BoneGizmo.SetTarget(BoneGizmoTarget);
}

FQuat FSkeletalMeshEditor::GetBoneWorldRotation(int32 BoneIndex) const
{
	FQuat Rotation = FQuat::Identity();
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	while (BoneIndex >= 0)
	{
		Rotation = MeshComponent->GetBoneLocalTransform(BoneIndex).Rotation * Rotation;
		BoneIndex = Bones[BoneIndex].ParentIndex;
	}

	return (MeshComponent->GetWorldRotation().Quaternion() * Rotation).Normalized();
}

bool FSkeletalMeshEditor::UpdateBoneGizmo()
{
	if
		(
			// 공통 뷰포트 입력 조건을 만족하지 않으면 입력을 무시
			!CanProcessViewportInput() ||

			// BoneGizmo의 대상이 없으면 입력을 무시
			!BoneGizmo.GetTarget()
			)
	{
		BoneGizmo.EndDrag();
		return false;
	}

	if (PointerInput.bCancel)
	{
		if (BoneGizmo.IsUsing())
		{
			MeshComponent->SetBoneLocalTransform(SelectedBoneIndex, DragStartBoneTransform);
			MeshComponent->RefreshBoneTransformsNow();
		}
		BoneGizmo.EndDrag();
		return false;
	}

	const bool bWasUsing = BoneGizmo.IsUsing();
	if ((!PointerInput.bHovered && !bWasUsing) || CameraInput.bCaptured)
	{
		BoneGizmo.EndDrag();
		return false;
	}

	SyncBoneGizmo();
	const int32 Width = static_cast<int32>(ViewportClient->GetWidth());
	const int32 Height = static_cast<int32>(ViewportClient->GetHeight());
	const FVector2 MousePosition(PointerInput.MouseUV.X * Width, PointerInput.MouseUV.Y * Height);
	const FRay MouseRay = ViewportClient->DeprojectScreenToWorld(MousePosition);

	// 클릭한 프레임에만 시작하고, 버튼을 놓는 프레임의 마지막 마우스 위치도 반영합니다.
	const bool bUpdateDrag = bWasUsing || (PointerInput.bMousePressed && PointerInput.bMouseDown);
	BoneGizmo.Update(MouseRay, MousePosition, ViewportClient->GetViewProjectionMatrix(),
		Width, Height, bUpdateDrag, ViewportClient->GetViewLocation(), !ViewportClient->IsPerspective());

	if (!bWasUsing && BoneGizmo.IsUsing())
	{
		const int32 ParentIndex = Mesh->GetRefSkeleton()[SelectedBoneIndex].ParentIndex;
		const FMatrix ParentWorld = ParentIndex >= 0
			? MeshComponent->GetBoneWorldMatrix(ParentIndex) : MeshComponent->GetWorldMatrix();

		// 스케일이 0인 부모의 역행렬로 이동을 계산할 수 없습니다.
		if (ModeTools.GetWidgetMode() == UE::Widget::WM_Translate && fabsf(ParentWorld.Determinant()) < 1e-12f)
		{
			BoneGizmo.EndDrag();
			return true;
		}

		DragStartBoneTransform = MeshComponent->GetBoneLocalTransform(SelectedBoneIndex);
		DragParentRotation = GetBoneWorldRotation(ParentIndex);
		if (ModeTools.GetWidgetMode() == UE::Widget::WM_Translate)
		{
			DragParentInverse = ParentWorld.Inverse();
		}
	}

	if (BoneGizmo.IsUsing())
	{
		ApplyBoneGizmoTransform();
		if (!PointerInput.bMouseDown)
		{
			BoneGizmo.EndDrag();
		}
	}

	return bWasUsing || BoneGizmo.IsUsing() || BoneGizmo.GetHoveredAxis() >= 0;
}

void FSkeletalMeshEditor::ApplyBoneGizmoTransform()
{
	FTransform Local = DragStartBoneTransform;
	switch (ModeTools.GetWidgetMode())
	{
	case UE::Widget::WM_Translate:
		Local.Location = DragParentInverse.TransformPosition(BoneGizmoTarget->GetWorldLocation());
		break;
	case UE::Widget::WM_Rotate:
		Local.Rotation = (DragParentRotation.Inverse() *
			BoneGizmoTarget->GetRelativeRotation().Quaternion()).Normalized();
		break;
	case UE::Widget::WM_Scale:
		Local.Scale = DragStartBoneTransform.Scale * BoneGizmoTarget->GetRelativeScale3D();
		break;
	}

	// 조작한 성분만 변경하고 나머지는 드래그 시작 시점의 로컬 값을 유지합니다.
	MeshComponent->SetBoneLocalTransform(SelectedBoneIndex, Local);
	MeshComponent->RefreshBoneTransformsNow();
}

bool FSkeletalMeshEditor::GetSelectedBoneLocalTransform(FTransform& OutTransform) const
{
	if (!MeshComponent || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		return false;
	}

	OutTransform = MeshComponent->GetBoneLocalTransform(SelectedBoneIndex);
	return true;
}

void FSkeletalMeshEditor::SetSelectedBoneLocalTransform(const FTransform& Transform)
{
	if (!MeshComponent || SelectedBoneIndex < 0 || SelectedBoneIndex >= BoneItems.Num())
	{
		return;
	}

	// 원본 애셋 대신 프리뷰 컴포넌트의 포즈만 수정합니다. 본 갱신은 다음 World Tick에서 수행합니다.
	BoneGizmo.EndDrag();
	MeshComponent->SetBoneLocalTransform(SelectedBoneIndex, Transform);
}

void FSkeletalMeshEditor::ResetPose()
{
	if (!MeshComponent)
	{
		return;
	}

	BoneGizmo.EndDrag();
	MeshComponent->ResetPose();
}

void FSkeletalMeshEditor::ResetBone(int32 BoneIndex)
{
	if (!Mesh)
	{
		return;
	}

	if (!MeshComponent)
	{
		return;
	}

	if (BoneIndex < 0 || BoneIndex >= Mesh->GetRefSkeleton().Num())
	{
		return;
	}

	BoneGizmo.EndDrag();
	FTransform Transform = Mesh->GetRefSkeleton()[BoneIndex].LocalBindPose;
	MeshComponent->SetBoneLocalTransform(BoneIndex, Transform);
}
