#include "EnginePCH.h"
#include "Editor/BlueprintEditor/BlueprintEditor.h"
#include "Editor/BlueprintEditor/BlueprintEditorTab.h"

#include <algorithm>
#include <cstring>

#include "Asset/AssetUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Core/EngineLog.h"
#include "Core/Misc/Paths.h"
#include "Engine/Blueprint.h"
#include "Editor/HitoriEd/LevelUndoHistory.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/BoxSphereBounds.h"
#include "Collision/HitResult.h"
#include "UObject/UObjectHash.h"

#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"

namespace
{
	// 에디터가 받은 경로("Assets/Blueprints/BP_Door.bp" 또는 Assets 기준 키)를 실제 파일 경로로 바꾼다.
	std::filesystem::path ResolveFile(const FString& AssetPath)
	{
		const FString Resolved = FAssetUtils::ResolveAssetFilePath(AssetPath);
		return FPaths::FromUtf8(Resolved.empty() ? AssetPath : Resolved);
	}
}

FBlueprintEditor::FBlueprintEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager, const FString& InAssetPath)
	: FAssetEditor(InEditorEngine, InEditorManager, InAssetPath)
{
}

bool FBlueprintEditor::InitializeResources()
{
	const std::filesystem::path File = ResolveFile(GetAssetPath());

	// 엔진 시작 뒤에 만든 BP는 아직 등록되지 않았을 수 있으므로 여기서 읽는다.
	BlueprintClass = FindClass(FPaths::ToUtf8(File.stem()));
	if (!BlueprintClass || !FBlueprint::IsBlueprintClass(BlueprintClass))
	{
		FString Error;
		BlueprintClass = FBlueprint::LoadBlueprint(File, &Error);
		if (!BlueprintClass)
		{
			HTR_LOG(Warning, "Blueprint editor: cannot open {} ({})", GetAssetPath(), Error);
			return false;
		}
	}

	if (!FAssetEditor::InitializeResources())
		return false;

	if (FBlueprint::IsComponentBlueprint(BlueprintClass))
	{
		// 컴포넌트 BP: 빈 액터에 BP 컴포넌트를 붙인다. AddComponent가 BP 기본값을 적용해 준다.
		Template = PreviewScene.AddActor(AActor::StaticClass());
		ComponentTemplate = Template ? Template->AddComponent(BlueprintClass, FName(BlueprintClass->Name)) : nullptr;
		if (!ComponentTemplate)
		{
			HTR_LOG(Warning, "Blueprint editor: cannot create component {}", BlueprintClass->Name);
			return false;
		}
		SelectedComponent = ComponentTemplate;
	}
	else
	{
		// 미리보기 월드에 BP 클래스로 액터를 만든다. SpawnActor가 BP 기본값을 적용해 준다.
		Template = PreviewScene.AddActor(BlueprintClass);
		if (!Template)
		{
			HTR_LOG(Warning, "Blueprint editor: cannot spawn {}", BlueprintClass->Name);
			return false;
		}
	}

	// 그래프는 편집용으로 복사해 둔다. 저장할 때까지 실행 중인 게임에는 옛 그래프가 쓰인다.
	if (std::shared_ptr<const FBPGraphSet> Saved = FBlueprint::GetGraphs(BlueprintClass))
		Graphs = *Saved;
	else
		Graphs = FBPGraphSet();

	SerializeTemplate(SavedBytes);
	bDirty = false;
	return true;
}

TUniquePtr<IEditorTab> FBlueprintEditor::CreateTab()
{
	return MakeUnique<FBlueprintEditorTab>(*this);
}

void FBlueprintEditor::ShutdownResources()
{
	Gizmo.EndDrag();
	Gizmo.SetTarget(nullptr);
	Template = nullptr;
	ComponentTemplate = nullptr;
	SelectedComponent = nullptr;
	FAssetEditor::ShutdownResources();
}

bool FBlueprintEditor::SerializeTemplate(TArray<uint8>& OutBytes) const
{
	OutBytes.Empty();
	if (!Template)
		return false;

	FMemoryWriter Writer(OutBytes);
	Writer.SetIsPersistent(true);
	{
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Ar(Formatter);
		FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
		UObject* Defaults = ComponentTemplate ? static_cast<UObject*>(ComponentTemplate) : Template;
		Defaults->Serialize(Root.EnterRecord("Defaults"));
		Ar.Close();
	}
	if (Writer.IsError())
		return false;
	// 그래프도 바뀌었는지 비교하도록 뒤에 붙인다.
	const FString GraphJson = FBlueprintGraphSerializer::ToJson(Graphs);
	const int32 Offset = OutBytes.Num();
	OutBytes.SetNum(Offset + static_cast<int32>(GraphJson.size()));
	std::memcpy(OutBytes.GetData() + Offset, GraphJson.data(), GraphJson.size());
	return true;
}

