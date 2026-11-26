#pragma once

#include "Core/Types.h"
#include "Containers/Array.h"
#include "Math/EngineMath.h"
#include "Render/SkeletalMeshData.h"

struct FClothPhysicalMeshData
{
	TArray<FVector> Vertices;
	TArray<uint32> Indices;
	TArray<float> InverseMasses;
	TArray<float> MaxDistances;
	TArray<FSkinWeight> SkinWeights;
};

struct FClothVertMapping
{
	uint32 SimIndex = 0;
	float BlendWeight = 0.0f; // 0 = 일반 스키닝, 1 = Cloth 적용
};

struct FClothSimulVertex
{
	FVector Position = FVector::ZeroVector;
	float Pad0 = 0.0f;
	FVector Normal = FVector(0.0f, 0.0f, 1.0f);
	float Pad1 = 0.0f;
};

struct FClothingSimulationData
{
	TArray<FVector> Positions;
	TArray<FVector> Normals;
};

struct FClothConfigData
{
	float GravityScale = 1.0f;
	float Damping = 0.25f;
	float Drag = 0.1f;
	float StretchStiffness = 0.9f;
	float BendStiffness = 0.5f;
	float ShearStiffness = 0.5f;
	float TetherStiffness = 1.0f;
	float Friction = 0.2f;
	FVector WindVelocity = FVector::ZeroVector;
};
