#pragma once

#include "Core/Types.h"

namespace nv::cloth
{
	class Factory;
}

// Runtime/Clothing 모듈의 NvCloth Factory 진입점 (기존 NvClothSDK 수명을 위임 관리)
class FClothCore
{
public:
	static FClothCore& Get()
	{
		static FClothCore Instance;
		return Instance;
	}

	bool Init(bool bPreferCuda = false);
	void Shutdown();

	bool IsInitialized() const;
	nv::cloth::Factory* GetFactory() const;
	const char* GetBackendName() const;

	FClothCore(const FClothCore&) = delete;
	FClothCore& operator=(const FClothCore&) = delete;

private:
	FClothCore() = default;
	~FClothCore() = default;
};