bool FBlueprintEditor::Save()
{
	if (!Template || !BlueprintClass)
		return false;

	FString Error;
	const std::filesystem::path File = ResolveFile(GetAssetPath());
	UObject* Defaults = ComponentTemplate ? static_cast<UObject*>(ComponentTemplate) : Template;
	if (!FBlueprint::SaveBlueprint(File, BlueprintClass->Super, Defaults, Error, ComponentTemplate ? nullptr : &Graphs))
	{
		HTR_LOG(Error, "Blueprint: failed to save {} ({})", GetAssetPath(), Error);
		const std::wstring Message = FPaths::FromUtf8("Failed to save blueprint.\n" + Error).wstring();
		MessageBoxW(GetActiveWindow(), Message.c_str(), L"Save Blueprint", MB_OK | MB_ICONERROR);
		return false;
	}

	SerializeTemplate(SavedBytes);
	bDirty = false;
	// 저장하면 레벨에 놓인 이 BP 액터들도 새 기본값으로 갱신되므로 레벨 실행 취소 기록에 남긴다.
	FLevelUndoHistory::MarkDirty();
	HTR_LOG(Info, "Blueprint saved: {}", GetAssetPath());
	return true;
}

bool FBlueprintEditor::ConfirmClose()
{
	if (!bDirty)
		return true;

	const std::wstring Message = FPaths::FromUtf8("Save changes to " + BlueprintClass->Name + " before closing?").wstring();
	const int Result = MessageBoxW(GetActiveWindow(), Message.c_str(), L"Blueprint", MB_YESNOCANCEL | MB_ICONQUESTION);
	if (Result == IDCANCEL)
		return false;
	if (Result == IDYES)
		return Save();
	return true;   // 저장하지 않고 닫는다
}

void FBlueprintEditor::DeleteComponent(UActorComponent* Component)
{
	if (!Template || !Component || Component == Template->GetRootComponent() || Component == ComponentTemplate)
		return;
	if (Gizmo.GetTarget() == Component)
	{
		Gizmo.EndDrag();
		Gizmo.SetTarget(nullptr);
	}
	if (SelectedComponent == Component)
		SelectedComponent = nullptr;
	Template->DestroyComponent(Component);
	NoteEdited();
}

bool FBlueprintEditor::RenameComponent(UActorComponent* Component, const char* NewName)
{
	if (!Template || !Component || !NewName || NewName[0] == '\0')
		return false;
	const FName Name(NewName);
	if (Name == Component->GetFName())
		return false;
	if (Template->FindComponentByName(Name))
	{
		HTR_LOG(Warning, "A component named '{}' already exists in this blueprint", NewName);
		return false;
	}
	Component->SetName(Name);
	NoteEdited();
	return true;
}

void FBlueprintEditor::FocusTemplate()
{
	if (!ViewportClient || !Template)
		return;

	bool bHasBox = false;
	FVector Min, Max;
	for (UActorComponent* Component : Template->GetComponents())
	{
		UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
		if (!Primitive)
			continue;
		const FBox Box = Primitive->CalcBounds();
		const FVector Size = Box.Max - Box.Min;
		if (Size.X <= 1e-4f && Size.Y <= 1e-4f && Size.Z <= 1e-4f)
			continue;
		if (!bHasBox)
		{
			Min = Box.Min;
			Max = Box.Max;
			bHasBox = true;
			continue;
		}
		Min = FVector(std::min(Min.X, Box.Min.X), std::min(Min.Y, Box.Min.Y), std::min(Min.Z, Box.Min.Z));
		Max = FVector(std::max(Max.X, Box.Max.X), std::max(Max.Y, Box.Max.Y), std::max(Max.Z, Box.Max.Z));
	}

	FBoxSphereBounds Bounds;
	if (bHasBox)
	{
		Bounds.Origin = (Min + Max) * 0.5f;
		Bounds.BoxExtent = (Max - Min) * 0.5f;
		Bounds.SphereRadius = Bounds.BoxExtent.Size();
	}
	else
	{
		// 메시가 없는 BP(GameMode 등)는 원점 근처를 본다.
		Bounds.Origin = Template->GetActorLocation();
		Bounds.BoxExtent = FVector(0.5f, 0.5f, 0.5f);
		Bounds.SphereRadius = 1.0f;
	}
	ViewportClient->FocusOnBounds(Bounds);
}

void FBlueprintEditor::SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input,
	const FBlueprintViewportPointer& InPointer)
{
	ViewportWidth = Width;
	ViewportHeight = Height;
	bViewportVisible = Width > 0 && Height > 0;
	CameraInput = Input;
	Pointer = InPointer;
}

void FBlueprintEditor::SetGizmoMode(UE::Widget::EWidgetMode Mode)
{
	Gizmo.EndDrag();
	ModeTools.SetWidgetMode(Mode);
}

