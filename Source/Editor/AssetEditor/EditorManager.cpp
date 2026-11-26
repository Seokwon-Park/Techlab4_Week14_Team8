#include "EnginePCH.h"
#include "Editor/BlueprintEditor/BlueprintEditor.h"
#include "Engine/Blueprint.h"
#include "Editor/AssetEditor/EditorManager.h"
#include "Editor/ProjectBrowser/HomeTab.h"
#include "Editor/StaticMeshEditor/StaticMeshEditor.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Asset/FbxImporter/FbxMeshBake.h"
#include "Render/Mesh.h"
#include "Render/SkeletalMesh.h"
#include "Editor/AnimSequenceEditor/AnimSequenceEditor.h"
#include "Editor/BlendSpaceEditor/BlendSpaceEditor.h"
#include "Editor/InputEditor/InputAssetEditor.h"
#include "Editor/UIEditor/UIEditor.h"
#include "Animation/AnimSequenceFile.h"
#include "Animation/BlendSpaceFile.h"
#include "Animation/AnimBlueprintFile.h"
#include "Core/Misc/Paths.h"
#include "Editor/AnimBlueprintEditor/AnimBlueprintEditor.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "Editor/ParticleEditor/ParticleSystemEditor.h"
#include "Particles/ParticleSystemFile.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"

#include <filesystem>

FEditorManager::FEditorManager(UEditorEngine& InEditorEngine)
	: EditorEngine(InEditorEngine)
	, LevelEditorTab(MakeUnique<FLevelEditorTab>())
{
	// UE처럼 탭 줄 맨 왼쪽에 집 아이콘 Home 탭(프로젝트 브라우저 화면)을 둔다. 시작할 때는 레벨 탭을 앞에 둔다.
	HomeTab = MakeUnique<FHomeTab>(TabContainer, LevelEditorTab.get());
	TabContainer.AddTab(HomeTab.get());
	TabContainer.AddTab(LevelEditorTab.get());
	TabContainer.ActivateTab(LevelEditorTab.get());
}

void FEditorManager::OpenEditor(const FString& AssetPath)
{
	if (bShuttingDown)
	{
		return;
	}

	const FString Path = FAssetUtils::GetAssetKey(AssetPath);
	if (Path.empty())
	{
		HTR_LOG(Warning, "Cannot open an editor for an empty or invalid asset path.");
		return;
	}

	PendingOpens.Add(Path);
}

FAssetEditor* FEditorManager::FindEditor(const FString& Path) const
{
	for (const auto& Editor : Editors)
	{
		if (Editor->GetAssetPath() == Path)
		{
			return Editor.get();
		}
	}

	return nullptr;
}

