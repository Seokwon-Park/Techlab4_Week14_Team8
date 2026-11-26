#include "EnginePCH.h"
#include "GameFramework/GameModeBase.h"

#include "Core/EngineLog.h"
#include "UObject/UObjectHash.h"

AGameModeBase::AGameModeBase()
	: DefaultPawnClass("ADefaultPawn")
	, HUDClass("AHUD")
	, PlayerControllerClass("AMyPlayerController")
	, GameStateClass("AGameStateBase")
	, PlayerStateClass("APlayerState")
	, SpectatorClass("ASpectatorPawn")
{
}

UClass* AGameModeBase::ResolveClass(const FString& ClassName, UClass* BaseClass, UClass* Fallback)
{
	if (ClassName.empty())
		return Fallback;
	UClass* Class = FindClass(ClassName);
	if (Class && (!BaseClass || Class->IsChildOf(BaseClass)))
		return Class;
	HTR_LOG(Warning, "GameMode: class '{}' not found, using {}", ClassName, Fallback ? Fallback->Name : FString("None"));
	return Fallback;
}
