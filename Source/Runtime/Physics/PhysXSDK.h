#pragma once

namespace physx
{
	class PxPhysics;
	class PxCpuDispatcher;
}

// 프로세스 공통 PhysXSDk
namespace PhysXSDK
{
	bool Initialize();
	void Shutdown();
	physx::PxPhysics* GetPhysics();
	physx::PxCpuDispatcher* GetCpuDispatcher();
}