#include "EnginePCH.h"
#include "Mesh.h"
#include "Renderer.h"
#include "Asset/AssetManager.h"
#include "Engine/PrimitiveSceneProxy.h"
#include "Physics/BodySetup.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"
#include "UObject/UObjectGlobals.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

UStaticMesh::~UStaticMesh()
{
    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
        Proxy->OnMeshDestroyed();
	VertexBuffer = nullptr;
	IndexBuffer = nullptr;
}

UMaterial* UStaticMesh::GetMaterial(uint32 SlotIndex) const
{
	if (SlotIndex < Materials.size() && Materials[SlotIndex]) {
		return Materials[SlotIndex];
	}
	return UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
}

size_t UStaticMesh::FMaterialSlotsHash::operator()(const std::vector<UMaterial*>& Slots) const
{
    size_t Hash = 0;
    for (const UMaterial* Material : Slots)
        Hash ^= std::hash<const UMaterial*>{}(Material) + 0x9e3779b9u + (Hash << 6) + (Hash >> 2);
    return Hash;
}

void UStaticMesh::RebuildRenderData()
{
    RenderBounds = MeshData.AABB;
    for (const auto& LOD : AdditionalLODs)
    {
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            RenderBounds.Min[Axis] = std::min(RenderBounds.Min[Axis], LOD.Data.AABB.Min[Axis]);
            RenderBounds.Max[Axis] = std::max(RenderBounds.Max[Axis], LOD.Data.AABB.Max[Axis]);
        }
    }
    LocalLODSphere.Center = (RenderBounds.Min + RenderBounds.Max) * 0.5f;
    double RadiusSquared = 0.0;
    for (uint32 LOD = 0; LOD < GetLODCount(); ++LOD)
    {
        for (const FVertex& Vertex : GetMeshData(LOD).Vertices)
        {
            const double X = static_cast<double>(Vertex.Position.X) - LocalLODSphere.Center.X;
            const double Y = static_cast<double>(Vertex.Position.Y) - LocalLODSphere.Center.Y;
            const double Z = static_cast<double>(Vertex.Position.Z) - LocalLODSphere.Center.Z;
            RadiusSquared = std::max(RadiusSquared, X * X + Y * Y + Z * Z);
        }
    }
    // Include a small geometric margin and round outwards.
    LocalLODSphere.RadiusSquared = std::nextafter(
        static_cast<float>(RadiusSquared * (1.0 + 2e-5)), std::numeric_limits<float>::infinity());
    RenderStates.clear();
    for (FPrimitiveSceneProxy* Proxy : RenderProxies)
        Proxy->OnMeshRenderDataChanged();
}

TSharedPtr<const FMeshRenderState> UStaticMesh::GetRenderState(const std::vector<UMaterial*>& Materials)
{
    auto& WeakState = RenderStates[Materials];
    if (auto State = WeakState.lock()) return State;
    auto State = MakeShared<FMeshRenderState>();
    State->LODCount = static_cast<uint8>(std::min(GetLODCount(), 4u));
    for (uint32 I = 0; I < 3; ++I)
        State->LODThresholdSq[I] = ScreenThresholds[I] * ScreenThresholds[I];
    for (uint32 LOD = 0; LOD < State->LODCount; ++LOD)
    {
        auto& Range = State->LODs[LOD];
        Range.FirstSection = State->Sections.Num();
        for (const FStaticMeshSection& Section : GetMeshData(LOD).Sections)
        {
            UMaterial* Material = Section.MaterialSlotIndex < Materials.size()
                ? Materials[Section.MaterialSlotIndex] : GetMaterial(Section.MaterialSlotIndex);
            if (Material) State->Sections.Add({ Material, Section.StartIndex, Section.IndexCount });
        }
        Range.NumSections = State->Sections.Num() - Range.FirstSection;
    }
    WeakState = State;
    return State;
}

