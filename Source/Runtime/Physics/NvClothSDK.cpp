#include "EnginePCH.h"
#include "Physics/NvClothSDK.h"

#include "Physics/PhysXSDK.h"

#include <PxFoundation.h>
#include <PxPhysics.h>
#include <foundation/PxErrorCallback.h>
#include <NvCloth/Callbacks.h>
#include <NvCloth/Factory.h>
#include <cstdlib>

namespace NvClothSDK
{
	class FNvClothAssertHandler final : public nv::cloth::PxAssertHandler
	{
	public:
		// NvCloth assertion의 기록과 프로세스 중단
		void operator()(const char* Expression, const char* File, int Line, bool&) override
		{
			HTR_LOG(Error, "NvCloth assertion failed: {} ({}:{})", Expression, File, Line);
			std::abort();
		}
	};

	// 반환 포인터로 드러나지 않는 CUDA Factory 내부 오류의 기록
	class FNvClothErrorCallback final : public physx::PxErrorCallback
	{
	public:
		physx::PxErrorCallback* ForwardTo = nullptr;
		inline static thread_local bool bInternalError = false;

		void reportError(physx::PxErrorCode::Enum Code, const char* Message, const char* File, int Line) override
		{
			if (Code == physx::PxErrorCode::eINTERNAL_ERROR)
			{
				bInternalError = true;
			}
			ForwardTo->reportError(Code, Message, File, Line);
		}
	};

	// GPU Factory보다 오래 유지하는 Windows 드라이버와 기본 컨텍스트
	class FCudaDriverContext final
	{
	public:
		~FCudaDriverContext()
		{
			if (Context && ReleasePrimaryContext(Device) != 0)
			{
				HTR_LOG(Warning, "NvCloth CUDA context release failed");
			}
			if (Driver)
			{
				FreeLibrary(Driver);
			}
		}

		const char* Open()
		{
			Driver = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			if (!Driver)
			{
				return "nvcuda.dll is unavailable";
			}

			// CUDA Toolkit 의존성 없이 Windows 드라이버 API 로딩
			const auto Init = reinterpret_cast<int(WINAPI*)(unsigned int)>(GetProcAddress(Driver, "cuInit"));
			const auto GetDeviceCount = reinterpret_cast<int(WINAPI*)(int*)>(GetProcAddress(Driver, "cuDeviceGetCount"));
			const auto GetDevice = reinterpret_cast<int(WINAPI*)(int*, int)>(GetProcAddress(Driver, "cuDeviceGet"));
			const auto Retain = reinterpret_cast<int(WINAPI*)(CUcontext*, int)>(GetProcAddress(Driver, "cuDevicePrimaryCtxRetain"));
			ReleasePrimaryContext = reinterpret_cast<decltype(ReleasePrimaryContext)>(GetProcAddress(Driver, "cuDevicePrimaryCtxRelease_v2"));
			PushCurrent = reinterpret_cast<decltype(PushCurrent)>(GetProcAddress(Driver, "cuCtxPushCurrent_v2"));
			PopCurrent = reinterpret_cast<decltype(PopCurrent)>(GetProcAddress(Driver, "cuCtxPopCurrent_v2"));
			if (!Init || !GetDeviceCount || !GetDevice || !Retain || !ReleasePrimaryContext || !PushCurrent || !PopCurrent)
			{
				return "required CUDA driver functions are unavailable";
			}
			if (Init(0) != 0)
			{
				return "cuInit failed";
			}

			int DeviceCount = 0;
			if (GetDeviceCount(&DeviceCount) != 0 || DeviceCount == 0)
			{
				return "no CUDA device is available";
			}

			// 첫 번째로 확보 가능한 CUDA 장치의 기본 컨텍스트 유지
			for (int Ordinal = 0; Ordinal < DeviceCount; ++Ordinal)
			{
				int Candidate = 0;
				CUcontext CandidateContext = nullptr;
				if (GetDevice(&Candidate, Ordinal) == 0 && Retain(&CandidateContext, Candidate) == 0)
				{
					Context = CandidateContext;
					Device = Candidate;
					return nullptr;
				}
			}
			return "could not retain a CUDA device context";
		}

		bool Push() const { return PushCurrent(Context) == 0; }
		bool Pop() const
		{
			CUcontext Popped = nullptr;
			return PopCurrent(&Popped) == 0 && Popped == Context;
		}

