#include "EnginePCH.h"
#include "AI/Navigation/NavigationTypes.h"

// 1일차 링크용 빈 스텁: 구조체 기본값을 그대로 돌려준다.
// TODO(D): Config/DefaultGame.ini [Navigation]에서 읽도록 구현

const FNavAgentProperties& GetDefaultNavAgent()
{
	static const FNavAgentProperties Default;
	return Default;
}

const FNavMeshResolution& GetDefaultNavResolution()
{
	static const FNavMeshResolution Default;
	return Default;
}
