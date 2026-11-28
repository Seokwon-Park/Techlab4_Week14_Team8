#pragma once

#include "Math/Matrix.h"
#include "Math/Frustum.h"
#include "Render/Buffer.h"

#include <d3d11.h>
#include <vector>
#include <unordered_map>

class FPrimitiveSceneProxy;
struct FViewContext;

struct FGPUOcclusionSettings
{
	bool bEnabled = true;
	bool bCull = true;               // false면 판정만 하고 그대로 그린다 (측정 도구로 잘못 가린 것이 없는지 검증할 때)
	// 가림막은 화면에 크게 보이는 것부터 고르고, 둘 중 먼저 닿는 쪽에서 멈춘다.
	//  - OccluderCoverage: 고른 가림막의 화면 넓이 합 (화면 몇 장분). 가까이 가면 몇백 개로도 금방 찬다.
	//  - MaxOccluders: 개수 상한. 멀리서 작은 물체가 많을 때 걸린다.
	float OccluderCoverage = 8.0f;
	uint32 MaxOccluders = 16384;

	// true면 GPU 판정 결과를 몇 프레임 뒤에 기다리지 않고 읽어 쓴다 (CPU가 GPU를 기다리지 않는다).
	// false면 예전처럼 같은 프레임에 결과를 기다려 읽는다 (정확하지만 CPU·GPU가 직렬로 돈다).
	bool bAsyncReadback = true;
	// 지난 결과를 쓸 수 있는 카메라 변화 한도. 넘으면 그 결과를 버리고 이번 프레임은 오클루전 없이 그린다.
	float MaxReuseCameraMove = 0.5f;          // 월드 단위
	float MinReuseCameraForwardDot = 0.998f;  // 약 3.6도
};

struct FGPUOcclusionStats
{
	uint32 Occluders = 0;            // 선행 패스에 그린 물체 수
	uint64 OccluderTriangles = 0;    // 선행 패스에 그린 삼각형 수
	float Coverage = 0.0f;           // 고른 가림막의 화면 넓이 합 (화면 몇 장분, 어림값)
	uint32 Tested = 0;               // 판정한 물체 수
	uint32 Occluded = 0;             // 가려졌다고 판정된 물체 수
	double WaitMs = 0.0;             // CPU가 GPU 판정 결과를 기다린 시간
	uint32 LatencyFrames = 0;        // 이번에 쓴 결과가 몇 번의 Run 전에 낸 판정인지 (동기 모드는 0)
	bool bUsedResult = false;        // 이번 Run에서 판정 결과를 적용했는지
	uint32 DroppedCameraMoved = 0;   // 카메라가 많이 움직여 버린 결과 수 (누적)
	uint32 SkippedNoFreeSlot = 0;    // 빈 스테이징 칸이 없어 판정을 못 낸 횟수 (누적)
};

// GPU Hi-Z 오클루전 컬링.
//  1) 프러스텀을 통과한 물체 중 화면에 크게 보이는 N개를 실제 메시로 별도 깊이 버퍼에 그린다 (가림막).
//  2) 컴퓨트로 깊이 버퍼의 밉 체인(Hi-Z)을 만든다. 밉 텍셀 = 아래 2×2 중 가장 먼 깊이.
//  3) 컴퓨트로 물체마다 AABB를 투영해 Hi-Z와 비교하고 가려짐 여부를 버퍼에 쓴다.
//  4) 그 버퍼를 스테이징 버퍼 링에 복사해 둔다.
//     비동기 모드: 기다리지 않고(DO_NOT_WAIT) 이미 끝난 지난 판정을 읽어, 지금 목록에 프록시 단위로 옮겨 적용한다.
//       지난 판정에 없던 물체, 그 뒤로 움직인 물체, 다른 뷰의 판정, 카메라가 많이 움직인 판정은 쓰지 않는다 (보이는 것으로 둔다).
//     동기 모드: 같은 프레임에 결과를 기다려 읽는다.
//  Gather는 가려진 물체를 건너뛰어 패킷·정렬·드로우콜이 모두 줄어든다.
class FGPUOcclusion
{
public:
	bool Init();

	// Proxies는 프러스텀을 통과한 물체들. 성공하면 GetOccluded()[i] = 1이면 Proxies[i]가 가려진 것이다.
	// 호출 전후로 바인딩된 렌더 타깃은 되돌려 놓는다.
	bool Run(const FPrimitiveSceneProxy* const* Proxies, uint32 Count, const FViewContext& View);

	const std::vector<uint8>& GetOccluded() const { return Occluded; }

	FGPUOcclusionSettings& GetSettings() { return Settings; }
	const FGPUOcclusionStats& GetStats() const { return Stats; }

private:
	struct FCullItem
	{
		FVector Center;
		float Padding0;
		FVector Extent;          // X < 0이면 판정하지 않고 항상 보이는 것으로 둔다
		float Padding1;
	};
	static_assert(sizeof(FCullItem) == 32);

	bool EnsureTargets(uint32 Width, uint32 Height);
	bool EnsureItemCapacity(uint32 Count);
	bool EnsureOccluderCapacity(uint32 Count);
	bool FillItems(const FPrimitiveSceneProxy* const* Proxies, uint32 Count, const FViewContext& View);
	void DrawOccluders(const FPrimitiveSceneProxy* const* Proxies, const FViewContext& View);
	void BuildHiZ();
	void Cull(const FMatrix& ViewProjection, uint32 Count);

