#pragma once

namespace nv::cloth
{
	class Factory;
}

// 프로세스 공용 NvCloth 초기화와 종료
namespace NvClothSDK
{
	void Initialize(bool bPreferCuda = false);
	void Shutdown();

	// 월드 Solver 생성용 비소유 Factory 조회
	nv::cloth::Factory* GetFactory();

	// 현재 Solver 생성에 사용하는 Factory의 백엔드 이름
	const char* GetBackendName();
}