void FEditorManager::CreateEditor(const FString& AssetPath)
{
	// 이미 열려 있는 에디터가 있으면 활성화
	if (FAssetEditor* Existing = FindEditor(AssetPath))
	{
		Existing->Activate();
		return;
	}

    if (AnimBlueprintFile::IsAnimBlueprintPath(AssetPath))
    {
        const FString FilePath = FAssetUtils::ResolveAssetFilePath(AssetPath);
        FAnimBlueprintData Data;
        FString Error;

        if (!AnimBlueprintFile::Read(FPaths::FromUtf8(FilePath.empty() ? AssetPath : FilePath), Data, Error))
        {
            HTR_LOG(Warning, "Failed to open AnimBlueprint: {} ({})", AssetPath, Error);
            const std::wstring Message = FPaths::FromUtf8(Error).wstring();
            MessageBoxW(GetActiveWindow(), Message.c_str(), L"Open Animation Blueprint", MB_OK | MB_ICONERROR);
            return;
        }

        TUniquePtr<FAssetEditor> Editor = MakeUnique<FAnimBlueprintEditor>(EditorEngine, *this, AssetPath, std::move(Data));

        if (!Editor->Initialize())
        {
            Editor->Shutdown();
            MessageBoxW(GetActiveWindow(), L"Cannot initialize the preview mesh. See Output Log.", L"Open Animation Blueprint", MB_OK | MB_ICONERROR);
            return;
        }

        Editors.Add(std::move(Editor));
        return;
    }

	// 애니메이션 시퀀스는 렌더 에셋이 아니므로 먼저 분기한다
	if (AnimSequenceFile::IsAnimSequencePath(AssetPath))
	{
		UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(AssetPath);
		if (!Sequence)
		{
			return;     // 경고는 LoadAnimSequence가 남긴다
		}

		TUniquePtr<FAssetEditor> Editor = MakeUnique<FAnimSequenceEditor>(EditorEngine, *this, AssetPath, Sequence);
		if (!Editor->Initialize())
		{
			Editor->Shutdown();
			HTR_LOG(Warning, "Asset editor initialization failed: {}", AssetPath.c_str());
			return;
		}
		Editors.Add(std::move(Editor));
		HTR_LOG(Info, "Opened asset editor: {}", AssetPath.c_str());
		return;
	}

	if (BlendSpaceFile::IsBlendSpacePath(AssetPath))
	{
		UBlendSpace1D* BlendSpace = UAssetManager::LoadBlendSpace(AssetPath);
		if (!BlendSpace)
		{
			return;
		}

		TUniquePtr<FAssetEditor> Editor = MakeUnique<FBlendSpaceEditor>(EditorEngine, *this, AssetPath, BlendSpace);
		if (!Editor->Initialize())
		{
			Editor->Shutdown();
			HTR_LOG(Warning, "BlendSpace editor initialization failed: {}", AssetPath);
			return;
		}
		Editors.Add(std::move(Editor));
		return;
	}

	// 입력 에셋 (*.inputaction / *.inputcontext). 미리보기 없이 속성만 편집한다
	if (FInputAssetEditor::CanOpen(AssetPath))
	{
		TUniquePtr<FAssetEditor> Editor = MakeUnique<FInputAssetEditor>(EditorEngine, *this, AssetPath);
		if (!Editor->Initialize())
		{
			Editor->Shutdown();
			HTR_LOG(Warning, "Input asset editor initialization failed: {}", AssetPath);
			return;
		}
		Editors.Add(std::move(Editor));
		return;
	}

	if (ParticleSystemFile::IsParticleSystemPath(AssetPath))
	{
		UParticleSystem* System = UAssetManager::LoadParticleSystem(AssetPath);
		if (!System)
		{
			return;     // 경고는 LoadParticleSystem이 남긴다
		}

		TUniquePtr<FAssetEditor> Editor = MakeUnique<FParticleSystemEditor>(EditorEngine, *this, AssetPath, System);
		if (!Editor->Initialize())
		{
			Editor->Shutdown();
			HTR_LOG(Warning, "Particle editor initialization failed: {}", AssetPath);
			return;
		}
		Editors.Add(std::move(Editor));
		return;
	}

	if (PhysicsAssetFile::IsPhysicsAssetPath(AssetPath))
	{
		OpenPhysicsAssetFile(AssetPath);
		return;
	}

	// 데이터 Blueprint
	if (FBlueprint::IsBlueprintPath(AssetPath))
	{
		TUniquePtr<FAssetEditor> Editor = MakeUnique<FBlueprintEditor>(EditorEngine, *this, AssetPath);
		if (!Editor->Initialize())
		{
			Editor->Shutdown();
			HTR_LOG(Warning, "Blueprint editor initialization failed: {}", AssetPath);
			return;
		}
		Editors.Add(std::move(Editor));
		return;
	}

	// 위젯 에셋은 에디터가 파일을 직접 읽는다
	if (FUIEditor::IsWidgetPath(AssetPath))
	{
		TUniquePtr<FAssetEditor> Editor = MakeUnique<FUIEditor>(EditorEngine, *this, AssetPath);
		if (!Editor->Initialize())
		{
			Editor->Shutdown();
			HTR_LOG(Warning, "Widget editor initialization failed: {}", AssetPath);
			return;
		}
		Editors.Add(std::move(Editor));
		HTR_LOG(Info, "Opened asset editor: {}", AssetPath.c_str());
		return;
	}

	// 애셋을 불러온다
	URenderAsset* Asset = UAssetManager::GetAssetByPath<URenderAsset>(AssetPath);
	
	// AssetPath가 제각각인 경우가 있으므로, 실패해도 여러가지 Key로 재시도한다.
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(AssetPath);
	if (!Asset && !FilePath.empty())
	{
		// 프로젝트 상대 경로로 시도
		const FString ProjectPath = std::filesystem::path(FilePath)
			.lexically_relative(std::filesystem::current_path()).generic_string();
		Asset = UAssetManager::GetAssetByPath<URenderAsset>(ProjectPath);
		
		if (!Asset)
		{
			// 절대 경로로 시도
			Asset = UAssetManager::GetAssetByPath<URenderAsset>(FilePath);
		}
	}
	
	// obj 파일은 AssetManager에 등록되지 않으므로 직접 로드
	if (!Asset && std::filesystem::path(AssetPath).extension() == ".obj")
	{
		Asset = UAssetManager::LoadObjStaticMesh(FilePath);
	}

	// 베이크된 FBX는 파일에 기록된 실제 메시 타입으로 로드합니다.
	if (!Asset && FFbxMeshBake::IsBakedPath(FilePath))
	{
		FString Error;
		Asset = UAssetManager::LoadFbxMesh(FilePath, Error);
		if (!Asset)
		{
			HTR_LOG(Warning, "Failed to load mesh preview: {} ({})", AssetPath, Error);
			return;
		}
	}

	if (!Asset)
	{
		HTR_LOG(Warning, "Asset is not loaded or could not be loaded: {}", AssetPath.c_str());
		return;
	}


	// Editor 불러오기
	TUniquePtr<FAssetEditor> Editor = nullptr;
	if (Asset->IsA<UStaticMesh>())
	{
		UStaticMesh* MeshAsset = Cast<UStaticMesh>(Asset);
		Editor = MakeUnique<FStaticMeshEditor>(EditorEngine, *this, AssetPath, *MeshAsset);
	}

	if (Asset->IsA<USkeletalMesh>())
	{
		USkeletalMesh* MeshAsset = Cast<USkeletalMesh>(Asset);
		Editor = MakeUnique<FSkeletalMeshEditor>(EditorEngine, *this, AssetPath, *MeshAsset);
	}

	// 지원하지 않는 타입
	if (!Editor)
	{
		HTR_LOG(Warning, "Unsupported asset editor type: {}", AssetPath.c_str());
		return;
	}

	// 초기화 실패시 로깅 후 return
	if (!Editor->Initialize())
	{
		Editor->Shutdown();
		HTR_LOG(Warning, "Asset editor initialization failed: {}", AssetPath.c_str());
		return;
	}

	Editors.Add(std::move(Editor));
	HTR_LOG(Info, "Opened asset editor: {}", AssetPath.c_str());
}