	// 판정 하나의 CPU 읽기 칸. GPU 결과를 복사해 두고, 낸 시점의 목록·카메라를 기억한다.
	struct FReadbackSlot
	{
		enum class EState : uint8 { Free, Pending, Ready };
		EState State = EState::Free;
		ComPtr<ID3D11Buffer> Staging;
		uint32 Capacity = 0;
		uint32 Count = 0;
		uint64 IssueSerial = 0;
		FVector CameraPosition;
		FVector CameraForward;
		std::vector<const FPrimitiveSceneProxy*> Proxies;   // 낸 시점의 목록 (역참조하지 않고 주소 비교만 한다)
		// Ready가 되면 가려진 물체만 남긴다
		struct FOccludedEntry
		{
			const FPrimitiveSceneProxy* Proxy;
			int32 PackedIndex;
			FAABB Bounds;
		};
		std::vector<FOccludedEntry> OccludedEntries;
		std::vector<int32> PackedIndices;
		std::vector<FAABB> Bounds;
	};
	static constexpr uint32 ReadbackSlotCount = 4;

	bool EnsureSlotCapacity(FReadbackSlot& Slot, uint32 Count);
	FReadbackSlot* AcquireFreeSlot();
	void PollPendingSlots();
	bool FinishSlot(FReadbackSlot& Slot, bool bWait);
	bool ReadBackSync(FReadbackSlot& Slot, uint32 Count);
	bool ApplyReadyResult(const FPrimitiveSceneProxy* const* Proxies, uint32 Count, const FViewContext& View);

	FGPUOcclusionSettings Settings;
	FGPUOcclusionStats Stats;
	std::vector<uint8> Occluded;

	ComPtr<ID3D11ComputeShader> BuildHiZCS;
	ComPtr<ID3D11ComputeShader> CullCS;
	ComPtr<ID3D11Buffer> HiZParamsCB;
	ComPtr<ID3D11Buffer> CullParamsCB;
	ComPtr<ID3D11Buffer> ViewCB;                  // 가림막 패스용 VP (b0)

	// 가림막 월드 행렬. 물체마다 256바이트 칸, 오프셋 바인딩(b2)으로 쓴다.
	uint32 OccluderCapacity = 0;
	TUniquePtr<FConstantBuffer> OccluderCB;

	// 물체별 입력·출력
	uint32 ItemCapacity = 0;
	ComPtr<ID3D11Buffer> ItemBuffer;                  // 동적 StructuredBuffer<FCullItem>
	ComPtr<ID3D11ShaderResourceView> ItemSRV;
	ComPtr<ID3D11Buffer> ResultBuffer;                // [i] = 1이면 가려짐
	ComPtr<ID3D11UnorderedAccessView> ResultUAV;

	// 뷰마다 따로 두는 상태 (FViewContext::ViewStateKey 단위).
	// 에디터는 크기·카메라가 다른 뷰포트 여러 개가 렌더러 하나를 같이 쓰므로, 타깃과 판정 결과를 뷰끼리 섞지 않는다.
	struct FViewState
	{
		uint64 LastUsedSerial = 0;
		// 가림막 깊이 (화면 해상도, D32)
		uint32 TargetWidth = 0, TargetHeight = 0;
		ComPtr<ID3D11Texture2D> OccluderDepth;
		ComPtr<ID3D11DepthStencilView> OccluderDSV;
		ComPtr<ID3D11ShaderResourceView> OccluderDepthSRV;

		// Hi-Z (화면 절반 해상도부터 1×1까지)
		ComPtr<ID3D11Texture2D> HiZ;
		ComPtr<ID3D11ShaderResourceView> HiZSRV;                   // 전체 밉
		std::vector<ComPtr<ID3D11ShaderResourceView>> HiZMipSRVs;  // 밉 하나씩 (다음 밉을 만들 때 읽기)
		std::vector<ComPtr<ID3D11UnorderedAccessView>> HiZMipUAVs;
		std::vector<uint32> MipWidths, MipHeights;

		// CPU로 읽기. 칸마다 스테이징 버퍼가 따로 있어 지난 판정을 기다리지 않고 새 판정을 낼 수 있다.
		FReadbackSlot ReadbackSlots[ReadbackSlotCount];
	};
	std::unordered_map<uint64, TUniquePtr<FViewState>> ViewStates;
	FViewState* V = nullptr;   // Run 동안 이번 뷰의 상태
	void EvictUnusedViewStates();
	uint64 RunSerial = 0;
	// 지난 판정을 지금 목록에 옮길 때 쓰는 표. [PackedIndex] = 이번 세대에 가려졌다고 적힌 OccludedEntries 번호
	std::vector<uint32> StampEntry;
	std::vector<uint32> StampGeneration;
	uint32 CurrentStampGeneration = 0;

	// 가림막 고르기 (용량 재사용)
	std::vector<uint8> ScoreBuckets;         // [물체 번호] = 점수 칸 (1/4 옥타브), 0이면 가림막 후보 아님
	std::vector<uint8> OccluderLODs;         // [물체 번호], 후보인 물체만 유효
	std::vector<uint32> HistogramCounts;     // [조각 × 칸] 개수 (조각마다 따로 모아 락 없이 병렬로 센다)
	std::vector<float> HistogramScores;      // [조각 × 칸] 점수 합 → 화면 넓이 합
	uint32 HistogramChunks = 0;
	float CoverageScale = 0.0f;              // 점수 × 이것 = 화면에서 차지하는 비율
};
