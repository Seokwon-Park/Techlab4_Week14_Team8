#include "EnginePCH.h"
#include "GameFramework/Actor/VehiclePawn.h"

#include "Asset/AssetManager.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Core/EngineLog.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXConversion.h"
#include "Physics/PhysXSDK.h"
#include "Render/RenderCommand.h"

#include <PxPhysicsAPI.h>
#include <vehicle/PxVehicleDrive4W.h>
#include <vehicle/PxVehicleTireFriction.h>
#include <vehicle/PxVehicleUpdate.h>
#include <vehicle/PxVehicleUtil.h>
#include <vehicle/PxVehicleUtilControl.h>
#include <vehicle/PxVehicleUtilSetup.h>

using namespace physx;

namespace
{
	constexpr float WheelRadius = 0.35f;
	constexpr float WheelWidth = 0.20f;
	constexpr float AvanteNMeshScale = 0.01f;
	constexpr PxU32 VehicleChassisQueryBit = 1u << 31;
	constexpr float RacingStraight = 82.0f;
	constexpr float RacingRadius = 46.0f;
	constexpr float RacingArc = PxPi * RacingRadius;
	constexpr float RacingLap = 4.0f * RacingStraight + 2.0f * RacingArc;

	float RacingProgress(const PxVec3& Position)
	{
		if (Position.x > RacingStraight)
			return 2.0f * RacingStraight + std::atan2(Position.x - RacingStraight, -Position.y) * RacingRadius;
		if (Position.x < -RacingStraight)
			return 4.0f * RacingStraight + RacingArc + std::atan2(-RacingStraight - Position.x, Position.y) * RacingRadius;
		return Position.y < 0.0f ? Position.x + RacingStraight : 3.0f * RacingStraight + RacingArc - Position.x;
	}

	PxVec3 RacingTarget(float Progress, float Lane)
	{
		Progress = std::fmod(Progress, RacingLap);
		const float Radius = RacingRadius + Lane;
		if (Progress < 2.0f * RacingStraight)
			return PxVec3(Progress - RacingStraight, -Radius, 0.0f);
		Progress -= 2.0f * RacingStraight;
		if (Progress < RacingArc)
			return PxVec3(RacingStraight + Radius * PxSin(Progress / RacingRadius), -Radius * PxCos(Progress / RacingRadius), 0.0f);
		Progress -= RacingArc;
		if (Progress < 2.0f * RacingStraight)
			return PxVec3(RacingStraight - Progress, Radius, 0.0f);
		Progress -= 2.0f * RacingStraight;
		return PxVec3(-RacingStraight - Radius * PxSin(Progress / RacingRadius), Radius * PxCos(Progress / RacingRadius), 0.0f);
	}

	FTransform InterpolatePose(const FTransform& Previous, const FTransform& Current, float Alpha)
	{
		return FTransform(FMath::Slerp(Previous.Rotation, Current.Rotation, Alpha),
			FMath::Lerp(Previous.Location, Current.Location, Alpha), FMath::Lerp(Previous.Scale, Current.Scale, Alpha));
	}

	PxQueryHitType::Enum FilterSuspensionHit(
		PxFilterData, PxFilterData ShapeFilter, const void*, PxU32, PxHitFlags&)
	{
		// 트리거 박스나 캐릭터 캡슐 같은 QueryOnly Shape를 바닥으로 밟지 않는다
		if (ShapeFilter.word3 & (VehicleChassisQueryBit | QueryOnlyShapeQueryBit))
			return PxQueryHitType::eNONE;

		return PxQueryHitType::eBLOCK;
	}
}

struct AVehiclePawn::FVehiclePhysics
{
	FPhysicsScene* Scene = nullptr;
	FDelegateHandle PreStepHandle = 0;
	FDelegateHandle PostStepHandle = 0;
	PxVehicleDrive4W* Drive = nullptr;
	PxBatchQuery* BatchQuery = nullptr;
	PxVehicleDrivableSurfaceToTireFrictionPairs* Friction = nullptr;
	PxRaycastQueryResult RayHits[WheelCount]{};
	PxWheelQueryResult WheelResults[WheelCount]{};
	PxRigidDynamic* ChassisBody = nullptr;
	bool bAvanteN = false;
	float OriginalMass = 0.0f;
	PxTransform OriginalCenterOfMass{PxIdentity};
	PxVec3 OriginalInertiaTensor{0.0f};
	PxShape* ChassisShape = nullptr;
	PxBoxGeometry OriginalBox;
	PxTransform OriginalShapePose{PxIdentity};
	bool bRaisedChassis = false;
	PxFilterData OriginalQueryFilter;
	PxFilterData OriginalSimulationFilter;
	FDelegateHandle ContactHandle = 0;
	std::unique_ptr<UStaticMesh> DentedMesh;
	bool bMeshDentDirty = false;
	bool bBoostVignetteApplied = false;
	PxVec3 LastTirePoints[WheelCount]{};
	bool bHasTirePoint[WheelCount]{};
	float OpponentLane = 2.3f;
	int32 LastGroundedWheels = -1;
	struct FPose
	{
		FTransform Chassis;
		FTransform Wheels[WheelCount];
	};
	FPose PreviousPose;
	FPose CurrentPose;

