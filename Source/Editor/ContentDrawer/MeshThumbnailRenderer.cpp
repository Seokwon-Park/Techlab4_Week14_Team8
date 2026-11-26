#include "EnginePCH.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/ContentDrawer/MeshThumbnailRenderer.h"

#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/Misc/Paths.h"
#include "Editor/AssetEditor/FPreviewScene.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Editor/Settings/SettingsPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Engine/Viewport.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/BoxSphereBounds.h"
#include "Render/Buffer.h"
#include "Render/Material.h"
#include "Render/MaterialFile.h"
#include "Render/Mesh.h"
#include "Render/PipelineState.h"
#include "Render/RenderCommand.h"
#include "Render/Renderer.h"
#include "Render/RenderResourceManager.h"
#include "Render/SceneRenderer.h"
#include "Render/SceneView.h"
#include "Render/SkeletalMesh.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{
	struct alignas(16) FThumbnailCheckerConstants
	{
		float ColorA[4];
		float ColorB[4];
		float CellSize;
		float Padding[3];
	};
	static_assert(sizeof(FThumbnailCheckerConstants) == 48);

	std::string ToLowerAscii(std::string Text)
	{
		std::transform(Text.begin(), Text.end(), Text.begin(),
			[](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });
		return Text;
	}

	bool IsObjExtension(const fs::path& FilePath)
	{
		return ToLowerAscii(FilePath.extension().generic_string()) == ".obj";
	}

	bool AreBoundsFinite(const FBox& Box)
	{
		return std::isfinite(Box.Min.X) && std::isfinite(Box.Min.Y) && std::isfinite(Box.Min.Z)
			&& std::isfinite(Box.Max.X) && std::isfinite(Box.Max.Y) && std::isfinite(Box.Max.Z)
			&& Box.Min.X <= Box.Max.X && Box.Min.Y <= Box.Max.Y && Box.Min.Z <= Box.Max.Z;
	}

	URenderAsset* LoadMeshAssetForThumbnail(const fs::path& FilePath, const FString& Utf8Path)
	{
		if (URenderAsset* Loaded = UAssetManager::FindLoadedAsset(Utf8Path))
			return Loaded;
		if (URenderAsset* Asset = UAssetManager::GetAssetByPath<URenderAsset>(Utf8Path))
			return Asset;

		const FString Resolved = FAssetUtils::ResolveAssetFilePath(Utf8Path);
		const FString LoadPath = Resolved.empty() ? Utf8Path : Resolved;

		if (IsObjExtension(FilePath))
			return UAssetManager::LoadObjStaticMesh(LoadPath);
		if (FFbxMeshBake::IsBakedPath(LoadPath))
		{
			FString Error;
			return UAssetManager::LoadFbxMesh(LoadPath, Error);
		}
		return nullptr;
	}

	// 썸네일 전용 뷰포트 클라이언트: 씬 뒤에 테마 체커보드 배경을 깐다.
	class FThumbnailViewportClient final : public FEditorViewportClient
	{
	public:
		explicit FThumbnailViewportClient(FPreviewScene& InPreviewScene)
			: FEditorViewportClient(InPreviewScene)
		{
			EngineShowFlags.Grid = false;
			EngineShowFlags.Fog = false;
			EngineShowFlags.DepthOfField = false;
			EngineShowFlags.LensFlares = false;
			EngineShowFlags.EyeAdaptation = false;
			EngineShowFlags.Vignette = false;
			EngineShowFlags.CameraOverlay = false;
			EngineShowFlags.AntiAliasing = false;
			EngineShowFlags.Bounds = false;
			EngineShowFlags.ObjectUUID = false;
			EngineShowFlags.CascadeBounds = false;
			EngineShowFlags.Collision = false;
			EngineShowFlags.PhysicsBodies = false;
			EngineShowFlags.Constraints = false;

			CheckerShader = FRenderResourceManager::GetShaderProgram(HITORI_ENGINE_SHADER("ThumbnailCheckerShader.hlsl"));
			CheckerCB = RenderCommand::CreateConstantBuffer(sizeof(FThumbnailCheckerConstants));
		}

		void Draw(const FSceneView* View, FScenePassCallbacks& Callbacks) override
		{
			if (!CheckerShader || !CheckerCB) return;
			const uint32 W = GetWidth();
			Callbacks.Overlays.EditorGrid = [this, W]()
			{
				const ImVec4 Bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
				const ImVec4 Frame = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
				FThumbnailCheckerConstants Data{};
				Data.ColorA[0] = Bg.x; Data.ColorA[1] = Bg.y; Data.ColorA[2] = Bg.z; Data.ColorA[3] = 1.0f;
				Data.ColorB[0] = Frame.x; Data.ColorB[1] = Frame.y; Data.ColorB[2] = Frame.z; Data.ColorB[3] = 1.0f;
				Data.CellSize = static_cast<float>(std::max(W, 1u)) / static_cast<float>(FMeshThumbnailRenderer::CheckerCellsAcross);
				RenderCommand::UpdateBufferData(CheckerCB.get(), &Data, sizeof(Data));

				FPipelineState Pipeline;
				Pipeline.Shader = CheckerShader;
				Pipeline.Topology = EPrimitiveTopology::TriangleList;
				Pipeline.RasterizerState = ERasterizerState::SolidNone;
				Pipeline.BlendState = EBlendState::Opaque;
				Pipeline.DepthStencilState = EDepthStencilState::PrepassReadOnly;
				RenderCommand::BindPipelineState(Pipeline);
				RenderCommand::BindConstantBuffer(0, CheckerCB.get(), EShaderBindFlagBits::Pixel);
				RenderCommand::BindVertexBuffer(nullptr);
				RenderCommand::Draw(3);
			};
		}

	private:
		FShaderProgram* CheckerShader = nullptr;
		TUniquePtr<FConstantBuffer> CheckerCB;
	};
}

