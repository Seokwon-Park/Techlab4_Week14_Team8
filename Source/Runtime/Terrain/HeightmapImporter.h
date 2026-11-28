// UE FLandscapeImportHelper 자리. 16bit 하이트맵 파일(.r16/.raw, 16bit PNG)을 uint16 배열로 읽는다.
// 파일을 읽어 검증만 한다. ATerrain이나 GPU 자원은 모른다.

#pragma once
#include "Core/Types.h"
#include "Core/EngineString.h"
#include "Containers/Array.h"

struct FHeightmapImportData
{
	TArray<uint16> Heights; // Row-Major, Heights[Y * SizeX + X]
	int32 SizeX = 0;
	int32 SizeY = 0;
};

class FHeightmapImporter
{
public:
	// .r16/.raw : 파일에 크기 정보가 없어서 ExpectedSizeX/Y를 쓴다 (Coordinates 값)
	// .png      : 파일에 적힌 크기를 쓴다 (Expected 값은 무시)
	// Path는 "Content/..." 같은 UTF-8 상대 경로 또는 절대 경로

	static bool Import(const FString& Path, int32 ExpectedSizeX, int32 ExpectedSizeY, FHeightmapImportData& OutData, FString& OutError);
	//Size = 청크 수 * ChunkQuads + 1, ChunkQuads는 2^n
	static bool ValidateSize(int32 SizeX, int32 SizeY, int32 ChunkQuads, FString& OutError);

private:
	static bool ReadFileBytes(const FString& Path, TArray<uint8>& OutBytes, FString& OutError);
	static bool ImportRaw16(const TArray<uint8>& Bytes, int32 SizeX, int32 SizeY,
		FHeightmapImportData& OutData, FString& OutError);
	static bool ImportPng16(const TArray<uint8>& Bytes, FHeightmapImportData& OutData, FString& OutError);
};