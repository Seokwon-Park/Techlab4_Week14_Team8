#include "EnginePCH.h"
#include "Editor/StaticMeshEditor/StaticMeshEditor.h"
#include "StaticMeshEditorTab.h"
#include "Components/StaticMeshComponent.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Launch/LaunchEngineLoop.h"
#include "Engine/World.h"
#include "Math/BoxSphereBounds.h"
#include "Physics/BodySetup.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Render/Mesh.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"
#include <algorithm>
#include <cmath>

FStaticMeshEditor::FStaticMeshEditor(
	UEditorEngine& InEditorEngine,
	FEditorManager& InEditorManager,
	const FString& InAssetPath,
	UStaticMesh& InMesh
)
	: FAssetEditor(InEditorEngine, InEditorManager, InAssetPath)
	, Mesh(&InMesh)
{
}

bool FStaticMeshEditor::InitializeResources()
{
	// 메시가 없음
	if (!Mesh)
	{
		return false;
	}

	// 메시의 Vertex가 비어있음
	if (Mesh->GetMeshData().Vertices.IsEmpty())
	{
		return false;
	}

	// 메시의 Index가 비어있음
	if (Mesh->GetMeshData().Indices.IsEmpty())
	{
		return false;
	}
	
	// 부모 InitializeResources 호출 (FPreviewScene 초기화 및 세팅)
	if (!FAssetEditor::InitializeResources())
	{
		return false;
	}

	// PreviewScene에 StaticMeshComponent 추가
	auto Component = PreviewScene.AddComponent(UStaticMeshComponent::StaticClass());
	MeshComponent = Cast<UStaticMeshComponent>(Component);
	if (!MeshComponent)
	{
		return false;
	}

	// StaticMeshComponent에 메시를 설정
	MeshComponent->SetStaticMesh(Mesh);

	// 카메라를 메시에 포커싱
	FocusMesh();

	return true;
}

TUniquePtr<IEditorTab> FStaticMeshEditor::CreateTab()
{
	return MakeUnique<FStaticMeshEditorTab>(*this);
}

void FStaticMeshEditor::FocusMesh()
{
	if (!ViewportClient)
	{
		return;
	}

	if (!Mesh)
	{
		return;
	}

	// 메시의 AABB를 가져와서 BoxSphereBounds로 변환
	const FBox& Box = Mesh->GetRenderBounds();
	const FVector Extent = (Box.Max - Box.Min) * 0.5f;
	FBoxSphereBounds Bound
	{
		.Origin = (Box.Min + Box.Max) * 0.5f,
		.BoxExtent = Extent,
		.SphereRadius = Extent.Size()
	};

	// AABB로 ViewportClient의 카메라를 메시에 포커싱
	ViewportClient->FocusOnBounds(Bound);
}

void FStaticMeshEditor::SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input)
{
	ViewportWidth = Width;
	ViewportHeight = Height;
	bViewportVisible = Width > 0 && Height > 0;
	CameraInput = Input;
}

void FStaticMeshEditor::Tick(float DeltaTime)
{
	if (!ViewportClient)
	{
		return;
	}

	if (!ViewportClient->GetWorld())
	{
		return;
	}

	// 뷰포트가 보이면 카메라 이동을 적용 (우클릭+휠 속도 조절, 원근/직교/좌드래그 지원)
	if (bViewportVisible)
	{
		FViewportCameraInput AdjustedInput = CameraInput;
		if (AdjustedInput.bCaptured && AdjustedInput.Wheel != 0.0f)
		{
			CameraSpeed = FMath::Clamp(CameraSpeed * powf(1.2f, AdjustedInput.Wheel / 120.0f), 0.1f, 50.0f);
			AdjustedInput.Wheel = 0.0f;
		}
		FViewportCameraState Camera{
			.Location = ViewportClient->GetViewLocation(),
			.Rotation = ViewportClient->GetViewRotation(),
			.OrthoWidth = ViewportClient->GetOrthoWidth(),
			.ViewWidth = static_cast<float>(ViewportWidth),
			.bPerspective = ViewportClient->IsPerspective()
		};
		ApplyViewportCameraMovement(Camera, AdjustedInput, DeltaTime, CameraSpeed, 0.2f);
		ViewportClient->SetViewLocation(Camera.Location);
		ViewportClient->SetViewRotation(Camera.Rotation);
		ViewportClient->SetOrthoWidth(Camera.OrthoWidth);
	}

	// 카메라 입력 리셋
	CameraInput = {};

	// 부모 Tick 호출 (PreviewScene Tick)
	FAssetEditor::Tick(DeltaTime);
}