FMeshThumbnailRenderer::FMeshThumbnailRenderer() = default;
FMeshThumbnailRenderer::~FMeshThumbnailRenderer() { Release(); }

bool FMeshThumbnailRenderer::IsSupportedFile(const fs::path& FilePath, const FString& Utf8Path)
{
	if (IsObjExtension(FilePath)) return true;
	if (FFbxMeshBake::IsBakedPath(Utf8Path)) return true;
	if (MaterialFile::IsMaterialPath(Utf8Path)) return true;
	return false;
}

ImTextureID FMeshThumbnailRenderer::GetOrRequestThumbnail(const fs::path& FilePath, const FString& Utf8Path, fs::file_time_type LastWriteTime)
{
	if (!IsSupportedFile(FilePath, Utf8Path))
		return ImTextureID_Invalid;

	const unsigned char CurrentTheme = static_cast<unsigned char>(EditorStyle::GetTheme());
	const auto It = Cache.find(Utf8Path);
	if (It != Cache.end())
	{
		if (It->second.LastWriteTime == LastWriteTime && It->second.Theme == CurrentTheme)
		{
			if (It->second.Texture && It->second.Texture->GetSRV())
				return reinterpret_cast<ImTextureID>(It->second.Texture->GetSRV());
			if (It->second.bFailed)
				return ImTextureID_Invalid;
		}
	}

	const bool bAlreadyQueued = std::any_of(PendingQueue.begin(), PendingQueue.end(),
		[&Utf8Path](const FPendingRequest& Req) { return Req.Utf8Path == Utf8Path; });
	if (!bAlreadyQueued)
	{
		PendingQueue.push_back({ FilePath, Utf8Path, LastWriteTime });
	}
	return (It != Cache.end() && It->second.Texture && It->second.Texture->GetSRV())
		? reinterpret_cast<ImTextureID>(It->second.Texture->GetSRV())
		: ImTextureID_Invalid;
}

