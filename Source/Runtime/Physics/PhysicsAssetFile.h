#pragma once

#include "Core/EngineString.h"
#include <filesystem>

class UPhysicsAsset;

// Particles/ParticleSystemFile과 같은 방식: UPhysicsAsset::Serialize를 JSON 파일로 읽고 쓴다
namespace PhysicsAssetFile
{
	bool Read(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError);
	bool Write(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError);
	bool IsPhysicsAssetPath(const FString& Path);
}