	void ApplyCorneringAssist(float ForwardSpeed, const PxVec3& GroundNormal) const
	{
		const float Strength = PxClamp((ForwardSpeed - 8.0f) / 8.0f, 0.0f, 1.0f);
		if (Strength == 0.0f)
			return;

		// 접지면 위에서만 횡미끄럼을 줄인다. 앞쪽 속도는 타이어와 구동계가 결정한다.
		const PxVec3 Up = GroundNormal.getNormalized();
		const PxVec3 Right = Up.cross(ChassisBody->getGlobalPose().q.getBasisVector0()).getNormalized();
		const float SideSpeed = ChassisBody->getLinearVelocity().dot(Right);
		const float SideAcceleration = PxClamp(-4.0f * SideSpeed, -7.0f, 7.0f);
		ChassisBody->addForce(Right * (SideAcceleration * Strength), PxForceMode::eACCELERATION);

		const float SteerAngle = 0.5f * (WheelResults[0].steerAngle + WheelResults[1].steerAngle);
		const float Wheelbase = Drive->mDriveSimData.getAckermannGeometryData().mAxleSeparation;
		const float YawRate = ForwardSpeed * PxTan(SteerAngle) / Wheelbase;
		const float MaxYawRate = 18.5f / ForwardSpeed;
		const float TargetYawRate = PxClamp(YawRate, -MaxYawRate, MaxYawRate);
		const float ActualYawRate = ChassisBody->getAngularVelocity().dot(Up);
		const float YawAcceleration = PxClamp(5.0f * (TargetYawRate - ActualYawRate), -3.0f, 3.0f);
		ChassisBody->addTorque(Up * (YawAcceleration * Strength), PxForceMode::eACCELERATION);
	}

	~FVehiclePhysics()
	{
		if (ChassisShape)
		{
			ChassisShape->setQueryFilterData(OriginalQueryFilter);
			if (ContactHandle)
				ChassisShape->setSimulationFilterData(OriginalSimulationFilter);
			if (bRaisedChassis)
			{
				ChassisShape->setGeometry(OriginalBox);
				ChassisShape->setLocalPose(OriginalShapePose);
			}
		}
		if (BatchQuery)
			BatchQuery->release();
		if (Drive)
			Drive->free();
		if (ChassisBody)
		{
			ChassisBody->setMass(OriginalMass);
			ChassisBody->setCMassLocalPose(OriginalCenterOfMass);
			ChassisBody->setMassSpaceInertiaTensor(OriginalInertiaTensor);
		}
		if (Friction)
			Friction->release();
	}
};

AVehiclePawn::AVehiclePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = ETickingGroup::PostPhysics;

	UStaticMeshComponent* Chassis = CreateDefaultSubobject<UStaticMeshComponent>("Chassis");
	SetRootComponent(Chassis);
	Chassis->SetSimulatePhysics(true);

	const char* WheelNames[WheelCount] = {"WheelFL", "WheelFR", "WheelRL", "WheelRR"};
	UStaticMesh* Cylinder = UAssetManager::GetAssetByPath<UStaticMesh>("Cylinder");
	for (PxU32 Index = 0; Index < WheelCount; ++Index)
	{
		UVehicleWheelMeshComponent* WheelMesh = CreateDefaultSubobject<UVehicleWheelMeshComponent>(WheelNames[Index]);
		WheelMesh->SetStaticMesh(Cylinder);
		WheelMesh->SetupAttachment(Chassis);
		WheelMesh->bGenerateOverlapEvents = false;
		WheelMesh->SetVisibility(false);
		WheelMeshes[Index] = WheelMesh;
	}

	USpringArmComponent* SpringArm = CreateDefaultSubobject<USpringArmComponent>("SpringArmComponent");
	SpringArm->SetupAttachment(Chassis);
	SpringArm->PrimaryComponentTick.TickGroup = ETickingGroup::PostUpdateWork;
	SpringArm->TargetArmLength = 6.5f;
	// 장애물에 막혀 팔이 짧아져도 카메라가 차체 위에 머물도록 한다.
	SpringArm->TargetOffset = FVector(0.0f, 0.0f, 1.30f);
	SpringArm->SetRelativeRotation({25.0f, 0.0f, 0.0f});
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritRoll = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 15.0f;
	SpringArm->CameraLagMaxDistance = 1.75f;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = 8.0f;
	SpringArm->CameraCollisionRecoverySpeed = 5.0f;
	SpringArm->bDrawDebug = false;

	UCameraComponent* Camera = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	Camera->SetupAttachment(SpringArm);
	Camera->SetExternalInputManaged(true);
}

AVehiclePawn::~AVehiclePawn() = default;