void FEditorManager::Tick(float DeltaTime)
{
	if (bShuttingDown)
	{
		return;
	}

	LevelEditorTab->Tick(DeltaTime);

	for (auto& Editor : Editors)
	{
		if (!Editor->IsCloseRequested())
		{
			Editor->Tick(DeltaTime);
		}
	}
}

void FEditorManager::RenderPreview()
{
	for (auto& Editor : Editors)
	{
		if (!Editor->IsCloseRequested())
		{
			Editor->OnRenderPreview();
		}
	}
}

void FEditorManager::LateTick()
{
	// ImGui의 분리 창까지 렌더링한 뒤 호출한다. 이번 프레임 텍스처 참조도 끝난 시점이다.
	for (int32 Index = Editors.Num() - 1; Index >= 0; --Index)
	{
		if (Editors[Index]->IsCloseRequested())
		{
			HTR_LOG(Info, "Closed asset editor: {}", Editors[Index]->GetAssetPath().c_str());
			Editors[Index]->Shutdown();
			Editors.RemoveAt(Index);
		}
	}

	TArray<FString> Requests = std::move(PendingOpens);
	PendingOpens = {};

	for (const FString& Path : Requests)
	{
		CreateEditor(Path);
	}

	TArray<FString> PreviewRequests = std::move(PendingAnimBlueprintPreviews);
	PendingAnimBlueprintPreviews = {};

	for (const FString& Path : PreviewRequests)
	{
		CreateAnimBlueprintPreview(Path);
	}

	TArray<FPendingPhysicsAssetOpen> PhysicsRequests = std::move(PendingPhysicsAssetOpens);
	PendingPhysicsAssetOpens = {};

	for (const FPendingPhysicsAssetOpen& Request : PhysicsRequests)
	{
		CreatePhysicsAssetEditor(Request);
	}
}

void FEditorManager::Shutdown()
{
	if (bShuttingDown)
	{
		return;
	}

	bShuttingDown = true;
	
	PendingOpens.Empty();
	PendingAnimBlueprintPreviews.Empty();
	PendingPhysicsAssetOpens.Empty();

	for (auto& Editor : Editors)
	{
		Editor->Shutdown();
	}
	Editors.Empty();

	TabContainer.RemoveTab(LevelEditorTab.get());
	LevelEditorTab.reset();
	if (HomeTab)
	{
		TabContainer.RemoveTab(HomeTab.get());
		HomeTab.reset();
	}
}

// FHomeTab의 완전한 타입이 필요해 cpp에서 정의한다.
FEditorManager::~FEditorManager() = default;

//void FEditorManager::DrawEditorUI()
//{
//	TabContainer.OnRender();
//}

void FEditorManager::DrawEditorUI()
{
    TabContainer.OnRender();
}

