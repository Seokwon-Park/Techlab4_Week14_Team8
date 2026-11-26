#pragma once

// eREQUIRE_RW_LOCK 씬에서 PhysX 객체를 읽고 쓸 때 쓰는 RAII 락. PhysX 헤더를 끌어오므로 .cpp에서만 include한다.
// 여러 스레드가 같이 읽을 수 있고, 쓰기는 혼자만 한다.
// - Write 안의 Read는 같은 스레드면 허용된다. Read 안에서 Write를 잡으면 데드락이다.
// - simulate()/fetchResults()도 쓰기 호출이라 쓰기 락 안에서 부른다. 그 안에서 불리는 이벤트 콜백은 락을 다시 잡지 않는다.
// - 씬에 들어가기 전의 액터(addActor 전)는 잠글 필요가 없다. 포인터가 null이면 아무것도 잠그지 않는다.
// - 매크로는 선언문 두 개로 펼쳐지므로 중괄호 없는 if/for 본문에 단독으로 쓰지 않는다.
#include "PxPhysicsAPI.h"   // premake: Source/ThirdParty/PhysX/include
#include <PxSceneLock.h>
#include <optional>

// __LINE__을 먼저 펼친 뒤 붙여야 한 스코프에 락을 여러 개 둘 수 있다.
#define HTR_PHYSX_CONCAT_INNER(A, B) A##B
#define HTR_PHYSX_CONCAT(A, B) HTR_PHYSX_CONCAT_INNER(A, B)

#define SCOPED_SCENE_READ_LOCK(PxScenePtr) \
	std::optional<physx::PxSceneReadLock> HTR_PHYSX_CONCAT(HTR_ReadLock_, __LINE__); \
	if (physx::PxScene* HTR_PHYSX_CONCAT(HTR_ReadScene_, __LINE__) = (PxScenePtr)) \
		HTR_PHYSX_CONCAT(HTR_ReadLock_, __LINE__).emplace(*HTR_PHYSX_CONCAT(HTR_ReadScene_, __LINE__), __FILE__, __LINE__)

#define SCOPED_SCENE_WRITE_LOCK(PxScenePtr) \
	std::optional<physx::PxSceneWriteLock> HTR_PHYSX_CONCAT(HTR_WriteLock_, __LINE__); \
	if (physx::PxScene* HTR_PHYSX_CONCAT(HTR_WriteScene_, __LINE__) = (PxScenePtr)) \
		HTR_PHYSX_CONCAT(HTR_WriteLock_, __LINE__).emplace(*HTR_PHYSX_CONCAT(HTR_WriteScene_, __LINE__), __FILE__, __LINE__)