void AVehiclePawn::BeginPlay()
{
	Super::BeginPlay();
	if (!HasBegunPlay() || VehiclePhysics)
		return;

	FPhysicsScene* Scene = GetWorld()->GetPhysicsScene();
	UStaticMeshComponent* Chassis = Cast<UStaticMeshComponent>(GetRootComponent());
	PxRigidDynamic* Body = Chassis ? Chassis->GetBodyInstance()->GetRigidDynamic() : nullptr;
	if (!Scene || !Scene->GetPxScene() || !Body)
		return;

	PxSceneWriteLock Lock(*Scene->GetPxScene());

	PxShape* Shape = nullptr;
	PxBoxGeometry Box;
	PxMaterial* Material = nullptr;
	if (Body->getNbShapes() != 1 || Body->getShapes(&Shape, 1) != 1 ||
		!Shape->getBoxGeometry(Box) || Shape->getMaterials(&Material, 1) != 1)
	{
		HTR_LOG(Warning, "Vehicle Pawn requires a single box chassis");
		return;
	}

	auto State = std::make_unique<FVehiclePhysics>();
	State->Scene = Scene;

	PxBatchQueryDesc QueryDesc(WheelCount, 0, 0);
	QueryDesc.preFilterShader = FilterSuspensionHit;
	QueryDesc.queryMemory.userRaycastResultBuffer = State->RayHits;
	State->BatchQuery = Scene->GetPxScene()->createBatchQuery(QueryDesc);
	State->Friction = PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(1, 1);
	if (!State->BatchQuery || !State->Friction)
		return;

	const PxMaterial* SurfaceMaterials[] = {Material};
	const PxVehicleDrivableSurfaceType SurfaceTypes[] = {{0}};
	State->Friction->setup(1, 1, SurfaceMaterials, SurfaceTypes);

	const UStaticMesh* ChassisMesh = Chassis->GetStaticMesh();
	const bool bAvanteN = ChassisMesh && ChassisMesh->GetPath().ends_with("/AvanteN_Body_Static.fbx.bin");
	State->bAvanteN = bAvanteN;
	if (bAvanteN || bRacingOpponent)
		State->Friction->setTypePairFriction(0, 0, 1.45f);
	State->ChassisBody = Body;
	State->ChassisShape = Shape;
	State->OriginalQueryFilter = Shape->getQueryFilterData();
	State->OriginalMass = Body->getMass();
	State->OriginalCenterOfMass = Body->getCMassLocalPose();
	State->OriginalInertiaTensor = Body->getMassSpaceInertiaTensor();
	const PxVec3 ShapeCenter = Shape->getLocalPose().p;
	if (bUseIntegratedWheelMesh)
	{
		// The imported wheel geometry reaches the ground. Leave room below the collision box for suspension travel.
		State->OriginalBox = Box;
		State->OriginalShapePose = Shape->getLocalPose();
		const float Clearance = PxMin(WheelRadius * 0.7f, Box.halfExtents.z * 0.4f);
		PxBoxGeometry RaisedBox = Box;
		RaisedBox.halfExtents.z -= Clearance;
		PxTransform RaisedPose = State->OriginalShapePose;
		RaisedPose.p.z += Clearance;
		Shape->setGeometry(RaisedBox);
		Shape->setLocalPose(RaisedPose);
		State->bRaisedChassis = true;
	}
	if ((bAvanteN || bRacingOpponent) && !PxRigidBodyExt::setMassAndUpdateInertia(*Body, bAvanteN ? 1450.0f : 2200.0f))
		return;

	PxVehicleWheelsSimData* Wheels = PxVehicleWheelsSimData::allocate(WheelCount);
	Wheels->setChassisMass(Body->getMass());

	PxVehicleWheelData Wheel;
	Wheel.mRadius = WheelRadius;
	Wheel.mWidth = bAvanteN ? 0.23f : WheelWidth;
	Wheel.mMOI = 0.5f * Wheel.mMass * Wheel.mRadius * Wheel.mRadius;

	const float X = bAvanteN ? 1.38677f : Box.halfExtents.x * 0.8f;
	const float Y = bAvanteN ? 0.80915f : Box.halfExtents.y * (bUseIntegratedWheelMesh ? 0.84f : 1.0f) +
		(bUseIntegratedWheelMesh ? 0.0f : Wheel.mWidth * 0.5f);
	const float Z = bAvanteN ? -0.39166f : -Box.halfExtents.z + (bUseIntegratedWheelMesh ? WheelRadius : 0.0f);
	const PxVec3 WheelPositions[] = {
		{ X, -Y, Z}, { X, Y, Z}, // 앞 왼쪽, 앞 오른쪽
		{-X, -Y, Z}, {-X, Y, Z}  // 뒤 왼쪽, 뒤 오른쪽
	};

	const PxVec3 WheelOrigin = bAvanteN ? PxVec3(0.0f) : ShapeCenter;
	PxTransform CenterOfMass = Body->getCMassLocalPose();
	CenterOfMass.p.z = ShapeCenter.z - Box.halfExtents.z * 0.7f;
	Body->setCMassLocalPose(CenterOfMass);

	float SprungMasses[WheelCount]{};
	if (bAvanteN || bRacingOpponent)
	{
		PxVehicleComputeSprungMasses(WheelCount, WheelPositions, CenterOfMass.p - WheelOrigin, Body->getMass(), 2, SprungMasses);
		Wheels->setSubStepCount(5.0f, 4, 3);
	}

	PxVec3 InertiaTensor = Body->getMassSpaceInertiaTensor();
	InertiaTensor.y *= 2.0f;
	Body->setMassSpaceInertiaTensor(InertiaTensor);

	for (PxU32 Index = 0; Index < WheelCount; ++Index)
	{
		const PxVec3 WheelPosition = WheelPositions[Index] + WheelOrigin;
		const PxVec3 Offset = WheelPosition - CenterOfMass.p;
		const bool bFrontWheel =
			Index == PxVehicleDrive4WWheelOrder::eFRONT_LEFT ||
			Index == PxVehicleDrive4WWheelOrder::eFRONT_RIGHT;
		Wheel.mMaxSteer = bFrontWheel ? PxPi * (40.0f / 180.0f) : 0.0f;
		Wheel.mMaxHandBrakeTorque = bFrontWheel ? 0.0f : 3000.0f;
		Wheels->setWheelData(Index, Wheel);
		Wheels->setTireData(Index, PxVehicleTireData{});
		if (bAvanteN || bRacingOpponent)
		{
			PxVehicleSuspensionData Suspension = Wheels->getSuspensionData(Index);
			Suspension.mSprungMass = SprungMasses[Index];
			Suspension.mSpringStrength = Suspension.mSprungMass * 120.0f;
			Suspension.mSpringDamperRate = Suspension.mSprungMass * 17.0f;
			Suspension.mMaxCompression = 0.16f;
			Suspension.mMaxDroop = 0.18f;
			Wheels->setSuspensionData(Index, Suspension);
		}
		Wheels->setWheelCentreOffset(Index, Offset);
		Wheels->setSuspTravelDirection(Index, PxVec3(0.0f, 0.0f, -1.0f));
		Wheels->setSuspForceAppPointOffset(Index, Offset);
		Wheels->setTireForceAppPointOffset(Index, Offset);
		Wheels->setWheelShapeMapping(Index, -1);
		State->WheelResults[Index].localPose = PxTransform(WheelPosition);
	}

	PxVehicleAckermannGeometryData Steering;
	Steering.mAxleSeparation = 2.0f * X;
	Steering.mFrontWidth = 2.0f * Y;
	Steering.mRearWidth = 2.0f * Y;

	PxVehicleEngineData Engine;
	PxVehicleGearsData Gears;
	PxVehicleAutoBoxData AutoBox;
	if (bAvanteN || bRacingOpponent)
	{
		// 데모용 레이스 튜닝: 저단의 구동력을 높이고 변속 중 가속이 끊기는 시간을 줄인다.
		Engine.mPeakTorque = 720.0f;
		Engine.mMaxOmega = 720.0f;
		Gears.setGearRatio(PxVehicleGearsData::eSECOND, 2.55f);
		Gears.setGearRatio(PxVehicleGearsData::eTHIRD, 1.75f);
		Gears.setGearRatio(PxVehicleGearsData::eFOURTH, 1.24f);
		Gears.mSwitchTime = 0.15f;
		AutoBox.setLatency(0.35f);
		for (PxU32 Gear = PxVehicleGearsData::eFIRST; Gear < Gears.mNbRatios; ++Gear)
			AutoBox.mUpRatios[Gear] = 0.84f;
	}

	PxVehicleDriveSimData4W DriveSettings;
	DriveSettings.setEngineData(Engine);
	DriveSettings.setGearsData(Gears);
	DriveSettings.setClutchData(PxVehicleClutchData{});
	DriveSettings.setAutoBoxData(AutoBox);
	DriveSettings.setDiffData(PxVehicleDifferential4WData{});
	DriveSettings.setAckermannGeometryData(Steering);
	State->Drive = PxVehicleDrive4W::create(
		PhysXSDK::GetPhysics(), Body, *Wheels, DriveSettings, 0);
	Wheels->free();
	if (!State->Drive)
		return;

	State->Drive->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);
	State->Drive->mDriveDynData.setUseAutoGears(true);

	PxFilterData QueryFilter = State->OriginalQueryFilter;
	QueryFilter.word3 |= VehicleChassisQueryBit;
	Shape->setQueryFilterData(QueryFilter);

	State->CurrentPose.Chassis = GetActorTransform();
	for (uint32 Index = 0; Index < WheelCount; ++Index)
		State->CurrentPose.Wheels[Index] = WheelMeshes[Index]->GetTransform();
	State->PreviousPose = State->CurrentPose;

	State->PreStepHandle = Scene->OnPreStep.AddDynamic(this, &AVehiclePawn::TickVehiclePhysics);
	if (State->PreStepHandle != 0)
	{
		State->PostStepHandle = Scene->OnPostStep.AddDynamic(this, &AVehiclePawn::SyncWheelMeshes);
		if (bRacingDemo)
		{
			State->OriginalSimulationFilter = Shape->getSimulationFilterData();
			PxFilterData SimulationFilter = State->OriginalSimulationFilter;
			SimulationFilter.word3 |= FPhysicsScene::ContactReportBit;
			Shape->setSimulationFilterData(SimulationFilter);
			State->ContactHandle = Chassis->OnComponentContact.AddDynamic(this, &AVehiclePawn::HandleChassisContact);
			if (!bRacingOpponent)
				GetWorld()->GetScene().TireMarkTexture = UAssetManager::GetAssetByPath<UTexture2D>("Models/RacingDemo/TireMark.png");
		}
		VehiclePhysics = std::move(State);
	}
}