void FMeshThumbnailRenderer::RenderPending(UEditorEngine& Engine, FRenderer* Renderer)
{
	if (PendingQueue.empty() || !Renderer || bPreviewInitFailed)
		return;

	const FPendingRequest Request = std::move(PendingQueue.front());
	PendingQueue.erase(PendingQueue.begin());

	const unsigned char CurrentTheme = static_cast<unsigned char>(EditorStyle::GetTheme());
	const auto It = Cache.find(Request.Utf8Path);
	if (It != Cache.end() && It->second.LastWriteTime == Request.LastWriteTime && It->second.Theme == CurrentTheme &&
		((It->second.Texture && It->second.Texture->GetSRV()) || It->second.bFailed))
	{
		return;
	}

	RenderMeshToCache(Request, Engine, Renderer);
}

bool FMeshThumbnailRenderer::EnsurePreviewScene(UEditorEngine& Engine)
{
	if (PreviewScene && ViewportClient && ThumbnailViewport && StaticMeshComp && SkeletalMeshComp)
		return true;
	if (bPreviewInitFailed)
		return false;

	PreviewScene = MakeUnique<FPreviewScene>();
	if (!PreviewScene->Initialize(&Engine))
	{
		PreviewScene.reset();
		bPreviewInitFailed = true;
		return false;
	}
	PreviewScene->Setup();

	AActor* PreviewActor = PreviewScene->AddActor(AActor::StaticClass());
	if (!PreviewActor)
	{
		PreviewScene->Release();
		PreviewScene.reset();
		bPreviewInitFailed = true;
		return false;
	}

	StaticMeshComp = Cast<UStaticMeshComponent>(
		PreviewActor->AddComponent(UStaticMeshComponent::StaticClass(), "ThumbnailStaticMesh"));
	SkeletalMeshComp = Cast<USkeletalMeshComponent>(
		PreviewActor->AddComponent(USkeletalMeshComponent::StaticClass(), "ThumbnailSkeletalMesh"));
	if (!StaticMeshComp || !SkeletalMeshComp)
	{
		StaticMeshComp = nullptr;
		SkeletalMeshComp = nullptr;
		PreviewScene->Release();
		PreviewScene.reset();
		bPreviewInitFailed = true;
		return false;
	}

	ViewportClient = MakeUnique<FThumbnailViewportClient>(*PreviewScene);
	ViewportClient->SetViewportType(ELevelViewportType::Perspective);

	ThumbnailViewport = MakeUnique<FViewport>(ViewportClient.get());
	ThumbnailViewport->UpdateViewportRHI(false, ThumbnailResolution, ThumbnailResolution);
	return true;
}

