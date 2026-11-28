#include "EnginePCH.h"
#include "TerrainCollisionComponent.h"
#include "Terrain/Terrain.h"
#include "Physics/PhysXSDK.h"
#include "Physics/PhysXConversion.h"
#include "Engine/World.h"

UTerrainCollisionComponent::UTerrainCollisionComponent()
{
	BodyInstance.SetCollisionProfile(ECollisionPreset::BlockAll);
	bGenerateOverlapEvents = false;
}

bool UTerrainCollisionComponent::ShouldCreatePhysicsState() const
{
	const ATerrain* Terrain = Cast<ATerrain>(GetOwner());
	return Super::ShouldCreatePhysicsState() && Terrain && Terrain->HasHeightData();
}

void UTerrainCollisionComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();

	using namespace physx;
	ATerrain* Terrain = Cast<ATerrain>(GetOwner());
	FPhysicsScene* Scene = GetWorld()->GetPhysicsScene();
	PxPhysics* Physics = PhysXSDK::GetPhysics();
	if (!Terrain || !Scene || !Physics)
		return;

	const FTerrainCoordinates Coords = Terrain->GetCoordinates();
	const TArray<uint16>& Heights = Terrain->GetHeights();

	TArray<PxHeightFieldSample> Samples;
	Samples.SetNum(Heights.Num());
	for (int32 Index = 0; Index < Heights.Num(); ++Index)
	{
		PxHeightFieldSample& Sample = Samples[Index];
		Sample.height = static_cast<PxI16>(static_cast<int32>(Heights[Index]) - TERRAIN_HEIGHT_MID);
		// 대각선 (X,Y)→(X+1,Y+1). 렌더 메시와 같은 분할
		Sample.setTessFlag();
	}

	PxHeightFieldDesc Desc;
	Desc.format = PxHeightFieldFormat::eS16_TM;
	Desc.nbRows = static_cast<PxU32>(Coords.SizeY);
	Desc.nbColumns = static_cast<PxU32>(Coords.SizeX);
	Desc.samples.data = Samples.GetData();
	Desc.samples.stride = sizeof(PxHeightFieldSample);

	PxCooking* Cooking = PxCreateCooking(PX_PHYSICS_VERSION, Physics->getFoundation(),
		PxCookingParams(Physics->getTolerancesScale()));
	if (!Cooking)
		return;
	PxHeightField* HeightField = Cooking->createHeightField(Desc, Physics->getPhysicsInsertionCallback());
	Cooking->release();
	if (!HeightField)
		return;

	const PxHeightFieldGeometry Geometry(HeightField, PxMeshGeometryFlags(),
		Coords.GetWorldHeightScale(), Coords.GetWorldCellSize(), Coords.GetWorldCellSize());

	PxMaterial* Material = Physics->createMaterial(0.7f, 0.7f, 0.0f);
	PxRigidStatic* Actor = Physics->createRigidStatic(PxTransform(U2PVector(Coords.Origin)));
	PxShape* Shape = (Material && Actor)
		? PxRigidActorExt::createExclusiveShape(*Actor, Geometry, *Material)
		: nullptr;

	// shape가 참조를 잡았으므로 생성 핸들은 바로 놓는다
	HeightField->release();
	if (Material)
		Material->release();
	if (!Shape)
	{
		if (Actor)
			Actor->release();
		return;
	}

	// 하이트필드 로컬 (X=row, Y=높이, Z=column) → 월드 (Y, Z, X)
	Shape->setLocalPose(PxTransform(PxQuat(0.5f, 0.5f, 0.5f, 0.5f)));

	if (!BodyInstance.InitBodyFromActor(*Actor, this, *Scene))
		Actor->release();
}