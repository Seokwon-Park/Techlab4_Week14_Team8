#include "EnginePCH.h"
#include "Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Level.h"
#include "Camera/CameraActor.h"
#include "Audio/AudioDevice.h"

#include "Serialization/JsonArchive.h"

UEngine* GEngine = nullptr;

bool UEngine::Init()
{
	// 모든 엔진 모드가 공통으로 쓰는 것만 여기서 준비한다. 월드와 뷰포트는 서브클래스의 Init이 만든다.
	InitializeAudioDevice();          // 실패해도 엔진은 계속 (UE도 장치가 없으면 Null 디바이스로 돈다)
	return true;
}

void UEngine::InitializeAudioDevice()
{
	MainAudioDevice = new FAudioDevice();
	if (!MainAudioDevice->Init())
	{
		HTR_LOG(Warning, "[Audio] No audio device. Sound disabled.");
		delete MainAudioDevice;
		MainAudioDevice = nullptr;
	}
}

void UEngine::ShutdownAudioDevice()
{
	if (!MainAudioDevice) return;
	MainAudioDevice->Teardown();      // 오디오 스레드 종료. 이후 USoundWave를 지워도 안전
	delete MainAudioDevice;
	MainAudioDevice = nullptr;
}

FWorldContext& UEngine::CreateNewWorldContext(EWorldType WorldType)
{
	FWorldContext* NewWorldContext = new FWorldContext();
	WorldContextlist.Add(NewWorldContext);
	NewWorldContext->WorldType = WorldType;
	NewWorldContext->ContextHandle = FName();

	return *NewWorldContext;
}

FWorldContext* UEngine::GetWorldContextFromWorld(const UWorld* InWorld)
{
	for (FWorldContext& WorldContext : WorldContextlist)
	{
		if (WorldContext.World() == InWorld)
		{
			return &WorldContext;
		}
	}
	return nullptr;
}

FWorldContext* UEngine::GetWorldContextFromType(EWorldType WorldType)
{
	for (FWorldContext& Context : WorldContextlist)
		if (Context.WorldType == WorldType) return &Context;
	return nullptr;
}

FWorldContext* UEngine::GetWorldContextFromPIEInstance(int32 PIEInstance)
{
	for (FWorldContext& Context : WorldContextlist)
		if (Context.WorldType == EWorldType::PIE && Context.PIEInstance == PIEInstance) return &Context;
	return nullptr;
}

UWorld* UEngine::GetEditorWorld() const
{
	for (const FWorldContext& Context : WorldContextlist)
		if (Context.WorldType == EWorldType::Editor) return Context.World();
	return nullptr;
}

UWorld* UEngine::CreatePIEWorldByDuplication(FWorldContext& Context, UWorld* SourceWorld)
{
	UWorld* World = UWorld::GetDuplicatedWorldForPIE(SourceWorld);
	if (World) Context.SetCurrentWorld(World);
	return World;
}

void UEngine::DestroyWorldContext(UWorld* InWorld)
{
	// 유효한 월드의 소속 컨텍스트 조회
	if (!InWorld){ return; }

	if (FWorldContext* Context = GetWorldContextFromWorld(InWorld))
	{
		DestroyWorldContext(*Context);
	}
}

void UEngine::DestroyWorldContext(FWorldContext& Context)
{
	for (int32 idx = 0; idx < WorldContextlist.Num(); ++idx)
	{
		if (&WorldContextlist[idx] == &Context)
		{
			// 컨텍스트 제거 후에도 필요한 월드와 타입 보관
			UWorld* World = Context.World();
			const EWorldType WorldType = Context.WorldType;

			if (Context.GameViewport)
			{
				Context.GameViewport->LostFocus();
				Context.GameViewport->Reset();
			}

			if (UGameInstance* Instance = Context.OwningGameInstance)
			{
				Instance->Shutdown();
				delete Instance;
			}

			Context.SetCurrentWorld(nullptr);
			WorldContextlist.RemoveAt(idx);

			// 실제 월드를 가진 플레이 컨텍스트의 오디오와 월드 정리
			const bool bOwnsWorld = WorldType == EWorldType::PIE ||	WorldType == EWorldType::Game || WorldType == EWorldType::EditorPreview;
			if (World && bOwnsWorld)
			{
				if (MainAudioDevice)
				{
					MainAudioDevice->Flush(World);
				}
				DestroyWorld(World);
			}

			return;
		}
	}
}

void UEngine::DestroyWorld(UWorld* InWorld)
{
	if (!InWorld) return;
	ACameraActor* Camera = InWorld->GetMainCamera();
	const bool bOwnsCamera = Camera && !Camera->GetLevel();
	InWorld->SetMainCamera(nullptr);
	InWorld->ClearWorld();
	for (ULevel* Level : InWorld->GetLevels()) delete Level;
	if (bOwnsCamera) delete Camera;
	delete InWorld;
}

void UEngine::SetClientTravel(UWorld* InWorld, const FString& MapPath)
{
	if (FWorldContext* Context = GetWorldContextFromWorld(InWorld))
		Context->TravelURL = MapPath;
}

void UEngine::TickWorldTravel(FWorldContext& Context)
{
	if (Context.TravelURL.empty()) return;
	const FString MapPath = Context.TravelURL;
	Context.TravelURL.clear();
	LoadMap(Context, MapPath);
}

bool UEngine::LoadMap(FWorldContext& Context, const FString& MapPath)
{
	// 새 월드를 먼저 읽는다. 파일이 없거나 깨졌으면 지금 월드를 그대로 둔다.
	UWorld* NewWorld = UWorld::CreateWorld(Context.WorldType, true);
	if (!NewWorld) return false;
	if (!FJsonArchive::LoadWorld(NewWorld, MapPath))
	{
		HTR_LOG(Error, "Failed to load map: {}", MapPath);
		DestroyWorld(NewWorld);
		return false;
	}

	PreLoadMap(Context);
	if (Context.GameViewport)
	{
		Context.GameViewport->ClearViewportWidgets();
		Context.GameViewport->SetWorld(nullptr);
	}
	
	DestroyWorld(Context.World());

	Context.SetCurrentWorld(NewWorld);
	if (Context.GameViewport)
		Context.GameViewport->SetWorld(NewWorld);
	NewWorld->BeginPlay();
	PostLoadMap(Context);

	HTR_LOG(Info, "Map loaded: {}", MapPath);
	return true;
}

void FWorldContext::SetCurrentWorld(UWorld* World)
{
	ThisCurrentWorld = World;
}
