#include "EnginePCH.h"
#include "Editor/MaterialEditor/MaterialEditor.h"
#include "Editor/MaterialEditor/MaterialEditorTab.h"
#include "Asset/AssetManager.h"
#include "Components/StaticMeshComponent.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Engine/Viewport.h"
#include "Engine/World.h"
#include "Math/BoxSphereBounds.h"
#include "Render/Material.h"
#include "Render/Mesh.h"

FMaterialEditor::FMaterialEditor(
	UEditorEngine& InEditorEngine,
	FEditorManager& InEditorManager,
	const FString& InAssetPath,
	UMaterial& InMaterial
)
	: FAssetEditor(InEditorEngine, InEditorManager, InAssetPath)
	, Material(&InMaterial)
{
}

UStaticMesh* FMaterialEditor::ResolvePreviewMesh(EMaterialPreviewShape Shape) const
{
	switch (Shape)
	{
	case EMaterialPreviewShape::Cube:  return UAssetManager::GetAssetByPath<UStaticMesh>("Cube");
	case EMaterialPreviewShape::Plane: return UAssetManager::GetAssetByPath<UStaticMesh>("Plane");
	case EMaterialPreviewShape::Sphere:
	default:                           return UAssetManager::GetAssetByPath<UStaticMesh>("Sphere");
	}
}

bool FMaterialEditor::InitializeResources()
{
	if (!Material)
		return false;

	if (!FAssetEditor::InitializeResources())
		return false;

	auto Component = PreviewScene.AddComponent(UStaticMeshComponent::StaticClass());
	MeshComponent = Cast<UStaticMeshComponent>(Component);
	if (!MeshComponent)
		return false;

	SetPreviewShape(EMaterialPreviewShape::Sphere);
	FocusPreviewMesh();
	return true;
}

TUniquePtr<IEditorTab> FMaterialEditor::CreateTab()
{
	return MakeUnique<FMaterialEditorTab>(*this);
}

void FMaterialEditor::SetPreviewShape(EMaterialPreviewShape Shape)
{
	PreviewShape = Shape;
	if (!MeshComponent)
		return;

	UStaticMesh* Mesh = ResolvePreviewMesh(Shape);
	if (!Mesh)
		return;

	MeshComponent->SetStaticMesh(Mesh);
	if (Shape == EMaterialPreviewShape::Plane)
		MeshComponent->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
	else
		MeshComponent->SetRelativeRotation(FRotator(0.0f, 0.0f, 0.0f));
	MeshComponent->SetMaterial(0, Material);
	MeshComponent->MarkRenderStateDirty();
	FocusPreviewMesh();
}

void FMaterialEditor::FocusPreviewMesh()
{
	if (!ViewportClient || !MeshComponent)
		return;

	UStaticMesh* Mesh = MeshComponent->GetStaticMesh();
	if (!Mesh)
		return;

	const FBox& Box = Mesh->GetRenderBounds();
	const FVector Extent = (Box.Max - Box.Min) * 0.5f;
	const float Radius = std::max(Extent.Size(), 0.75f);
	FBoxSphereBounds Bound
	{
		.Origin = (Box.Min + Box.Max) * 0.5f,
		.BoxExtent = Extent,
		.SphereRadius = Radius
	};
	ViewportClient->FocusOnBounds(Bound);
}

void FMaterialEditor::SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input)
{
	ViewportWidth = Width;
	ViewportHeight = Height;
	bViewportVisible = Width > 0 && Height > 0;
	CameraInput = Input;
}

void FMaterialEditor::MarkDirty()
{
	bDirty = true;
	if (Material)
		Material->SyncLegacyStates();
	if (MeshComponent)
		MeshComponent->MarkRenderStateDirty();
}

bool FMaterialEditor::Save()
{
	if (!Material)
		return false;

	FString Error;
	if (!UAssetManager::SaveMaterial(*Material, GetAssetPath(), Error))
	{
		HTR_LOG(Error, "Failed to save material '{}': {}", GetAssetPath(), Error);
		return false;
	}

	bDirty = false;
	HTR_LOG(Info, "Saved material: {}", GetAssetPath());
	return true;
}

bool FMaterialEditor::ConfirmClose()
{
	if (!IsDirty() || bCloseConfirmed)
		return true;

	bUnsavedPromptRequested = true;
	return false;
}

void FMaterialEditor::CloseWithoutPrompt()
{
	bCloseConfirmed = true;
	Close();
}

bool FMaterialEditor::ConsumeUnsavedPromptRequest()
{
	const bool bRequested = bUnsavedPromptRequested;
	bUnsavedPromptRequested = false;
	return bRequested;
}

void FMaterialEditor::Tick(float DeltaTime)
{
	if (!ViewportClient || !ViewportClient->GetWorld())
		return;

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

	CameraInput = {};
	FAssetEditor::Tick(DeltaTime);
}

void FMaterialEditor::OnRenderPreview()
{
	const bool bRender = bViewportVisible;
	bViewportVisible = false;
	if (!bRender || !Viewport || !ViewportClient || !ViewportClient->GetWorld())
		return;

	Viewport->UpdateViewportRHI(false, ViewportWidth, ViewportHeight);
	if (!Viewport->GetViewTargets().IsValidFor(ViewportWidth, ViewportHeight))
		return;

	if (bInitialFocusPending)
	{
		FocusPreviewMesh();
		bInitialFocusPending = false;
	}

	Viewport->Draw();
}

void FMaterialEditor::ShutdownResources()
{
	MeshComponent = nullptr;
	Material = nullptr;
	FAssetEditor::ShutdownResources();
}