void FEditorManager::OpenAnimBlueprintPreview(const FString& PreviewMeshPath)
{
	if (bShuttingDown)
	{
		return;
	}

	const FString Path = FAssetUtils::GetAssetKey(PreviewMeshPath);

	if (Path.empty())
	{
		return;
	}

	PendingAnimBlueprintPreviews.Add(Path);
}

void FEditorManager::CreateAnimBlueprintPreview(const FString& PreviewMeshPath)
{
	const FString DocumentKey = PreviewMeshPath + ".animblueprint-preview";

	if (FAssetEditor* Existing = FindEditor(DocumentKey))
	{
		Existing->Activate();
		return;
	}

	TUniquePtr<FAssetEditor> Editor = MakeUnique<FAnimBlueprintEditor>(EditorEngine, *this, PreviewMeshPath);

	if (!Editor->Initialize())
	{
		Editor->Shutdown();
		HTR_LOG(Warning, "AnimBlueprint preview initialization failed: {}", PreviewMeshPath);
		return;
	}

	Editors.Add(std::move(Editor));
}

void FEditorManager::OpenPhysicsAssetEditor(const FString& MeshPath, USkeletalMesh* Mesh)
{
	if (bShuttingDown || !Mesh || MeshPath.empty())
	{
		return;
	}

	// UI를 그리는 도중에 부르므로 생성은 LateTick으로 미룬다
	PendingPhysicsAssetOpens.Add({ MeshPath, Mesh });
}

void FEditorManager::OpenPhysicsAssetFile(const FString& AssetPath)
{
	const FString ResolvedPath = FAssetUtils::ResolveAssetFilePath(AssetPath);
	const FString FilePath = ResolvedPath.empty() ? AssetPath : ResolvedPath;

	UPhysicsAsset* Loaded = NewObject<UPhysicsAsset>();
	FString Error;
	if (!PhysicsAssetFile::Read(FilePath, *Loaded, Error))
	{
		HTR_LOG(Warning, "Failed to load physics asset: {} ({})", AssetPath, Error);
		return;
	}
	if (Loaded->PreviewMeshPath.empty())
	{
		HTR_LOG(Warning, "PhysicsAsset has no preview mesh. Open it from the SkeletalMesh editor and save once: {}", AssetPath);
		return;
	}

	const FString MeshPath = FAssetUtils::GetAssetKey(Loaded->PreviewMeshPath);
	const FString MeshFile = FAssetUtils::ResolveAssetFilePath(Loaded->PreviewMeshPath);
	USkeletalMesh* Mesh = Cast<USkeletalMesh>(UAssetManager::LoadFbxMesh(MeshFile.empty() ? Loaded->PreviewMeshPath : MeshFile, Error));
	if (!Mesh)
	{
		HTR_LOG(Warning, "PhysicsAsset preview mesh failed to load: {} ({})", Loaded->PreviewMeshPath, Error);
		return;
	}

	// 메시에 연결된 에셋이 아직 없으면 이 파일을 연결한다.
	// 이미 다른 파일이 연결돼 있으면(같은 메시의 변형 에셋) 연결은 그대로 두고 이 파일만 따로 편집한다
	if (!Mesh->GetPhysicsAsset())
	{
		Mesh->PhysicsAsset = Loaded;
		Mesh->PhysicsAssetPath = FilePath;
	}
	const bool bMeshAsset = Mesh->PhysicsAsset == Loaded
		|| [&] { std::error_code Ec; return std::filesystem::equivalent(Mesh->PhysicsAssetPath, FilePath, Ec); }();
	if (bMeshAsset)
		CreatePhysicsAssetEditor({ MeshPath, Mesh });
	else
		CreatePhysicsAssetEditor({ MeshPath, Mesh, Loaded, FilePath });
}

void FEditorManager::CreatePhysicsAssetEditor(const FPendingPhysicsAssetOpen& Request)
{
	if (FAssetEditor* Existing = FindEditor(Request.Asset ? Request.AssetPath : FPhysicsAssetEditor::MakeDocumentKey(Request.MeshPath)))
	{
		Existing->Activate();
		return;
	}

	TUniquePtr<FAssetEditor> Editor = MakeUnique<FPhysicsAssetEditor>(EditorEngine, *this, Request.MeshPath, *Request.Mesh, Request.Asset, Request.AssetPath);
	if (!Editor->Initialize())
	{
		Editor->Shutdown();
		HTR_LOG(Warning, "Physics asset editor initialization failed: {}", Request.MeshPath);
		return;
	}

	Editors.Add(std::move(Editor));
	HTR_LOG(Info, "Opened physics asset editor: {}", Request.MeshPath);
}
