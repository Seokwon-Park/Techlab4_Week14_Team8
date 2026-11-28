#include "EnginePCH.h"
#include "Terrain/HeightmapImporter.h"
#include "Core/Misc/Paths.h"

#include "stb_image.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <fstream>

namespace
{
	FString GetLowerExtension(const FString& Path)
	{
		std::filesystem::path FilePath = FPaths::FromUtf8(Path);
		std::filesystem::path ExtPath = FilePath.extension();
		FString Extension = FPaths::ToUtf8(ExtPath);

		for (char& Ch : Extension)
		{
			Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
		}
		return Extension;
	}
}

bool FHeightmapImporter::Import(const FString& Path, int32 ExpectedSizeX, int32 ExpectedSizeY,
	FHeightmapImportData& OutData, FString& OutError)
{
	OutData = FHeightmapImportData();

    if (Path.empty())
    {
        OutError = "HeightmapPath is empty";
        return false;
    }

    const FString Extension = GetLowerExtension(Path);
    const bool bRaw = (Extension == ".r16" || Extension == ".raw");
    const bool bPng = (Extension == ".png");
    if (!bRaw && !bPng)
    {
        OutError = std::format("Unsupported extension '{}' (use .r16, .raw or .png)", Extension);
        return false;
    }

    TArray<uint8> Bytes;
    if (!ReadFileBytes(Path, Bytes, OutError))
        return false;

    return bRaw ? ImportRaw16(Bytes, ExpectedSizeX, ExpectedSizeY, OutData, OutError)
        : ImportPng16(Bytes, OutData, OutError);
}

bool FHeightmapImporter::ValidateSize(int32 SizeX, int32 SizeY, int32 ChunkQuads, FString& OutError)
{
    if (ChunkQuads <= 0 || (ChunkQuads & (ChunkQuads - 1)) != 0)
    {
        OutError = std::format("ChunkQuads {} must be a power of two (e.g. 64)", ChunkQuads);
        return false;
    }
    if (SizeX <= 1 || SizeY <= 1)
    {
        OutError = std::format("Size {}x{} is too small", SizeX, SizeY);
        return false;
    }
    if ((SizeX - 1) % ChunkQuads != 0 || (SizeY - 1) % ChunkQuads != 0)
    {
        OutError = std::format("Size {}x{} must be N*ChunkQuads+1 (ChunkQuads={}, e.g. {} or {})",
            SizeX, SizeY, ChunkQuads, ChunkQuads * 8 + 1, ChunkQuads * 16 + 1);
        return false;
    }
    return true;
}

bool FHeightmapImporter::ReadFileBytes(const FString& Path, TArray<uint8>& OutBytes, FString& OutError)
{
    std::ifstream File(FPaths::FromUtf8(Path), std::ios::binary | std::ios::ate);
    if (!File)
    {
        OutError = std::format("Cannot open '{}'", Path);
        return false;
    }

    const std::streamsize Size = File.tellg();
    if (Size <= 0)
    {
        OutError = std::format("'{}' is empty", Path);
        return false;
    }

    OutBytes.SetNum(static_cast<int32>(Size));
    File.seekg(0, std::ios::beg);
    if (!File.read(reinterpret_cast<char*>(OutBytes.GetData()), Size))
    {
        OutError = std::format("Failed to read '{}'", Path);
        return false;
    }
    return true;
}

bool FHeightmapImporter::ImportRaw16(const TArray<uint8>& Bytes, int32 SizeX, int32 SizeY,
    FHeightmapImportData& OutData, FString& OutError)
{
    if (SizeX <= 1 || SizeY <= 1)
    {
        OutError = "RAW heightmap has no size header: set Coordinates.SizeX/SizeY first";
        return false;
    }

    const int64 ExpectedBytes = static_cast<int64>(SizeX) * SizeY * sizeof(uint16);
    if (static_cast<int64>(Bytes.Num()) != ExpectedBytes)
    {
        OutError = std::format("File is {} bytes but {}x{} needs {} bytes (SizeX*SizeY*2)",
            Bytes.Num(), SizeX, SizeY, ExpectedBytes);
        return false;
    }

    OutData.SizeX = SizeX;
    OutData.SizeY = SizeY;
    OutData.Heights.SetNum(SizeX * SizeY);
    // .r16은 리틀 엔디언이고 x86도 리틀 엔디언이라 그대로 복사한다
    std::memcpy(OutData.Heights.GetData(), Bytes.GetData(), static_cast<size_t>(ExpectedBytes));
    return true;
}

bool FHeightmapImporter::ImportPng16(const TArray<uint8>& Bytes, FHeightmapImportData& OutData, FString& OutError)
{
    const stbi_uc* Buffer = Bytes.GetData();
    const int Length = Bytes.Num();

    // 8bit PNG도 stbi_load_16으로 읽히지만 0~255를 *257로 늘려서 계단 지형이 되기 때문에 거절
    if (!stbi_is_16_bit_from_memory(Buffer, Length))
    {
        OutError = "PNG is not 16-bit. Export the heightmap as 16-bit grayscale PNG";
        return false;
    }

    int Width = 0, Height = 0, Channels = 0;
    stbi_us* Pixels = stbi_load_16_from_memory(Buffer, Length, &Width, &Height, &Channels, 1);
    if (!Pixels)
    {
        OutError = std::format("stb_image failed: {}", stbi_failure_reason());
        return false;
    }

    OutData.SizeX = Width;
    OutData.SizeY = Height;
    OutData.Heights.SetNum(Width * Height);
    std::memcpy(OutData.Heights.GetData(), Pixels, static_cast<size_t>(Width) * Height * sizeof(uint16));
    stbi_image_free(Pixels);

    if (Channels != 1)
        HTR_LOG(Warning, "[Heightmap] PNG has {} channels, converted to grayscale", Channels);
    return true;
}