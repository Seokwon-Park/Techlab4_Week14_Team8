#pragma once

#include "Core/Types.h"
#include "Clothing/ClothingTypes.h"

class UClothingAssetCommon;

namespace nv::cloth
{
	class Fabric;
	class Cloth;
	class Solver;
}

// 단일 UClothingAssetCommon 인스턴스의 NvCloth 시뮬레이션 컨텍스트를 캡슐화한다.
class FClothingSimulationNv
{
public:
	FClothingSimulationNv() = default;
	~FClothingSimulationNv();

	FClothingSimulationNv(const FClothingSimulationNv&) = delete;
	FClothingSimulationNv& operator=(const FClothingSimulationNv&) = delete;

	bool Initialize(const UClothingAssetCommon& Asset);
	void Shutdown();

	void Simulate(float DeltaTime, const TArray<FVector>* KinematicTargets = nullptr);
	void Teleport(const TArray<FVector>& TargetPositions);
	void SetWindVelocity(const FVector& Wind);

	bool IsInitialized() const { return Cloth != nullptr && Solver != nullptr; }
	const FClothingSimulationData& GetSimulationData() const { return SimulationData; }

private:
	void RecomputeNormals();

	nv::cloth::Fabric* Fabric = nullptr;
	nv::cloth::Cloth* Cloth = nullptr;
	nv::cloth::Solver* Solver = nullptr;

	TArray<uint32> CachedIndices;
	TArray<float> CachedInvMasses;
	FClothingSimulationData SimulationData;
};
