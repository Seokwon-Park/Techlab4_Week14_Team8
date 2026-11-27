#include "EnginePCH.h"
#include "Engine/WorldSubsystem.h"

// 1일차 링크용 빈 스텁. TODO(B): 구현

void RegisterWorldSubsystemClass(const char* ClassName, FWorldSubsystemFactory Factory)
{
}

void FWorldSubsystemCollection::Initialize(UWorld& World)
{
}

void FWorldSubsystemCollection::LoadLevel(const FLevelLoadContext& C)
{
}

void FWorldSubsystemCollection::BeginPlay(UWorld& World)
{
}

void FWorldSubsystemCollection::UnloadLevel()
{
}

FWorldSubsystemStats FWorldSubsystemCollection::GatherStats() const
{
	return {};
}