void AVehiclePawn::TickVehiclePhysics(float StepDelta)
{
	// 차체 충돌 해제 중 서스펜션 레이캐스트와 접지력 적용 생략
	if (!GetActorEnableCollision())
	{
		return;
	}
	FVehiclePhysics& State = *VehiclePhysics;
	PxScene& Scene = *State.Scene->GetPxScene();
	PxSceneWriteLock Lock(Scene);

	const PxVec3 Gravity = Scene.getGravity();
	if (Gravity.magnitudeSquared() == 0.0f)
		return;

	PxVehicleWheels* Vehicles[] = {State.Drive};
	PxVehicleSuspensionRaycasts(State.BatchQuery, 1, Vehicles, WheelCount, State.RayHits);

	// 잠든 차체는 PxVehicleUpdates가 바퀴 결과를 갱신하지 않는다.
	PxVehicleWheelQueryResult Results{State.WheelResults, WheelCount};
	const APlayerController* Controller = GetController();
	const auto IsDown = [Controller](EKeyCode Key) {
		return Controller && Controller->IsKeyDown(static_cast<int32>(Key));
	};

	const bool bBoost = bRacingDemo && !bRacingOpponent && IsDown(EKeyCode::Shift);
	const bool bForward = IsDown(EKeyCode::W) || (bBoost && !IsDown(EKeyCode::S));
	const bool bReverse = IsDown(EKeyCode::S);
	const bool bHandbrake = IsDown(EKeyCode::Space);
	const bool bBrake = bForward && bReverse;
	const bool bWantsDrive = (bForward != bReverse) && !bHandbrake;

	constexpr float ShiftSpeedLimit = 0.25f; // m/s
	const float ForwardSpeed = State.Drive->computeForwardSpeed();
	auto& DriveData = State.Drive->mDriveDynData;
	const bool bInReverse = DriveData.getTargetGear() == PxVehicleGearsData::eREVERSE;
	const bool bChangingDirection = bWantsDrive && (bReverse != bInReverse);
	if (bChangingDirection && PxAbs(ForwardSpeed) <= ShiftSpeedLimit)
	{
		DriveData.forceGearChange(
			bReverse ? PxVehicleGearsData::eREVERSE : PxVehicleGearsData::eFIRST);
	}

	const bool bOppositeMotion =
		bReverse ? ForwardSpeed > ShiftSpeedLimit : ForwardSpeed < -ShiftSpeedLimit;
	const bool bBrakeBeforeDrive = bWantsDrive &&
		(bOppositeMotion || (bChangingDirection && PxAbs(ForwardSpeed) > ShiftSpeedLimit));

	const bool bAccelerate = bWantsDrive && !bBrakeBeforeDrive;
	const bool bServiceBrake = bBrake || bBrakeBeforeDrive;
	const bool bSteerLeft = IsDown(EKeyCode::A);
	const bool bSteerRight = IsDown(EKeyCode::D);
	const bool bInAir = PxVehicleIsInAir(Results);
	PxVehicleDrive4WRawInputData Input;
	if (bRacingOpponent)
	{
		const PxTransform Pose = State.ChassisBody->getGlobalPose();
		const float Progress = RacingProgress(Pose.p);
		const PxVec3 Center = RacingTarget(Progress, 0.0f);
		const PxVec3 Outward(Center.x - PxClamp(Center.x, -RacingStraight, RacingStraight), Center.y, 0.0f);
		const PxVec3 Outer = Outward.getNormalized();
		const PxVec3 Tangent(-Outer.y, Outer.x, 0.0f);
		float DesiredLane = 2.3f;
		if (const APawn* Player = GetWorld()->GetPlayerPawn(); Player && Player != this)
		{
			const PxVec3 ToPlayer = U2PVector(Player->GetActorLocation()) - Pose.p;
			const float Ahead = ToPlayer.dot(Tangent);
			if (Ahead > -5.0f && Ahead < 18.0f && PxAbs(ToPlayer.dot(Outer)) < 5.0f)
				DesiredLane = (U2PVector(Player->GetActorLocation()) - Center).dot(Outer) > 0.0f ? -2.3f : 2.3f;
		}
		State.OpponentLane += PxClamp(DesiredLane - State.OpponentLane, -2.5f * StepDelta, 2.5f * StepDelta);
		const float LookAhead = PxClamp(6.0f + ForwardSpeed * 0.55f, 6.0f, 17.0f);
		const PxVec3 ToTarget = RacingTarget(Progress + LookAhead, State.OpponentLane) - Pose.p;
		const PxVec3 Local = Pose.q.rotateInv(PxVec3(ToTarget.x, ToTarget.y, 0.0f));
		const bool bTurningAround = Local.x < 0.0f;
		const float Wheelbase = State.Drive->mDriveSimData.getAckermannGeometryData().mAxleSeparation;
		const float WheelAngle = std::atan2(2.0f * Wheelbase * Local.y, PxMax(Local.x * Local.x + Local.y * Local.y, 1.0f));
		const float Speed = PxMax(0.0f, ForwardSpeed);
		const float SteeringLimit = PxClamp(1.0f - Speed * 0.032f, 0.22f, 1.0f);
		const float Steer = bTurningAround ? (Local.y >= 0.0f ? 1.0f : -1.0f) :
			PxClamp(WheelAngle / (SteeringLimit * 40.0f * (PxPi / 180.0f)), -1.0f, 1.0f);
		const PxVec3 Ahead = RacingTarget(Progress + LookAhead + 6.0f, 0.0f);
		const bool bApproachingCurve = PxAbs(Ahead.x) > RacingStraight + 0.5f;
		const bool bApproachingCrest = Ahead.y > 0.0f && Ahead.x > -62.0f && Ahead.x < 10.0f;
		const float DesiredSpeed = bTurningAround ? 6.0f : (bApproachingCurve || bApproachingCrest ? 16.5f : 24.0f);
		const float Pedal = (DesiredSpeed - ForwardSpeed) * 0.6f;
		Input.setAnalogAccel(PxClamp(Pedal, 0.0f, 1.0f));
		Input.setAnalogBrake(PxClamp(-Pedal, 0.0f, 1.0f));
		Input.setAnalogSteer(Steer);
		static const PxVehiclePadSmoothingData Smoothing{
			{6.0f, 8.0f, 6.0f, 5.0f, 5.0f},
			{10.0f, 12.0f, 10.0f, 6.0f, 6.0f}
		};
		static const PxReal SteerPairs[] = { 0.0f, 1.0f, 24.375f, 0.22f };
		static const PxFixedSizeLookupTable<8> SteerBySpeed(SteerPairs, 2);
		PxVehicleDrive4WSmoothAnalogRawInputsAndSetAnalogInputs(
			Smoothing, SteerBySpeed, Input, StepDelta, bInAir, *State.Drive);
	}
	else if (State.bAvanteN)
	{
		const float Steer = static_cast<float>(bSteerRight) - static_cast<float>(bSteerLeft);
		const bool bCornering = Steer != 0.0f && !bHandbrake && State.LastGroundedWheels >= 3;
		const float CornerWeight = bCornering ? PxClamp((ForwardSpeed - 12.0f) / 12.0f, 0.0f, 1.0f) : 0.0f;
		// 코너에서만 가속을 줄이고, 진입 속도가 높으면 남은 입력을 제동에 쓴다.
		const float SpeedCorrection = CornerWeight * PxClamp((ForwardSpeed - 25.0f) * 0.06f, 0.0f, 0.70f);
		const float Pedal = (bAccelerate ? 1.0f - 0.70f * CornerWeight : 0.0f) - SpeedCorrection;
		Input.setAnalogAccel(PxMax(Pedal, 0.0f));
		Input.setAnalogBrake(bServiceBrake ? 1.0f : PxMax(-Pedal, 0.0f));
		Input.setAnalogHandbrake(bHandbrake ? 1.0f : 0.0f);
		Input.setAnalogSteer(Steer);

		static const PxVehiclePadSmoothingData Smoothing{
			{6.0f, 8.0f, 6.0f, 4.0f, 4.0f},
			{10.0f, 12.0f, 10.0f, 6.0f, 6.0f}
		};
		static const PxReal SteerPairs[] = {
			0.0f, 1.00f, 6.0f, 0.76f, 12.0f, 0.44f, 18.0f, 0.22f,
			24.0f, 0.14f, 32.0f, 0.10f, 42.0f, 0.07f
		};
		static const PxFixedSizeLookupTable<8> SteerBySpeed(SteerPairs, 7);
		PxVehicleDrive4WSmoothAnalogRawInputsAndSetAnalogInputs(
			Smoothing, SteerBySpeed, Input, StepDelta, bInAir, *State.Drive);
	}
	else
	{
		Input.setDigitalAccel(bAccelerate);
		Input.setDigitalBrake(bServiceBrake);
		Input.setDigitalHandbrake(bHandbrake);
		Input.setDigitalSteerLeft(bSteerLeft);
		Input.setDigitalSteerRight(bSteerRight);
		static const PxVehicleKeySmoothingData Smoothing{
			{6.0f, 6.0f, 6.0f, 2.5f, 2.5f},
			{10.0f, 10.0f, 10.0f, 5.0f, 5.0f}
		};
		static const PxReal SteerPairs[] = {
			0.0f, 1.00f, 3.0f, 0.92f, 6.0f, 0.62f, 10.0f, 0.34f, 18.0f, 0.11f, 28.0f, 0.07f
		};
		static const PxFixedSizeLookupTable<8> SteerBySpeed(SteerPairs, 6);
		PxVehicleDrive4WSmoothDigitalRawInputsAndSetAnalogInputs(
			Smoothing, SteerBySpeed, Input, StepDelta, bInAir, *State.Drive);
	}

	PxVehicleUpdates(StepDelta, Gravity, *State.Friction, 1, Vehicles, &Results);

	int32 GroundedWheels = 0;
	PxVec3 GroundNormal(0.0f);
	for (const PxWheelQueryResult& Wheel : State.WheelResults)
	{
		if (!Wheel.isInAir)
		{
			++GroundedWheels;
			GroundNormal += Wheel.tireContactNormal;
		}
	}
	if (GroundedWheels >= 3)
	{
		if ((State.bAvanteN || bRacingOpponent) && !bHandbrake)
			State.ApplyCorneringAssist(ForwardSpeed, GroundNormal);
		if (bBoost && bAccelerate && !bReverse)
		{
			const PxVec3 Up = GroundNormal.getNormalized();
			const PxVec3 Forward = State.ChassisBody->getGlobalPose().q.getBasisVector0();
			const PxVec3 AlongRoad = (Forward - Up * Forward.dot(Up)).getNormalized();
			const float Acceleration = 10.0f * PxClamp((47.0f - ForwardSpeed) / 8.0f, 0.0f, 1.0f);
			State.ChassisBody->addForce(AlongRoad * Acceleration, PxForceMode::eACCELERATION);
		}
	}

	FScene& RenderScene = GetWorld()->GetScene();
	if (bRacingDemo && !bRacingOpponent && RenderScene.TireMarkTexture)
	{
		const float SideSpeed = State.ChassisBody->getLinearVelocity().dot(State.ChassisBody->getGlobalPose().q.getBasisVector1());
		for (uint32 Index = 0; Index < WheelCount; ++Index)
		{
			const PxWheelQueryResult& Wheel = State.WheelResults[Index];
			const bool bSliding = PxAbs(SideSpeed) > 2.0f && PxAbs(Wheel.lateralSlip) > 0.13f;
			const bool bLockedRear = bHandbrake && Index >= 2 && PxAbs(Wheel.longitudinalSlip) > 0.25f;
			if (Wheel.isInAir || !Wheel.tireContactShape || Wheel.tireContactNormal.z < 0.65f ||
				PxAbs(ForwardSpeed) < 4.0f || (!bSliding && !bLockedRear))
			{
				State.bHasTirePoint[Index] = false;
				continue;
			}
			const PxVec3 Point = Wheel.tireContactPoint;
			if (State.bHasTirePoint[Index])
			{
				const PxVec3 Delta = Point - State.LastTirePoints[Index];
				const PxVec3 Normal = Wheel.tireContactNormal;
				const PxVec3 Tangent = Delta - Normal * Delta.dot(Normal);
				const float Distance = Tangent.magnitude();
				if (Distance < 0.35f) continue;
				if (Distance < 2.0f)
				{
					const PxVec3 Along = Tangent / Distance;
					const PxVec3 Center = (Point + State.LastTirePoints[Index]) * 0.5f;
					RenderScene.AddTireMark({ FVector4(P2UVector(Center), 0.12f),
						FVector4(P2UVector(Along), Distance * 0.5f + 0.045f),
						FVector4(P2UVector(Normal.cross(Along)), 0.115f), FVector4(P2UVector(Normal), 1.0f) });
				}
			}
			State.LastTirePoints[Index] = Point;
			State.bHasTirePoint[Index] = true;
		}
	}

	if (GroundedWheels != State.LastGroundedWheels)
	{
		State.LastGroundedWheels = GroundedWheels;
		HTR_LOG(Info, "Vehicle grounded wheels: {}/4", GroundedWheels);
	}
}

