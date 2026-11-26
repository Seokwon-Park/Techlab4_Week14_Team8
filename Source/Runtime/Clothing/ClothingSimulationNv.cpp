#include "EnginePCH.h"
#include "Clothing/ClothingSimulationNv.h"
#include "Clothing/ClothCore.h"
#include "Clothing/ClothingAsset.h"

#include <NvCloth/Cloth.h>
#include <NvCloth/Fabric.h>
#include <NvCloth/Factory.h>
#include <NvCloth/Range.h>
#include <NvCloth/Solver.h>
#include <NvClothExt/ClothFabricCooker.h>
#include <foundation/PxVec3.h>
#include <foundation/PxVec4.h>
#include <vector>

FClothingSimulationNv::~FClothingSimulationNv()
{
	Shutdown();
}

bool FClothingSimulationNv::Initialize(const UClothingAssetCommon& Asset)
{
	Shutdown();

	if (!Asset.IsValid())
		return false;

	nv::cloth::Factory* Factory = FClothCore::Get().GetFactory();
	if (!Factory)
		return false;

	const FClothPhysicalMeshData& MeshData = Asset.GetPhysicalMeshData();
	const int32 NumVerts = MeshData.Vertices.Num();
	if (NumVerts == 0 || MeshData.Indices.Num() < 3)
		return false;

	std::vector<physx::PxVec4> Particles(static_cast<size_t>(NumVerts));
	CachedInvMasses.Reset();
	CachedInvMasses.Reserve(NumVerts);
	for (int32 i = 0; i < NumVerts; ++i)
	{
		const FVector& P = MeshData.Vertices[i];
		const float InvMass = (i < MeshData.InverseMasses.Num()) ? MeshData.InverseMasses[i] : 0.0f;
		Particles[static_cast<size_t>(i)] = physx::PxVec4(P.X, P.Y, P.Z, InvMass);
		CachedInvMasses.Add(InvMass);
	}

	nv::cloth::ClothMeshDesc MeshDesc;
	MeshDesc.setToDefault();
	MeshDesc.points.data = Particles.data();
	MeshDesc.points.stride = sizeof(physx::PxVec4);
	MeshDesc.points.count = static_cast<uint32>(NumVerts);
	MeshDesc.invMasses.data = &Particles[0].w;
	MeshDesc.invMasses.stride = sizeof(physx::PxVec4);
	MeshDesc.invMasses.count = static_cast<uint32>(NumVerts);
	MeshDesc.triangles.data = MeshData.Indices.GetData();
	MeshDesc.triangles.stride = sizeof(uint32) * 3;
	MeshDesc.triangles.count = static_cast<uint32>(MeshData.Indices.Num() / 3);

	const FClothConfigData& Cfg = Asset.GetConfig();
	const physx::PxVec3 Gravity(0.0f, 0.0f, -9.81f * Cfg.GravityScale);
	nv::cloth::Vector<int32_t>::Type PhaseTypeInfo;
	Fabric = NvClothCookFabricFromMesh(Factory, MeshDesc, Gravity, &PhaseTypeInfo, false);
	if (!Fabric)
		return false;

	nv::cloth::Range<const physx::PxVec4> ParticleRange(Particles.data(), Particles.data() + Particles.size());
	Cloth = Factory->createCloth(ParticleRange, *Fabric);
	if (!Cloth)
	{
		Shutdown();
		return false;
	}

	Cloth->setGravity(Gravity);
	Cloth->setDamping(physx::PxVec3(Cfg.Damping, Cfg.Damping, Cfg.Damping));
	Cloth->setDragCoefficient(Cfg.Drag);
	Cloth->setFriction(Cfg.Friction);
	Cloth->setWindVelocity(physx::PxVec3(Cfg.WindVelocity.X, Cfg.WindVelocity.Y, Cfg.WindVelocity.Z));

	Solver = Factory->createSolver();
	if (!Solver)
	{
		Shutdown();
		return false;
	}
	Solver->addCloth(Cloth);

	CachedIndices = MeshData.Indices;
	SimulationData.Positions = MeshData.Vertices;
	SimulationData.Normals.SetNum(NumVerts);
	RecomputeNormals();
	return true;
}