		CUcontext GetContext() const { return Context; }

	private:
		HMODULE Driver = nullptr;
		CUcontext Context = nullptr;
		int Device = 0;
		int(WINAPI* ReleasePrimaryContext)(int) = nullptr;
		int(WINAPI* PushCurrent)(CUcontext) = nullptr;
		int(WINAPI* PopCurrent)(CUcontext*) = nullptr;
	};

	// 프로세스 공용 콜백과 선택한 백엔드의 Factory
	static FNvClothAssertHandler GAssertHandler;
	static FNvClothErrorCallback GErrorCallback;
	static TUniquePtr<FCudaDriverContext> GCudaContext;
	static nv::cloth::Factory* GFactory = nullptr;

	// GPU Factory 생성과 호출 스레드의 이전 컨텍스트 복원
	const char* TryCreateCudaFactory()
	{
		if (!NvClothCompiledWithCudaSupport())
		{
			return "the NvCloth DLL has no CUDA backend";
		}
		auto Context = MakeUnique<FCudaDriverContext>();
		if (const char* Failure = Context->Open())
		{
			return Failure;
		}
		if (!Context->Push())
		{
			return "could not make the CUDA context current";
		}

		// 실제 커널 로딩 결과를 포함하는 SDK 오류의 검사
		FNvClothErrorCallback::bInternalError = false;
		nv::cloth::Factory* Factory = NvClothCreateFactoryCUDA(Context->GetContext());
		if (Factory && FNvClothErrorCallback::bInternalError)
		{
			NvClothDestroyFactory(Factory);
			Factory = nullptr;
		}
		if (!Context->Pop())
		{
			if (Factory)
			{
				NvClothDestroyFactory(Factory);
			}
			return "could not restore the previous CUDA context";
		}
		if (!Factory)
		{
			return "CUDA Factory or kernel initialization failed";
		}

		GFactory = Factory;
		GCudaContext = std::move(Context);
		HTR_LOG(Info, "NvCloth CUDA Factory initialized");
		return nullptr;
	}

	// PhysX 콜백 등록과 시작 옵션에 따른 백엔드 선택
	void Initialize(bool bPreferCuda)
	{
		if (GFactory)
		{
			return;
		}

		physx::PxFoundation& Foundation = PhysXSDK::GetPhysics()->getFoundation();
		GErrorCallback.ForwardTo = &Foundation.getErrorCallback();
		nv::cloth::InitializeNvCloth(&Foundation.getAllocatorCallback(),
			&GErrorCallback, &GAssertHandler, nullptr);

		if (bPreferCuda)
		{
			if (const char* Failure = TryCreateCudaFactory())
			{
				HTR_LOG(Warning, "NvCloth CUDA initialization failed: {}. Falling back to CPU.", Failure);
			}
			else
			{
				return;
			}
		}

		GFactory = NvClothCreateFactoryCPU();
		if (!GFactory)
		{
			HTR_LOG(Warning, "NvCloth CPU Factory creation failed");
			return;
		}

		HTR_LOG(Info, "NvCloth CPU Factory initialized");
	}
	// 선택한 Factory 해제 후 CUDA 컨텍스트와 드라이버 반환
	void Shutdown()
	{
		if (!GFactory)
		{
			return;
		}

		const bool bWasCuda = GCudaContext != nullptr;
		const bool bPushed = bWasCuda && GCudaContext->Push();
		if (bWasCuda && !bPushed)
		{
			HTR_LOG(Warning, "NvCloth CUDA context activation failed during shutdown");
		}
		NvClothDestroyFactory(GFactory);
		GFactory = nullptr;
		if (bPushed && !GCudaContext->Pop())
		{
			HTR_LOG(Warning, "NvCloth CUDA context restoration failed during shutdown");
		}
		GCudaContext.reset();
		HTR_LOG(Info, "NvCloth {} Factory released", bWasCuda ? "CUDA" : "CPU");
	}

	// 월드 Solver 생성용 비소유 Factory 반환
	nv::cloth::Factory* GetFactory()
	{
		return GFactory;
	}

	const char* GetBackendName()
	{
		if (!GFactory)
		{
			return "Unavailable";
		}

		// CUDA 초기화 실패 시 CPU로 전환된 결과까지 반영
		return GFactory->getPlatform() == nv::cloth::Platform::CUDA ? "CUDA" : "CPU";
	}
}
