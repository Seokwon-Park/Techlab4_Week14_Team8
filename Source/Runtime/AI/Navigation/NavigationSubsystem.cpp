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
	ClearNavigation();
}

void FNavigationSubsystem::GetStats(FWorldSubsystemStats& Out) const
{
}

void FNavigationSubsystem::BuildNavigation()
{
	ClearNavigation();

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

	for (int Index = 0; Index < Polygons->nverts; ++Index)
	{
		const unsigned short* Vertex = &Polygons->verts[Index * 3];
		const float Position[] = {
			Polygons->bmin[0] + Vertex[0] * Polygons->cs,
			Polygons->bmin[1] + Vertex[1] * Polygons->ch,
			Polygons->bmin[2] + Vertex[2] * Polygons->cs,
		};
		DebugGeometry.Vertices.Add(Recast2UnrealPoint(Position));
	}

	for (int PolyIndex = 0; PolyIndex < Polygons->npolys; ++PolyIndex)
	{
		const unsigned short* Polygon = &Polygons->polys[PolyIndex * Polygons->nvp * 2];
		const unsigned short* Neighbors = Polygon + Polygons->nvp;
		int VertexCount = 0;
		while (VertexCount < Polygons->nvp && Polygon[VertexCount] != RC_MESH_NULL_IDX)
		{
			++VertexCount;
		}

		for (int Index = 1; Index + 1 < VertexCount; ++Index)
		{
			DebugGeometry.TriangleIndices.Add(Polygon[0]);
			DebugGeometry.TriangleIndices.Add(Polygon[Index + 1]);
			DebugGeometry.TriangleIndices.Add(Polygon[Index]);
		}

		for (int EdgeIndex = 0; EdgeIndex < VertexCount; ++EdgeIndex)
		{
			const unsigned short Neighbor = Neighbors[EdgeIndex];
			const bool bBoundary = Neighbor == RC_MESH_NULL_IDX || (Neighbor & 0x8000) != 0;
			if (!bBoundary && Neighbor < PolyIndex)
			{
				continue; // 공유 경계는 한쪽 폴리곤에서만 추가
			}

			TArray<FVector>& Edges = bBoundary ? DebugGeometry.BoundaryEdges : DebugGeometry.PolyEdges;
			Edges.Add(DebugGeometry.Vertices[Polygon[EdgeIndex]]);
			Edges.Add(DebugGeometry.Vertices[Polygon[(EdgeIndex + 1) % VertexCount]]);
		}
	}

	NavMesh.reset(Mesh.release());
	NavQuery.reset(Query.release());

	HTR_LOG(Info, "Built test NavMesh: {} polygons, {} bytes", Polygons->npolys, DataSize);
}

FNavPathResult FNavigationSubsystem::FindPath(const FVector& Start, const FVector& End) const
{
	FNavPathResult Result;
	if (!NavQuery)
	{
		return Result; // NoNavMesh
	}

	Result.Status = ENavPathStatus::QueryFailed;

	const FVector RecastStart = Unreal2RecastPoint(Start);
	const FVector RecastEnd = Unreal2RecastPoint(End);
	const float StartPosition[] = { RecastStart.X, RecastStart.Y, RecastStart.Z };
	const float EndPosition[] = { RecastEnd.X, RecastEnd.Y, RecastEnd.Z };
	const float HalfExtents[] = { 0.50f, 0.30f, 0.50f };

	dtQueryFilter Filter;
	Filter.setIncludeFlags(1); // BuildNavigation()의 WalkFlag와 동일한 값

	dtPolyRef StartRef = 0;
	dtPolyRef EndRef = 0;
	float NearestStart[3]{};
	float NearestEnd[3]{};

	const dtStatus StartStatus = NavQuery->findNearestPoly(StartPosition, HalfExtents, &Filter, &StartRef, NearestStart);
	if (dtStatusFailed(StartStatus))
	{
		return Result;
	}
	if (StartRef == 0)
	{
		Result.Status = ENavPathStatus::StartNotOnNavMesh;
		return Result;
	}

	const dtStatus EndStatus = NavQuery->findNearestPoly(EndPosition, HalfExtents, &Filter, &EndRef, NearestEnd);
	if (dtStatusFailed(EndStatus))
	{
		return Result;
	}
	if (EndRef == 0)
	{
		Result.Status = ENavPathStatus::GoalNotOnNavMesh;
		return Result;
	}

	constexpr int MaxPathPolygons = 256;
	dtPolyRef Corridor[MaxPathPolygons]{};
	int CorridorCount = 0;
	const dtStatus PathStatus = NavQuery->findPath(StartRef, EndRef, NearestStart, NearestEnd, &Filter, Corridor, &CorridorCount, MaxPathPolygons);

	if (dtStatusFailed(PathStatus) || dtStatusDetail(PathStatus, DT_OUT_OF_NODES) || dtStatusDetail(PathStatus, DT_BUFFER_TOO_SMALL) || CorridorCount == 0)
	{
		return Result;
	}

	const dtPolyRef LastRef = Corridor[CorridorCount - 1];
	const bool bPartial = (LastRef != EndRef) || dtStatusDetail(PathStatus, DT_PARTIAL_RESULT);
	float PathEnd[] = { NearestEnd[0], NearestEnd[1], NearestEnd[2] };
	if (bPartial && dtStatusFailed(NavQuery->closestPointOnPoly(LastRef, NearestEnd, PathEnd, nullptr)))
	{
		return Result;
	}

	constexpr int MaxStraightPoints = 256;
	float StraightPoints[MaxStraightPoints * 3]{};
	int StraightCount = 0;
	const dtStatus StraightStatus 
		= NavQuery->findStraightPath(NearestStart, PathEnd, Corridor, CorridorCount, StraightPoints, nullptr, nullptr, &StraightCount, MaxStraightPoints);

	if (dtStatusFailed(StraightStatus) || dtStatusDetail(StraightStatus, DT_BUFFER_TOO_SMALL) || StraightCount == 0)
	{
		return Result;
	}

	for (int Index = 0; Index < StraightCount; ++Index)
	{
		Result.Points.Add(Recast2UnrealPoint(&StraightPoints[Index * 3]));
	}

	Result.Status = bPartial ? ENavPathStatus::Partial : ENavPathStatus::Success;
	return Result;
}

const FNavMeshDebugGeometry* FNavigationSubsystem::GetDebugGeometry() const
{
	return NavMesh ? &DebugGeometry : nullptr;
}

void FNavigationSubsystem::FreeNavMesh(dtNavMesh* Mesh)
{
	dtFreeNavMesh(Mesh);
}

void FNavigationSubsystem::FreeNavQuery(dtNavMeshQuery* Query)
{
	dtFreeNavMeshQuery(Query);
}

void FNavigationSubsystem::ClearNavigation()
{
	NavQuery.reset();
	NavMesh.reset();

	const uint32 NextRevision = DebugGeometry.Revision + 1;
	DebugGeometry = {};
	DebugGeometry.Revision = NextRevision;
}

REGISTER_WORLD_SUBSYSTEM(FNavigationSubsystem);