void FStaticMeshEditor::OnRenderPreview()
{
	// Viewport가 보이지 않으면 렌더링하지 않음
	const bool bRender = bViewportVisible;
	bViewportVisible = false;
	if (!bRender)
	{
		return;
	}

	if (!ViewportClient)
	{
		return;
	}

	if (!ViewportClient->GetWorld() || !Viewport)
	{
		return;
	}

	// 뷰포트 크기 변경. 크기가 같으면 아무것도 하지 않는다.
	Viewport->UpdateViewportRHI(false, ViewportWidth, ViewportHeight);

	// 뷰포트의 타깃이 없다면 렌더링하지 않음
	if (!Viewport->GetViewTargets().IsValidFor(ViewportWidth, ViewportHeight))
	{
		return;
	}

	// FocusMesh가 아직 호출되지 않았다면 호출
	if (bInitialFocusPending)
	{
		FocusMesh();
		bInitialFocusPending = false;
	}

	// 뷰포트를 그린다. 뷰포트가 자기 클라이언트의 Draw를 부른다.
	Viewport->Draw();
}

void FStaticMeshEditor::OnRenderLine(FLineBatcher& LineBatcher)
{
	if (!bShowCollision || !Mesh || !MeshComponent) return;
	UBodySetup* BodySetup = Mesh->GetBodySetup();
	if (!BodySetup) return;
	FPhysicsDebugDraw DebugDraw;
	const FTransform WorldTransform = MeshComponent->GetTransform();
	DebugDraw.DrawAggregateGeom(&LineBatcher, BodySetup->AggGeom, WorldTransform, FVector4(0.25f, 0.95f, 0.45f, 1.0f), 0.12f);
}

namespace
{
	bool SerializeBodySetupBytes(UBodySetup* Setup, TArray<uint8>& OutBytes)
	{
		if (!Setup) return false;
		OutBytes.Empty();
		FMemoryWriter Writer(OutBytes);
		{
			FJsonArchiveOutputFormatter Formatter(Writer);
			FStructuredArchive Archive(Formatter);
			Setup->Serialize(Archive.Open().EnterRecord());
		}
		return !Writer.IsError();
	}

	bool DeserializeBodySetupBytes(UBodySetup* Setup, const TArray<uint8>& Bytes)
	{
		if (!Setup || Bytes.IsEmpty()) return false;
		FMemoryReader Reader(Bytes, true);
		FJsonArchiveInputFormatter Formatter(Reader);
		if (Reader.IsError()) return false;
		FStructuredArchive Archive(Formatter);
		Setup->Serialize(Archive.Open().EnterRecord());
		return !Reader.IsError();
	}
}

void FStaticMeshEditor::PushCollisionUndo()
{
	if (!Mesh) return;
	TArray<uint8> Bytes;
	if (SerializeBodySetupBytes(Mesh->GetBodySetup(), Bytes))
	{
		CollisionUndoStack.Add(std::move(Bytes));
		if (CollisionUndoStack.Num() > 50)
			CollisionUndoStack.RemoveAt(0);
		CollisionRedoStack.Empty();
	}
}

void FStaticMeshEditor::UndoCollision()
{
	if (!Mesh || CollisionUndoStack.IsEmpty()) return;
	UBodySetup* Setup = Mesh->GetBodySetup();
	if (!Setup) return;
	TArray<uint8> Current;
	if (SerializeBodySetupBytes(Setup, Current))
		CollisionRedoStack.Add(std::move(Current));
	const TArray<uint8> Prev = std::move(CollisionUndoStack.Last());
	CollisionUndoStack.RemoveLast();
	DeserializeBodySetupBytes(Setup, Prev);
	bDirty = true;
}