void AVehiclePawn::SyncWheelMeshes(float)
{
	FVehiclePhysics& State = *VehiclePhysics;
	State.PreviousPose = State.CurrentPose;
	State.CurrentPose.Chassis = GetActorTransform();
	if (bUseIntegratedWheelMesh) return;

	const float ChassisScale = GetRootComponent()->GetWorldScale3D().X;
	const bool bAvanteN = State.bAvanteN;
	const PxQuat MeshBasis = bAvanteN ? PxQuat(PxIdentity) : PxQuat(-PxHalfPi, PxVec3(1.0f, 0.0f, 0.0f));
	const FVector MeshScale = bAvanteN
		? FVector(AvanteNMeshScale, AvanteNMeshScale, AvanteNMeshScale)
		: FVector(WheelRadius, WheelRadius, WheelWidth);

	for (PxU32 Index = 0; Index < WheelCount; ++Index)
	{
		const PxTransform& Pose = State.WheelResults[Index].localPose;
		FTransform& Wheel = State.CurrentPose.Wheels[Index];
		Wheel = FTransform(
			P2UQuat(Pose.q * MeshBasis),
			P2UVector(Pose.p) / ChassisScale,
			MeshScale / ChassisScale);
		WheelMeshes[Index]->SetTransform(Wheel);
		WheelMeshes[Index]->SetVisibility(true);
	}
}

