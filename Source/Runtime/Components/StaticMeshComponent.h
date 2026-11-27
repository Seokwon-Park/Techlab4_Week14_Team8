#pragma once

#include "MeshComponent.h"
#include "StaticMeshComponent.generated.h"

UCLASS()
class UStaticMeshComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	UStaticMeshComponent();
	virtual ~UStaticMeshComponent();

	virtual void BeginPlay();
	virtual void TickComponent(float DeltaTime);

	virtual const FStaticMeshData* GetMeshData() const override { return StaticMesh ? &StaticMesh->GetMeshData() : nullptr; }

	// 메시가 바뀌면 슬롯 구성이 달라지므로 덮어쓰기를 비운다
	void SetStaticMesh(UStaticMesh* InStaticMesh);
	UStaticMesh* GetStaticMesh() const { return StaticMesh; }
	virtual UStaticMesh* GetRenderMesh() const override { return RenderMeshOverride ? RenderMeshOverride : StaticMesh; }
	void SetRenderMeshOverride(UStaticMesh* InMesh);
	const FTransform* GetRenderTransformOverride() const { return RenderTransformOverride.get(); }
	void SetRenderTransformOverride(const FTransform& WorldTransform);
	void ClearRenderTransformOverride();

	virtual int32 GetNumMaterials() const override;
	virtual FString GetMaterialSlotName(int32 SlotIndex) const override;
	virtual UMaterial* GetDefaultMaterial(int32 SlotIndex) const override;

	void SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext) override;
	void SubmitToRenderQueue(FRenderQueue& Queue, uint32 LODIndex);

	bool IsEditorIcon() const { return bEditorIcon; }
	void SetEditorIcon(bool IsEditorIcon) { bEditorIcon = IsEditorIcon; }

	// 라이트 방향 화살표 같은 에디터 아이콘 메시를 빌보드 아이콘과 같이 화면 크기 기준으로 줄인다
	// (UE UBillboardComponent의 bIsScreenSizeScaled / ScreenSize 방식). RefSize는 같이 줄어들 아이콘의 월드 길이.
	void SetScreenSizeScaled(bool bEnable, float InScreenSize = 0.08f, float InRefSize = 2.0f)
	{
		bIsScreenSizeScaled = bEnable; ScreenSize = InScreenSize; ScreenSizeRefSize = InRefSize;
	}
	bool IsScreenSizeScaledIcon() const { return bEditorIcon && bIsScreenSizeScaled; }
	// 뷰마다 줄인 월드 행렬로 패킷을 낸다 (프록시의 고정 행렬을 쓰지 않으므로 Proxy는 비운다).
	void SubmitScreenSizedIcon(FRenderQueue& RenderQueue, const FViewContext& ViewContext);

	UBodySetup* GetBodySetup() override { return StaticMesh ? StaticMesh->GetBodySetup() : nullptr; }

private:
	UStaticMesh* RenderMeshOverride = nullptr;
	TUniquePtr<FTransform> RenderTransformOverride;
	UPROPERTY(EditAnywhere)
	UStaticMesh* StaticMesh = nullptr;
	// Mesh의 Section별 Material과 Texture를 보존해 각각의 렌더 패킷으로 제출한다.
	//virtual void SubmitToRenderQueue(TQueue<FRenderPacket>& RenderQueue) override;

	// 조명 Icon Debug arrow용
	bool bEditorIcon = false;
	bool bIsScreenSizeScaled = true;   // 에디터 아이콘일 때만 쓴다
	float ScreenSize = 0.08f;          // 화면 세로 대비 최대 비율 (빌보드 아이콘과 같은 값)
	float ScreenSizeRefSize = 2.0f;    // 라이트 아이콘 IconSize와 같아서 아이콘과 같은 비율로 줄어든다
};