void FStaticMeshEditor::RedoCollision()
{
	if (!Mesh || CollisionRedoStack.IsEmpty()) return;
	UBodySetup* Setup = Mesh->GetBodySetup();
	if (!Setup) return;
	TArray<uint8> Current;
	if (SerializeBodySetupBytes(Setup, Current))
		CollisionUndoStack.Add(std::move(Current));
	const TArray<uint8> Next = std::move(CollisionRedoStack.Last());
	CollisionRedoStack.RemoveLast();
	DeserializeBodySetupBytes(Setup, Next);
	bDirty = true;
}

void FStaticMeshEditor::AddAutoFitBox()
{
	if (!Mesh) return;
	UBodySetup* Setup = Mesh->GetBodySetup();
	if (!Setup) return;
	PushCollisionUndo();
	const FBox& Box = Mesh->GetRenderBounds();
	const FVector Size = Box.Max - Box.Min;
	FKBoxElem Elem;
	Elem.Center = (Box.Min + Box.Max) * 0.5f;
	Elem.X = std::max(Size.X, 0.01f);
	Elem.Y = std::max(Size.Y, 0.01f);
	Elem.Z = std::max(Size.Z, 0.01f);
	Setup->AggGeom.BoxElems.Add(Elem);
	bDirty = true;
}

void FStaticMeshEditor::AddAutoFitSphere()
{
	if (!Mesh) return;
	UBodySetup* Setup = Mesh->GetBodySetup();
	if (!Setup) return;
	PushCollisionUndo();
	const FBox& Box = Mesh->GetRenderBounds();
	const FVector Extent = (Box.Max - Box.Min) * 0.5f;
	FKSphereElem Elem;
	Elem.Center = (Box.Min + Box.Max) * 0.5f;
	Elem.Radius = std::max(Extent.Size(), 0.01f);
	Setup->AggGeom.SphereElems.Add(Elem);
	bDirty = true;
}

void FStaticMeshEditor::AddAutoFitCapsule()
{
	if (!Mesh) return;
	UBodySetup* Setup = Mesh->GetBodySetup();
	if (!Setup) return;
	PushCollisionUndo();
	const FBox& Box = Mesh->GetRenderBounds();
	const FVector Size = Box.Max - Box.Min;
	FKSphylElem Elem;
	Elem.Center = (Box.Min + Box.Max) * 0.5f;
	Elem.Radius = std::max(std::max(Size.X, Size.Y) * 0.5f, 0.01f);
	Elem.Length = std::max(Size.Z - Elem.Radius * 2.0f, 0.01f);
	Setup->AggGeom.SphylElems.Add(Elem);
	bDirty = true;
}

void FStaticMeshEditor::AddAutoFitConvex()
{
	if (!Mesh) return;
	UBodySetup* Setup = Mesh->GetBodySetup();
	if (!Setup) return;
	const auto& Vertices = Mesh->GetMeshData().Vertices;
	if (Vertices.IsEmpty()) return;
	PushCollisionUndo();
	FKConvexElem Elem;
	// 최대 256개 정점으로 균등 샘플링해 Convex Hull 후보 정점을 구성한다.
	const int32 Stride = std::max(1, static_cast<int32>(Vertices.Num() / 256));
	for (int32 i = 0; i < Vertices.Num(); i += Stride)
		Elem.VertexData.Add(Vertices[i].Position);
	Setup->AggGeom.ConvexElems.Add(std::move(Elem));
	bDirty = true;
}

void FStaticMeshEditor::ClearCollision()
{
	if (!Mesh) return;
	UBodySetup* Setup = Mesh->GetBodySetup();
	if (!Setup) return;
	PushCollisionUndo();
	Setup->AggGeom.BoxElems.Empty();
	Setup->AggGeom.SphereElems.Empty();
	Setup->AggGeom.SphylElems.Empty();
	Setup->AggGeom.ConvexElems.Empty();
	bDirty = true;
}

bool FStaticMeshEditor::SaveBodySetup()
{
	if (!Mesh) return false;
	FString Error;
	if (!Mesh->SaveBodySetup(&Error))
	{
		HTR_LOG(Error, "Failed to save .bodysetup: {}", Error);
		return false;
	}
	bDirty = false;
	HTR_LOG(Info, "Saved collision .bodysetup: {}", Mesh->GetBodySetupFilePath());
	return true;
}

void FStaticMeshEditor::ShutdownResources()
{
	MeshComponent = nullptr;
	Mesh = nullptr;
	FAssetEditor::ShutdownResources();
}