void FMeshThumbnailRenderer::RenderMeshToCache(const FPendingRequest& Request, UEditorEngine& Engine, FRenderer* Renderer)
{
	FThumbnailEntry& Entry = Cache[Request.Utf8Path];
	Entry.LastWriteTime = Request.LastWriteTime;
	const unsigned char CurrentTheme = static_cast<unsigned char>(EditorStyle::GetTheme());
	if (Entry.Theme != CurrentTheme)
		Entry.Texture.reset();
	Entry.Theme = CurrentTheme;
	Entry.bFailed = true;

	if (!EnsurePreviewScene(Engine))
		return;

	FBox BoundsBox{};
	bool bValidMesh = false;
	StaticMeshComp->ClearOverrideMaterials();
	StaticMeshComp->SetStaticMesh(nullptr);
	SkeletalMeshComp->SetSkeletalMesh(nullptr);

	if (MaterialFile::IsMaterialPath(Request.Utf8Path))
	{
		UMaterial* Material = UAssetManager::Get().LoadMaterial(Request.Utf8Path);
		UStaticMesh* Sphere = UAssetManager::GetAssetByPath<UStaticMesh>("Sphere");
		if (Material && Sphere)
		{
			StaticMeshComp->SetStaticMesh(Sphere);
			StaticMeshComp->SetMaterial(0, Material);
			BoundsBox = Sphere->GetRenderBounds();
			bValidMesh = AreBoundsFinite(BoundsBox);
		}
	}
	else
	{
		URenderAsset* Asset = LoadMeshAssetForThumbnail(Request.FilePath, Request.Utf8Path);
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
		{
			const auto& MeshData = StaticMesh->GetMeshData();
			if (!MeshData.Vertices.IsEmpty() && !MeshData.Indices.IsEmpty())
			{
				StaticMeshComp->SetStaticMesh(StaticMesh);
				BoundsBox = StaticMesh->GetRenderBounds();
				bValidMesh = AreBoundsFinite(BoundsBox);
			}
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset))
		{
			SkeletalMeshComp->SetSkeletalMesh(SkeletalMesh);
			BoundsBox = SkeletalMeshComp->CalcLocalBounds();
			bValidMesh = AreBoundsFinite(BoundsBox);
		}
	}

	if (!bValidMesh)
	{
		StaticMeshComp->ClearOverrideMaterials();
		StaticMeshComp->SetStaticMesh(nullptr);
		SkeletalMeshComp->SetSkeletalMesh(nullptr);
		return;
	}

	PreviewScene->Tick(0.0f);
	if (UWorld* PreviewWorld = PreviewScene->GetWorld())
		PreviewWorld->GetScene().UpdateAllTransforms();

	const FVector Extent = (BoundsBox.Max - BoundsBox.Min) * 0.5f;
	const FBoxSphereBounds Bounds{
		(BoundsBox.Min + BoundsBox.Max) * 0.5f,
		Extent,
		Extent.Size()
	};

	ThumbnailViewport->UpdateViewportRHI(false, ThumbnailResolution, ThumbnailResolution);
	if (!ThumbnailViewport->GetViewTargets().IsValidFor(ThumbnailResolution, ThumbnailResolution))
	{
		StaticMeshComp->ClearOverrideMaterials();
		StaticMeshComp->SetStaticMesh(nullptr);
		SkeletalMeshComp->SetSkeletalMesh(nullptr);
		return;
	}

	ViewportClient->SetViewRotation(FRotator(22.0f, 200.0f, 0.0f));
	ViewportClient->FocusOnBounds(Bounds);
	ThumbnailViewport->Draw();

	FTexture2D* RenderedColor = ThumbnailViewport->GetViewTargets().SceneColor.get();
	if (RenderedColor && RenderedColor->GetRawPtr())
	{
		FTextureDesc Desc{};
		Desc.Width = ThumbnailResolution;
		Desc.Height = ThumbnailResolution;
		Desc.Format = ETextureFormat::R8G8B8A8_UNorm;
		Desc.BindFlags = ETextureBindFlags::ShaderResource | ETextureBindFlags::RenderTarget;

		TUniquePtr<FTexture2D> CachedTexture = RenderCommand::CreateTexture2D(Desc);
		if (CachedTexture && CachedTexture->GetRawPtr() && CachedTexture->GetSRV())
		{
			RenderCommand::CopyTexture(CachedTexture.get(), RenderedColor);
			Entry.Texture = std::move(CachedTexture);
			Entry.bFailed = false;
		}
	}

	StaticMeshComp->ClearOverrideMaterials();
	StaticMeshComp->SetStaticMesh(nullptr);
	SkeletalMeshComp->SetSkeletalMesh(nullptr);
}

void FMeshThumbnailRenderer::ClearPendingQueue()
{
	PendingQueue.clear();
}

void FMeshThumbnailRenderer::InvalidatePath(const FString& Utf8Path)
{
	Cache.erase(Utf8Path);
	PendingQueue.erase(
		std::remove_if(PendingQueue.begin(), PendingQueue.end(),
			[&Utf8Path](const FPendingRequest& Req) { return Req.Utf8Path == Utf8Path; }),
		PendingQueue.end());
}

void FMeshThumbnailRenderer::Release()
{
	PendingQueue.clear();
	Cache.clear();
	StaticMeshComp = nullptr;
	SkeletalMeshComp = nullptr;
	ThumbnailViewport.reset();
	ViewportClient.reset();
	if (PreviewScene)
	{
		PreviewScene->Release();
		PreviewScene.reset();
	}
	bPreviewInitFailed = false;
}
