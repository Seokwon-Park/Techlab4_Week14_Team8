#include "EnginePCH.h"
#include "AI/Navigation/NavigationSubsystem.h"

#include "AI/Navigation/NavMeshDebugGeometry.h"

#include "Recast.h"
#include "DetourNavMeshQuery.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourAlloc.h"

namespace
{
	rcConfig MakeRecastConfig(const float* Vertices, int VertexCount)
	{
		const FNavAgentProperties& Agent = GetDefaultNavAgent();
		const FNavMeshResolution& Resolution = GetDefaultNavResolution();

		rcConfig Config{};
		Config.cs = Resolution.CellSize * CmToWorld;
		Config.ch = Resolution.CellHeight * CmToWorld;
		Config.walkableHeight = FMath::Max(3, static_cast<int>(std::ceil(Agent.AgentHeight * CmToWorld / Config.ch)));
		Config.walkableRadius = static_cast<int>(std::ceil(Agent.AgentRadius * CmToWorld / Config.cs));
		Config.walkableClimb = static_cast<int>(std::floor(Agent.AgentStepHeight * CmToWorld / Config.ch));
		Config.walkableSlopeAngle = Agent.AgentMaxSlope;

		Config.maxEdgeLen = static_cast<int>(12.0f / Config.cs);
		Config.maxSimplificationError = 1.3f;
		Config.minRegionArea = 8 * 8;
		Config.mergeRegionArea = 20 * 20;
		Config.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
		Config.detailSampleDist = Config.cs * 6.0f;
		Config.detailSampleMaxError = Config.ch;

		rcCalcBounds(Vertices, VertexCount, Config.bmin, Config.bmax);
		Config.bmin[1] -= Config.ch;
		Config.bmax[1] += (Config.walkableHeight + 1) * Config.ch;
		rcCalcGridSize(Config.bmin, Config.bmax, Config.cs, &Config.width, &Config.height);
		return Config;
	}
}


void FNavigationSubsystem::OnLevelLoad(const FLevelLoadContext& Context)
{
}

void FNavigationSubsystem::OnLevelUnload()
{
}

void FNavigationSubsystem::GetStats(FWorldSubsystemStats& Out) const
{
}

