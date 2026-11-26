#pragma once

#include "Components/PrimitiveComponent.h"

class FOutlineRenderer;
struct FSceneView;
struct FViewportSettings;

class FOutline
{
public:
	FOutline();
	~FOutline();

	// 대상 메시의 외곽선을 View의 카메라 기준으로 그린다. 씬 위에 올리는 패스 안에서 부른다.
	// FGizmo::Render와 같은 방식이다. UE는 선택 외곽선을 렌더러 내부에서 그리므로 대응하는 함수가 없다.
	void Render(const FSceneView* View, const FViewportSettings& Viewport) const;

	// 외곽선을 그릴 메시. 여러 액터를 고르면 그 메시를 모두 넣는다 (SetTargets).
	void SetTarget(UPrimitiveComponent* InTarget)
	{
		Targets.Empty();
		if (InTarget)
			Targets.Add(InTarget);
		Target = InTarget;
	}
	void SetTargets(const TArray<UPrimitiveComponent*>& InTargets)
	{
		Targets = InTargets;
		Target = Targets.IsEmpty() ? nullptr : Targets[0];
	}
	// 그리는 동안에는 지금 그리는 메시, 평소에는 첫 번째 대상이다.
	UPrimitiveComponent* GetTarget() const { return Target; }
	UStaticMesh* GetMesh() const { return Target ? Target->GetRenderMesh() : nullptr; }
	FMatrix GetWorldMatrix() const { return Target->GetWorldMatrix(); }
	const FVector& GetTargetScale() const { return Target->GetRelativeScale3D(); }
private:
	TArray<UPrimitiveComponent*> Targets;
	// FOutlineRenderer가 이 대상 하나를 읽어 그린다. Render가 대상마다 바꿔 가며 부른다.
	mutable UPrimitiveComponent* Target = nullptr;

	// 스텐실과 셰이더로 실제 그리기를 맡는 도구
	TUniquePtr<FOutlineRenderer> Renderer;

};