void UStaticMesh::RegisterProxy(FPrimitiveSceneProxy* Proxy)
{
    RenderProxies.Add(Proxy);
}

void UStaticMesh::UnregisterProxy(FPrimitiveSceneProxy* Proxy)
{
    const auto It = std::find(RenderProxies.begin(), RenderProxies.end(), Proxy);
    if (It != RenderProxies.end())
        RenderProxies.RemoveAtSwap(static_cast<uint32>(It - RenderProxies.begin()));
}

FString UStaticMesh::GetBodySetupFilePath() const
{
    const FString& AssetPath = GetPath();
    if (AssetPath.empty()) return {};
    const FString Resolved = FAssetUtils::ResolveAssetFilePath(AssetPath);
    const FString Base = Resolved.empty() ? AssetPath : Resolved;
    return Base + ".bodysetup";
}

bool UStaticMesh::SaveBodySetup(FString* OutError)
{
    UBodySetup* Setup = GetBodySetup();
    if (!Setup)
    {
        if (OutError) *OutError = "No BodySetup to save";
        return false;
    }
    const FString FilePath = GetBodySetupFilePath();
    if (FilePath.empty())
    {
        if (OutError) *OutError = "Mesh has no asset path";
        return false;
    }

    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes);
    {
        FJsonArchiveOutputFormatter Formatter(Writer);
        FStructuredArchive Archive(Formatter);
        Setup->Serialize(Archive.Open().EnterRecord());
    }
    if (Writer.IsError())
    {
        if (OutError) *OutError = "Failed to serialize BodySetup";
        return false;
    }

    std::ofstream File(FPaths::FromUtf8(FilePath), std::ios::binary | std::ios::trunc);
    if (!File)
    {
        if (OutError) *OutError = "Failed to open .bodysetup for writing";
        return false;
    }
    File.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
    File.close();
    return static_cast<bool>(File);
}

bool UStaticMesh::LoadBodySetupIfExists()
{
    const FString FilePath = GetBodySetupFilePath();
    if (FilePath.empty()) return false;
    std::error_code Ec;
    const std::filesystem::path PathObj = FPaths::FromUtf8(FilePath);
    if (!std::filesystem::is_regular_file(PathObj, Ec)) return false;

    std::ifstream File(PathObj, std::ios::binary | std::ios::ate);
    const auto Size = static_cast<std::streamoff>(File.tellg());
    if (!File || Size <= 0) return false;

    TArray<uint8> Bytes;
    Bytes.SetNum(static_cast<int32>(Size));
    File.seekg(0);
    if (!File.read(reinterpret_cast<char*>(Bytes.GetData()), Size)) return false;

    FMemoryReader Reader(Bytes, true);
    FJsonArchiveInputFormatter Formatter(Reader);
    if (Reader.IsError()) return false;

    UBodySetup* Loaded = NewObject<UBodySetup>(this);
    FStructuredArchive Archive(Formatter);
    Loaded->Serialize(Archive.Open().EnterRecord());
    if (Reader.IsError()) return false;

    BodySetup = Loaded;
    return true;
}

UBodySetup* UStaticMesh::GetBodySetup()
{
    if (BodySetup) return BodySetup;
    if (LoadBodySetupIfExists()) return BodySetup;

    const FBox& Box = MeshData.AABB;
    if (Box.Min.X > Box.Max.X || Box.Min.Y > Box.Max.Y || Box.Min.Z > Box.Max.Z) return nullptr;

    constexpr float MinThickness = 0.01f;
    const FVector Size = Box.Max - Box.Min;

    FKBoxElem Elem;
    Elem.Center = (Box.Min + Box.Max) * 0.5f;
    Elem.X = std::max(Size.X, MinThickness);
    Elem.Y = std::max(Size.Y, MinThickness);
    Elem.Z = std::max(Size.Z, MinThickness);

    BodySetup = NewObject<UBodySetup>(this);
    BodySetup->AggGeom.BoxElems.Add(Elem);
    return BodySetup;
}