void FBlueprintEditor::SyncGizmoTarget()
{
	if (Gizmo.IsUsing())
		return;
	USceneComponent* Target = Cast<USceneComponent>(SelectedComponent);
	// 루트는 옮겨도 인스턴스에 남지 않으므로(스폰 위치로 바뀐다) 기즈모를 띄우지 않는다.
	if (Target && Template && Target == Template->GetRootComponent())
		Target = nullptr;
	if (Target && Template && Template->GetComponents().Find(Target) == INDEX_NONE)
		Target = nullptr;
	Gizmo.SetTarget(Target, false);
}

void FBlueprintEditor::UpdateGizmoAndPicking()
{
	SyncGizmoTarget();
	if (!ViewportClient || !Template || ViewportWidth == 0 || ViewportHeight == 0)
	{
		Gizmo.EndDrag();
		return;
	}

	// Esc: 끌던 것을 원래대로 돌린다.
	if (Pointer.bCancel && Gizmo.IsUsing())
	{
		if (USceneComponent* Target = Gizmo.GetTarget())
			Target->SetTransform(DragStartTransform);
		Gizmo.EndDrag();
		return;
	}

	const bool bWasUsing = Gizmo.IsUsing();
	// 카메라를 잡고 있거나 마우스가 뷰포트 밖이면(끄는 중이 아니라면) 아무것도 하지 않는다.
	if (CameraInput.bCaptured || (!Pointer.bHovered && !bWasUsing))
	{
		if (!Pointer.bMouseDown)
			Gizmo.EndDrag();
		return;
	}

	const int32 Width = static_cast<int32>(ViewportWidth);
	const int32 Height = static_cast<int32>(ViewportHeight);
	const FVector2 MousePosition(Pointer.MouseUV.X * Width, Pointer.MouseUV.Y * Height);
	const FRay MouseRay = ViewportClient->DeprojectScreenToWorld(MousePosition);

	if (Gizmo.GetTarget())
	{
		// 누른 프레임에만 끌기를 시작하고, 놓는 프레임의 마지막 위치도 반영한다.
		const bool bUpdateDrag = bWasUsing || (Pointer.bMousePressed && Pointer.bMouseDown);
		Gizmo.Update(MouseRay, MousePosition, ViewportClient->GetViewProjectionMatrix(), Width, Height,
			bUpdateDrag, ViewportClient->GetViewLocation(), !ViewportClient->IsPerspective());

		if (!bWasUsing && Gizmo.IsUsing())
			DragStartTransform = Gizmo.GetTarget()->GetTransform();
		if (bWasUsing && !Gizmo.IsUsing())
			NoteEdited();   // 끌기를 마쳤으면 저장 표시를 다시 계산한다
		if (bWasUsing || Gizmo.IsUsing() || Gizmo.GetHoveredAxis() >= 0)
			return;         // 기즈모를 다루는 중이면 클릭으로 고르지 않는다
	}

	// 기즈모 밖을 클릭하면 그 아래 컴포넌트를 고른다. 빈 곳이면 액터 자신(Self)을 고른다.
	if (!Pointer.bMousePressed)
		return;
	UWorld* World = ViewportClient->GetWorld();
	FHitResult Hit;
	if (World && World->LineTraceSingle(MouseRay, Hit) && Hit.HitComponent && Hit.HitComponent->GetOwner() == Template)
		SelectComponent(Hit.HitComponent);
	else
		SelectComponent(nullptr);
	SyncGizmoTarget();
}

void FBlueprintEditor::Tick(float DeltaTime)
{
	if (ViewportClient && ViewportClient->GetWorld() && bViewportVisible)
	{
		FViewportCameraState Camera{
			.Location = ViewportClient->GetViewLocation(),
			.Rotation = ViewportClient->GetViewRotation(),
			.OrthoWidth = ViewportClient->GetOrthoWidth(),
			.ViewWidth = static_cast<float>(ViewportWidth),
			.bPerspective = true
		};
		ApplyViewportCameraMovement(Camera, CameraInput, DeltaTime, 5.0f, 0.2f);
		ViewportClient->SetViewLocation(Camera.Location);
		ViewportClient->SetViewRotation(Camera.Rotation);
	}
	UpdateGizmoAndPicking();
	CameraInput = {};
	Pointer = {};

	// 편집이 끝났으면 저장된 상태와 비교해 * 표시를 정한다. 액터 하나라 가볍다.
	if (bCheckDirty && !ImGui::IsAnyItemActive())
	{
		bCheckDirty = false;
		TArray<uint8> Current;
		if (SerializeTemplate(Current))
			bDirty = !(Current == SavedBytes);
	}

	FAssetEditor::Tick(DeltaTime);
}

void FBlueprintEditor::OnRenderPreview()
{
	const bool bRender = bViewportVisible;
	bViewportVisible = false;
	if (!bRender || !ViewportClient || !ViewportClient->GetWorld() || !Viewport)
		return;

	Viewport->UpdateViewportRHI(false, ViewportWidth, ViewportHeight);
	if (!Viewport->GetViewTargets().IsValidFor(ViewportWidth, ViewportHeight))
		return;

	if (bInitialFocusPending)
	{
		FocusTemplate();
		bInitialFocusPending = false;
	}
	Viewport->Draw();
}