void FNavigationSubsystem::BuildNavigation()
{
	// 임시
	const FVector WorldVertices[] = {
		FVector(0.0f, 0.0f, 0.0f),
		FVector(12.0f, 0.0f, 0.0f),
		FVector(12.0f, 12.0f, 0.0f),
		FVector(0.0f, 12.0f, 0.0f),
	};

	float Vertices[12]{};
	for (int Index = 0; Index < 4; ++Index)
	{
		const FVector Position = Unreal2RecastPoint(WorldVertices[Index]);
		Vertices[Index * 3] = Position.X;
		Vertices[Index * 3 + 1] = Position.Y;
		Vertices[Index * 3 + 2] = Position.Z;
	}

	const int Indices[] = { 0, 2, 1, 0, 3, 2 };
	const rcConfig Config = MakeRecastConfig(Vertices, 4);

	rcContext Context;
	unsigned char TriangleAreas[2]{};

	const auto Fail = [](const char* Stage)
	{
		HTR_LOG(Warning, "NavMesh build failed at {}", Stage);
	};

	std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)> Heightfield(rcAllocHeightfield(), rcFreeHeightField);
	if (!Heightfield || 
		!rcCreateHeightfield(&Context, *Heightfield, Config.width, Config.height, 
			Config.bmin, Config.bmax, Config.cs, Config.ch))
	{
		Fail("Heightfield");
		return;
	}

	rcMarkWalkableTriangles(&Context, Config.walkableSlopeAngle, Vertices, 4, Indices, 2, TriangleAreas);
	if (!rcRasterizeTriangles(&Context, Vertices, 4, Indices, TriangleAreas, 2, *Heightfield, Config.walkableClimb))
	{
		Fail("Rasterization");
		return;
	}

	rcFilterLowHangingWalkableObstacles(&Context, Config.walkableClimb, *Heightfield);
	rcFilterLedgeSpans(&Context, Config.walkableHeight, Config.walkableClimb, *Heightfield);
	rcFilterWalkableLowHeightSpans(&Context, Config.walkableHeight, *Heightfield);

	std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)> Compact(rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
	if (!Compact || 
		!rcBuildCompactHeightfield(&Context, Config.walkableHeight, Config.walkableClimb, *Heightfield, *Compact))
	{
		Fail("compact Heightfield");
		return;
	}
	Heightfield.reset();

	if (!rcErodeWalkableArea(&Context, Config.walkableRadius, *Compact) ||
		!rcBuildDistanceField(&Context, *Compact) ||
		!rcBuildRegions(&Context, *Compact, Config.borderSize, Config.minRegionArea, Config.mergeRegionArea))
	{
		Fail("Erosion or Watershed Regions");
		return;
	}

	std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)> Contours(rcAllocContourSet(), rcFreeContourSet);
	if (!Contours || !rcBuildContours(&Context, *Compact, Config.maxSimplificationError, Config.maxEdgeLen, *Contours))
	{
		Fail("Contours");
		return;
	}

	std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)> Polygons(rcAllocPolyMesh(), rcFreePolyMesh);
	if (!Polygons || !rcBuildPolyMesh(&Context, *Contours, Config.maxVertsPerPoly, *Polygons) || Polygons->npolys == 0)
	{
		Fail("PolygonMesh");
		return;
	}

	std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)> Detail(rcAllocPolyMeshDetail(), rcFreePolyMeshDetail);
	if (!Detail || 
		!rcBuildPolyMeshDetail(&Context, *Polygons, *Compact, Config.detailSampleDist, Config.detailSampleMaxError, *Detail))
	{
		Fail("DetailMesh");
		return;
	}

	constexpr unsigned char GroundArea = 0;
	constexpr unsigned short WalkFlag = 1;

	for (int Index = 0; Index < Polygons->npolys; ++Index)
	{
		Polygons->areas[Index] = GroundArea;
		Polygons->flags[Index] = WalkFlag;
	}

	dtNavMeshCreateParams Params{};
	Params.verts = Polygons->verts;
	Params.vertCount = Polygons->nverts;
	Params.polys = Polygons->polys;
	Params.polyAreas = Polygons->areas;
	Params.polyFlags = Polygons->flags;
	Params.polyCount = Polygons->npolys;
	Params.nvp = Polygons->nvp;
	Params.detailMeshes = Detail->meshes;
	Params.detailVerts = Detail->verts;
	Params.detailVertsCount = Detail->nverts;
	Params.detailTris = Detail->tris;
	Params.detailTriCount = Detail->ntris;

	const FNavAgentProperties& Agent = GetDefaultNavAgent();
	Params.walkableHeight = Agent.AgentHeight * CmToWorld;
	Params.walkableRadius = Agent.AgentRadius * CmToWorld;
	Params.walkableClimb = Agent.AgentStepHeight * CmToWorld;
	Params.cs = Config.cs;
	Params.ch = Config.ch;
	Params.buildBvTree = true;
	rcVcopy(Params.bmin, Polygons->bmin);
	rcVcopy(Params.bmax, Polygons->bmax);

	std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)> Mesh(dtAllocNavMesh(), dtFreeNavMesh);
	if (!Mesh)
	{
		Fail("Detour mesh allocation");
		return;
	}

	unsigned char* Data = nullptr;
	int DataSize = 0;
	if (!dtCreateNavMeshData(&Params, &Data, &DataSize))
	{
		Fail("Detour mesh data");
		return;
	}

	if (dtStatusFailed(Mesh->init(Data, DataSize, DT_TILE_FREE_DATA)))
	{
		dtFree(Data);
		Fail("Detour mesh initialization");
		return;
	}

	std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)> Query(dtAllocNavMeshQuery(), dtFreeNavMeshQuery);
	if (!Query || dtStatusFailed(Query->init(Mesh.get(), 2048)))
	{
		Fail("Detour query initialization");
		return;
	}

	HTR_LOG(Info, "Built test NavMesh: {} polygons, {} bytes", Polygons->npolys, DataSize);
}

FNavPathResult FNavigationSubsystem::FindPath(const FVector& Start, const FVector& End) const
{
	return {}; // Status = NoNavMesh
}

const FNavMeshDebugGeometry* FNavigationSubsystem::GetDebugGeometry() const
{
	return nullptr;
}

REGISTER_WORLD_SUBSYSTEM(FNavigationSubsystem);
