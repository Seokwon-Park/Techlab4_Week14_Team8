#include "EnginePCH.h"
#include "Clothing/ClothCore.h"
#include "Physics/NvClothSDK.h"

bool FClothCore::Init(bool bPreferCuda)
{
	if (!NvClothSDK::GetFactory())
	{
		NvClothSDK::Initialize(bPreferCuda);
	}
	return NvClothSDK::GetFactory() != nullptr;
}

void FClothCore::Shutdown()
{
	NvClothSDK::Shutdown();
}

bool FClothCore::IsInitialized() const
{
	return NvClothSDK::GetFactory() != nullptr;
}

nv::cloth::Factory* FClothCore::GetFactory() const
{
	return NvClothSDK::GetFactory();
}

const char* FClothCore::GetBackendName() const
{
	return NvClothSDK::GetBackendName();
}
