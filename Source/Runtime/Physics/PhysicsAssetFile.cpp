#include "EnginePCH.h"
#include "Physics/PhysicsAssetFile.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>

#include "Physics/PhysicsAsset.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"

bool PhysicsAssetFile::Read(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError)
{
	OutError.clear();
	std::ifstream File(Path, std::ios::binary | std::ios::ate);
	const auto Size = static_cast<std::streamoff>(File.tellg());
	if (!File || Size <= 0 || Size > (std::numeric_limits<int32>::max)())
	{
		OutError = "PhysicsAsset: cannot open file or invalid size";
		return false;
	}

	TArray<uint8> Bytes;
	Bytes.SetNum(static_cast<int32>(Size));
	File.seekg(0);
	if (!File.read(reinterpret_cast<char*>(Bytes.GetData()), Size))
	{
		OutError = "PhysicsAsset: cannot read file";
		return false;
	}

	FMemoryReader Reader(Bytes, true);
	FJsonArchiveInputFormatter Formatter(Reader);
	if (Reader.IsError())
	{
		OutError = "PhysicsAsset: invalid JSON";
		return false;
	}
	FStructuredArchive Archive(Formatter);
	Asset.Serialize(Archive.Open().EnterRecord());
	if (Reader.IsError())
	{
		OutError = "PhysicsAsset: broken data";
		return false;
	}
	return true;
}

bool PhysicsAssetFile::Write(const std::filesystem::path& Path, UPhysicsAsset& Asset, FString& OutError)
{
	OutError.clear();
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	{
		// Archive가 소멸하면서 열린 Record/Array를 닫고 JSON을 마무리한다
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Archive(Formatter);
		Asset.Serialize(Archive.Open().EnterRecord());
	}
	if (Writer.IsError())
	{
		OutError = "PhysicsAsset: cannot serialize data";
		return false;
	}

	std::error_code Ec;
	if (Path.has_parent_path())
		std::filesystem::create_directories(Path.parent_path(), Ec);

	std::ofstream File(Path, std::ios::binary | std::ios::trunc);
	if (!File)
	{
		OutError = "PhysicsAsset: cannot open output file";
		return false;
	}
	File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	File.close();
	if (!File)
	{
		OutError = "PhysicsAsset: cannot write file";
		return false;
	}
	return true;
}

bool PhysicsAssetFile::IsPhysicsAssetPath(const FString& Path)
{
	FString Extension = std::filesystem::path(Path).extension().string();
	std::transform(Extension.begin(), Extension.end(), Extension.begin(),
		[](unsigned char C) { return static_cast<char>(std::tolower(C)); });
	return Extension == ".physicsasset";
}