void AVehiclePawn::HandleChassisContact(const FVector& Point, const FVector& Inward, float Impulse)
{
	FVehiclePhysics& State = *VehiclePhysics;
	const float ImpactSpeed = Impulse / (State.bAvanteN ? 1450.0f : 2200.0f);
	if (ImpactSpeed < 0.7f || PxAbs(Inward.Z) > 0.65f) return;

	const UStaticMesh* Original = CastChecked<UStaticMeshComponent>(GetRootComponent())->GetStaticMesh();
	if (!Original) return;
	const FStaticMeshData& Source = Original->GetMeshData();
	if (!State.DentedMesh)
	{
		State.DentedMesh.reset(NewObject<UStaticMesh>());
		State.DentedMesh->Materials = Original->Materials;
		State.DentedMesh->MeshData.Vertices = Source.Vertices;
		State.DentedMesh->MeshData.Indices = Source.Indices;
		State.DentedMesh->MeshData.Sections = Source.Sections;
		State.DentedMesh->MeshData.MaterialSlots = Source.MaterialSlots;
		State.DentedMesh->MeshData.AABB = Source.AABB;
	}

	const float Scale = GetActorScale3D().X;
	const FVector DentCenter = Point + FVector(0.0f, 0.0f, 0.13f);
	const float Radius = 0.95f;
	const float Depth = FMath::Clamp(ImpactSpeed * 0.045f, 0.04f, 0.24f);
	TArray<FVertex>& Vertices = State.DentedMesh->MeshData.Vertices;
	for (uint32 Index = 0; Index < static_cast<uint32>(Vertices.Num()); ++Index)
	{
		const FVector Offset = Source.Vertices[Index].Position * Scale - DentCenter;
		const float Weight = 1.0f - Offset.SizeSquared() / (Radius * Radius);
		if (Weight <= 0.0f) continue;

		FVertex& Vertex = Vertices[Index];
		const FVector Displacement = (Vertex.Position - Source.Vertices[Index].Position) * Scale;
		const FVector Dented = (Displacement + Inward * (Depth * Weight * Weight)).GetClampedToMaxSize(0.32f);
		Vertex.Position = Source.Vertices[Index].Position + Dented / Scale;
		const FVector Gradient = Offset * (-4.0f * Depth * Weight / (Radius * Radius));
		const FVector NewNormal = (Vertex.Normal - Gradient * Vertex.Normal.Dot(Inward)).GetSafeNormal();
		if (NewNormal.SizeSquared() > 0.0f) Vertex.Normal = NewNormal;
		State.bMeshDentDirty = true;
	}
}

void AVehiclePawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!VehiclePhysics) return;

	// 고정 스텝이 없는 프레임에도 차체와 바퀴의 렌더 위치만 갱신한다.
	FVehiclePhysics& State = *VehiclePhysics;
	UStaticMeshComponent* ChassisComponent = CastChecked<UStaticMeshComponent>(GetRootComponent());
	if (State.bMeshDentDirty)
	{
		UStaticMesh& Mesh = *State.DentedMesh;
		const FStaticMeshData& Data = Mesh.GetMeshData();
		auto Buffer = RenderCommand::CreateStaticVertexBuffer(
			Data.Vertices.GetData(), static_cast<uint32>(Data.Vertices.Num()) * sizeof(FVertex), sizeof(FVertex));
		if (!Mesh.IndexBuffer)
			Mesh.IndexBuffer = RenderCommand::CreateStaticIndexBuffer(Data.Indices.GetData(), Data.Indices.Num());
		if (Buffer && Mesh.IndexBuffer)
		{
			Mesh.VertexBuffer = std::move(Buffer);
			Mesh.RebuildRenderData();
			ChassisComponent->SetRenderMeshOverride(&Mesh);
			State.bMeshDentDirty = false;
		}
	}
	if (bRacingDemo && !bRacingOpponent)
	{
		const APlayerController* Controller = GetController();
		if (APlayerCameraManager* Camera = Controller ? Controller->GetCameraManager() : nullptr)
		{
			const bool bShow = Camera->GetViewTarget().Target == this && Controller->IsKeyDown(static_cast<int32>(EKeyCode::Shift));
			if (bShow || State.bBoostVignetteApplied)
				Camera->VignetteIntensity = bShow ? 0.72f : 0.0f;
			State.bBoostVignetteApplied = bShow;
		}
	}
	const float Alpha = State.Scene->GetInterpolationAlpha();
	const FTransform Chassis = InterpolatePose(State.PreviousPose.Chassis, State.CurrentPose.Chassis, Alpha);
	ChassisComponent->SetRenderTransformOverride(Chassis);
	if (bUseIntegratedWheelMesh) return;
	const FMatrix ChassisMatrix = Chassis.GetLocalMatrix();
	for (uint32 Index = 0; Index < WheelCount; ++Index)
	{
		const FTransform Wheel = InterpolatePose(State.PreviousPose.Wheels[Index], State.CurrentPose.Wheels[Index], Alpha);
		WheelMeshes[Index]->SetRenderTransformOverride(FTransform(Chassis.Rotation * Wheel.Rotation,
			ChassisMatrix.TransformPosition(Wheel.Location), Chassis.Scale * Wheel.Scale));
	}
}

void AVehiclePawn::EndPlay()
{
	if (VehiclePhysics)
	{
		UStaticMeshComponent* Chassis = CastChecked<UStaticMeshComponent>(GetRootComponent());
		Chassis->ClearRenderTransformOverride();
		Chassis->SetRenderMeshOverride(nullptr);
		if (VehiclePhysics->ContactHandle)
			Chassis->OnComponentContact.Remove(VehiclePhysics->ContactHandle);
		if (VehiclePhysics->bBoostVignetteApplied)
		{
			const APlayerController* Controller = GetController();
			if (APlayerCameraManager* Camera = Controller ? Controller->GetCameraManager() : nullptr)
				Camera->VignetteIntensity = 0.0f;
		}
		for (UVehicleWheelMeshComponent* Wheel : WheelMeshes)
			Wheel->ClearRenderTransformOverride();
		FPhysicsScene& Scene = *VehiclePhysics->Scene;
		Scene.OnPreStep.Remove(VehiclePhysics->PreStepHandle);
		Scene.OnPostStep.Remove(VehiclePhysics->PostStepHandle);
		PxSceneWriteLock Lock(*Scene.GetPxScene());
		VehiclePhysics.reset();
	}

	Super::EndPlay();
}