void FClothingSimulationNv::Shutdown()
{
	if (Solver && Cloth)
		Solver->removeCloth(Cloth);
	if (Solver)
	{
		delete Solver;
		Solver = nullptr;
	}
	if (Cloth)
	{
		delete Cloth;
		Cloth = nullptr;
	}
	if (Fabric)
	{
		Fabric->decRefCount();
		Fabric = nullptr;
	}
	CachedIndices.Reset();
	CachedInvMasses.Reset();
	SimulationData.Positions.Reset();
	SimulationData.Normals.Reset();
}

void FClothingSimulationNv::SetWindVelocity(const FVector& Wind)
{
	if (Cloth)
		Cloth->setWindVelocity(physx::PxVec3(Wind.X, Wind.Y, Wind.Z));
}

void FClothingSimulationNv::Teleport(const TArray<FVector>& TargetPositions)
{
	if (!Cloth)
		return;
	auto Mapped = Cloth->getCurrentParticles();
	const uint32 Count = std::min(static_cast<uint32>(Mapped.size()), static_cast<uint32>(TargetPositions.Num()));
	for (uint32 i = 0; i < Count; ++i)
	{
		const FVector& P = TargetPositions[static_cast<int32>(i)];
		Mapped[i] = physx::PxVec4(P.X, P.Y, P.Z, CachedInvMasses[static_cast<int32>(i)]);
		SimulationData.Positions[static_cast<int32>(i)] = P;
	}
	Cloth->teleport(physx::PxVec3(0.0f, 0.0f, 0.0f));
	RecomputeNormals();
}

void FClothingSimulationNv::Simulate(float DeltaTime, const TArray<FVector>* KinematicTargets)
{
	if (!Solver || !Cloth || DeltaTime <= 0.0f)
		return;

	if (KinematicTargets && !KinematicTargets->IsEmpty())
	{
		auto Mapped = Cloth->getCurrentParticles();
		const uint32 Count = std::min(static_cast<uint32>(Mapped.size()), static_cast<uint32>(KinematicTargets->Num()));
		for (uint32 i = 0; i < Count; ++i)
		{
			if (CachedInvMasses[static_cast<int32>(i)] <= 0.0f)
			{
				const FVector& P = (*KinematicTargets)[static_cast<int32>(i)];
				Mapped[i] = physx::PxVec4(P.X, P.Y, P.Z, 0.0f);
			}
		}
	}

	const float ClampedDt = FMath::Clamp(DeltaTime, 1.0f / 240.0f, 1.0f / 20.0f);
	if (Solver->beginSimulation(ClampedDt))
	{
		for (int i = 0; i < Solver->getSimulationChunkCount(); ++i)
			Solver->simulateChunk(i);
		Solver->endSimulation();
	}

	auto Result = Cloth->getCurrentParticles();
	const int32 Count = std::min(static_cast<int32>(Result.size()), SimulationData.Positions.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		const physx::PxVec4& P = Result[static_cast<uint32>(i)];
		SimulationData.Positions[i] = FVector(P.x, P.y, P.z);
	}
	RecomputeNormals();
}

void FClothingSimulationNv::RecomputeNormals()
{
	const int32 NumVerts = SimulationData.Positions.Num();
	SimulationData.Normals.SetNum(NumVerts);
	for (int32 i = 0; i < NumVerts; ++i)
		SimulationData.Normals[i] = FVector::ZeroVector;

	for (int32 i = 0; i + 2 < CachedIndices.Num(); i += 3)
	{
		const uint32 I0 = CachedIndices[i];
		const uint32 I1 = CachedIndices[i + 1];
		const uint32 I2 = CachedIndices[i + 2];
		if (I0 >= static_cast<uint32>(NumVerts) || I1 >= static_cast<uint32>(NumVerts) || I2 >= static_cast<uint32>(NumVerts))
			continue;
		const FVector& P0 = SimulationData.Positions[static_cast<int32>(I0)];
		const FVector& P1 = SimulationData.Positions[static_cast<int32>(I1)];
		const FVector& P2 = SimulationData.Positions[static_cast<int32>(I2)];
		const FVector FaceN = FVector::Cross(P1 - P0, P2 - P0);
		SimulationData.Normals[static_cast<int32>(I0)] += FaceN;
		SimulationData.Normals[static_cast<int32>(I1)] += FaceN;
		SimulationData.Normals[static_cast<int32>(I2)] += FaceN;
	}

	for (int32 i = 0; i < NumVerts; ++i)
	{
		const float LenSq = SimulationData.Normals[i].SizeSquared();
		SimulationData.Normals[i] = LenSq > 1e-8f ? SimulationData.Normals[i].Normalized() : FVector(0.0f, 0.0f, 1.0f);
	}
}
