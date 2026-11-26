#include "EnginePCH.h"
#include "Physics/PhysXSDK.h"
#include "Physics/PhysicsSettings.h"
#include "Core/EngineLog.h"

#include <PxFoundation.h>
#include <PxPhysics.h>
#include <PxPhysicsVersion.h>
#include <common/PxTolerancesScale.h>
#include <extensions/PxDefaultAllocator.h>
#include <extensions/PxDefaultErrorCallback.h>
#include <extensions/PxDefaultCpuDispatcher.h>
#include <extensions/PxExtensionsAPI.h>
#include <vehicle/PxVehicleSDK.h>

#if defined(ENGINE_DEBUG)
#include <pvd/PxPvd.h>
#include <pvd/PxPvdTransport.h>
#endif

namespace PhysXSDK
{
	// Foundation에 전달하는 콜백의 프로세스 수명 보장
	static physx::PxDefaultAllocator GAllocator;
	static physx::PxDefaultErrorCallback GErrorCallback;

	// 프로세스 공용 SDK 객체
	static physx::PxFoundation* GFoundation = nullptr;
	static physx::PxPhysics* GPhysics = nullptr;
	static physx::PxDefaultCpuDispatcher* GCpuDispatcher = nullptr;
	static bool bExtensionsInitialized = false;
	static bool bVehicleInitialized = false;

	// 물리 SDK에 전달할 PVD 객체
	static physx::PxPvd* GPvd = nullptr;

#if defined(ENGINE_DEBUG)
	// PVD 데이터를 전송하는 소켓 객체
	static physx::PxPvdTransport* GPvdTransport = nullptr;

	// 연결 실패와 SDK 종료에 공통인 PVD 리소스 정리
	void ReleasePvd()
	{
		if (GPvd)
		{
			GPvd->release();
			GPvd = nullptr;
		}

		if (GPvdTransport)
		{
			GPvdTransport->release();
			GPvdTransport = nullptr;
		}
	}
#endif

	bool Initialize()
	{
		if (GPhysics)
		{
			return true;
		}

		// 메모리 할당과 오류 보고를 담당하는 기반 객체 생성
		GFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, GAllocator, GErrorCallback);
		if (!GFoundation)
		{
			HTR_LOG(Error, "PxCreateFoundation failed");
			return false;
		}

		const FPhysicsSettings& Settings = FPhysicsSettings::Get();
#if defined(ENGINE_DEBUG)
		// 옵션 활성화 시 로컬 PVD 연결 시도
		if (Settings.bEnablePVD)
		{
			GPvd = physx::PxCreatePvd(*GFoundation);
			GPvdTransport = physx::PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);

			if (GPvd && GPvdTransport &&
				GPvd->connect(*GPvdTransport, physx::PxPvdInstrumentationFlag::eDEBUG))
			{
				HTR_LOG(Info, "PhysX PVD connected to 127.0.0.1:5425");
			}
			else
			{
				// 연결 실패 시 PVD 리소스 반환과 물리 초기화 계속
				ReleasePvd();
				HTR_LOG(Warning, "PhysX PVD connection failed. Continuing without PVD.");
			}
		}
#endif


		// 기반 객체와 선택적 PVD 연결을 사용하는 PhysX 인스턴스 생성
		GPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *GFoundation, physx::PxTolerancesScale(),false,GPvd);
		if (!GPhysics)
		{
			HTR_LOG(Error, "PxCreatePhysics failed");
			Shutdown();
			return false;
		}

		// 기본 Dispatcher 사용을 위한 Extensions 초기화
		bExtensionsInitialized = PxInitExtensions(*GPhysics, GPvd);
		if (!bExtensionsInitialized)
		{
			HTR_LOG(Error, "PxInitExtensions failed");
			Shutdown();
			return false;
		}

		bVehicleInitialized = physx::PxInitVehicleSDK(*GPhysics);
		if (!bVehicleInitialized)
		{
			HTR_LOG(Error, "PxInitVehicleSDK failed");
			Shutdown();
			return false;
		}
		physx::PxVehicleSetBasisVectors(
			physx::PxVec3(0.0f, 0.0f, 1.0f), physx::PxVec3(1.0f, 0.0f, 0.0f));

		// 프로젝트 워커 수에 따른 CPU 디스패처 생성. 기본 0개
		GCpuDispatcher = physx::PxDefaultCpuDispatcherCreate(Settings.CpuDispatcherWorkerCount);
		if (!GCpuDispatcher)
		{
			HTR_LOG(Error, "PxDefaultCpuDispatcherCreate failed");
			Shutdown();
			return false;
		}

		HTR_LOG(Info, "PhysX SDK initialized");
		return true;
	}

	void Shutdown()
	{
		// 의존 순서의 역순으로 SDK 객체 해제
		if (GCpuDispatcher)
		{
			GCpuDispatcher->release();
			GCpuDispatcher = nullptr;
		}

		if (bVehicleInitialized)
		{
			physx::PxCloseVehicleSDK();
			bVehicleInitialized = false;
		}

		if (bExtensionsInitialized)
		{
			PxCloseExtensions();
			bExtensionsInitialized = false;
		}

		if (GPhysics)
		{
			GPhysics->release();
			GPhysics = nullptr;
		}

#if defined(ENGINE_DEBUG)
		// Physics 해제 후 Foundation 해제 전 PVD 정리
		ReleasePvd();
#endif

		if (GFoundation)
		{
			GFoundation->release();
			GFoundation = nullptr;
			HTR_LOG(Info, "PhysX SDK released");
		}
	}

	// 씬 생성에 필요한 공용 객체 반환
	physx::PxPhysics* GetPhysics()
	{
		return GPhysics;
	}

	physx::PxCpuDispatcher* GetCpuDispatcher()
	{
		return GCpuDispatcher;
	}
}
