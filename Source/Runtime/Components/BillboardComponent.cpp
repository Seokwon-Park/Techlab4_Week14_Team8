#include "EnginePCH.h"
#include "BillboardComponent.h"

#include "Asset/AssetManager.h"
#include "Serialization/TypeSerializer.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Render/RenderPacket.h"

namespace
{
	// 판정 면은 그려지는 면과 같은 카메라를 봐야 한다. 게임 월드는 게임 뷰포트가 실제로 그리고
	// 마우스 Ray(ScreenToWorldRay)를 만드는 카메라(PlayerCamera)를, 그 밖의 월드는 MainCamera를 쓴다.
	UCameraComponent* FindTraceCamera(const UWorld* World)
	{
		if (!World) return nullptr;
		if (GEngine)
		{
			FWorldContext* Context = GEngine->GetWorldContextFromWorld(World);
			if (Context && Context->GameViewport && Context->GameViewport->GetCameraComponent())
				return Context->GameViewport->GetCameraComponent();
		}
		ACameraActor* MainCamera = World->GetMainCamera();
		return MainCamera ? MainCamera->GetCameraComponent() : nullptr;
	}
}

// Billboard 컴포넌트의 초기 상태를 구성한다.
UBillboardComponent::UBillboardComponent()
{
	QuadMesh = UAssetManager::GetAssetByPath<UStaticMesh>("ParticleQuad");
	Material = UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");
}

// Billboard 컴포넌트의 소멸을 처리한다.
UBillboardComponent::~UBillboardComponent()
{
}

// 기본 Mesh와 Material을 에셋 관리자에서 가져온다.
void UBillboardComponent::BeginPlay()
{
	Super::BeginPlay();

}

// 부모 컴포넌트의 프레임 갱신을 호출한다.
void UBillboardComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
}

bool UBillboardComponent::LineTraceComponent(const FRay& WorldRay, FHitResult& OutHit)
{
	if (!QuadMesh) return false;

	FMatrix BillboardMatrix;
	GetWorldTransformedMatrix(&BillboardMatrix);   // 카메라를 향하는, 실제로 그려지는 행 렬
	return TraceMesh(WorldRay, QuadMesh->GetMeshData(), BillboardMatrix, OutHit);
}

// View별 렌더 행렬을 그대로 사용해 메인 카메라와 다른 방향에서도 같은 면을 선택한다.
bool UBillboardComponent::LineTraceComponentForView(
	const FRay& WorldRay, FHitResult& OutHit, const FMatrix& BillboardWorldMatrix)
{
	return QuadMesh && TraceMesh(WorldRay, QuadMesh->GetMeshData(), BillboardWorldMatrix, OutHit);
}

FBox UBillboardComponent::CalcLocalBounds() const
{
	// 카메라 방향에 따라 회전하므로 얇은 쿼드 AABB로는 컬링할 수 없다.
	const float Extent = bEditorIcon ? std::max(0.5f, IconSize) : 0.5f;
	return FBox{ FVector(-Extent, -Extent, -Extent), FVector(Extent, Extent, Extent) };
}

void UBillboardComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FViewContext& ViewContext)
{
	if (QuadMesh == nullptr || Material == nullptr)
	{
		return;
	}

	const FVector WorldPos = GetWorldLocation();
	float Width = 0.0f, Height = 0.0f;
	GetIconExtent(WorldPos, ViewContext.CameraPosition, ViewContext.CameraForward,
		ViewContext.bOrthographic, ViewContext.ProjectionScaleY, Width, Height);
	const FMatrix BillboardWorldMatrix = ViewContext.BuildBillboardMatrix(WorldPos, Width, Height);
	const uint32 PacketIndex = RenderQueue.Num();
	SubmitToRenderQueue(RenderQueue, BillboardWorldMatrix);
	if (static_cast<uint32>(RenderQueue.Num()) > PacketIndex)
	{
		const FVector Delta = WorldPos - ViewContext.CameraPosition;
		RenderQueue[PacketIndex].CameraToParticleDistance = Delta.Dot(Delta);
	}
}

// View별 Billboard 행렬과 Material을 렌더 패킷에 담는다.
void UBillboardComponent::SubmitToRenderQueue(FRenderQueue& RenderQueue, const FMatrix& BillboardWorldMatrix)
{
	if (QuadMesh == nullptr || Material == nullptr)
		return;

	// Light Component light 가져와서 IconParams로 전달
	IconParams.Tint = TintSource ? TintSource->GetLightColor().ToVector4() : FVector4(1, 1, 1, 1);
	// 현재 반투명 패스는 톤 매핑 이후 SceneColor(LDR)에 그려진다.
	IconParams.DecodeSRGB = 0.0f;
	FRenderPacket Packet;
	Packet.Mesh = QuadMesh;
	Packet.Material = Material;
	Packet.Model = RenderQueue.StoreWorldMatrix(BillboardWorldMatrix);
	Packet.MaterialParamData = &IconParams;
	Packet.MaterialParamDataSize = sizeof(FSubUVConstants);
	RenderQueue.Add(Packet);
}

// 판정용 Billboard 행렬. 렌더(SubmitToRenderQueue)와 같은 FViewContext::BuildBillboardMatrix로 만들어
// 화면에 보이는 면과 Raycast에 맞는 면이 같도록 한다.
void UBillboardComponent::GetWorldTransformedMatrix(FMatrix* OutWorldMatrix) const
{
	const FVector WorldPos = GetWorldLocation();

	FViewContext View;
	if (UCameraComponent* Camera = FindTraceCamera(GetOwner() ? GetOwner()->GetWorld() : nullptr))
	{
		View.CameraPosition = Camera->GetWorldLocation();
		View.CameraForward = Camera->GetWorldRotation().Quaternion().GetForwardVector();
		View.bOrthographic = Camera->GetIsOrthogonal();
		// 렌더(FSceneView::ToViewContext)와 같은 성분을 써서 클릭 면이 그려진 크기와 같게 한다
		View.ProjectionScaleY = std::abs(Camera->GetProjectionMatrix().M[2][1]);
	}
	else
	{
		View.CameraPosition = WorldPos - FVector(1.0f, 0.0f, 0.0f); // 카메라가 없으면 -X 쪽에서 보는 것으로 둔다
	}
	float Width = 0.0f, Height = 0.0f;
	GetIconExtent(WorldPos, View.CameraPosition, View.CameraForward, View.bOrthographic, View.ProjectionScaleY, Width, Height);
	*OutWorldMatrix = View.BuildBillboardMatrix(WorldPos, Width, Height);
}

// UE FSpriteSceneProxy와 같은 방식: 아이콘이 화면 세로의 ScreenSize 비율보다 크게 보일 거리부터는
// 그 비율에 맞게 줄인다. 그래서 멀리서는 월드 크기로 작아지고, 가까이 가도 화면을 덮지 않는다.
void UBillboardComponent::GetIconExtent(const FVector& WorldPos, const FVector& CameraPos, const FVector& CameraForward,
	bool bOrthographic, float ProjectionScaleY, float& OutWidth, float& OutHeight) const
{
	const FVector WorldScale = GetWorldScale3D();
	OutWidth = bEditorIcon ? IconSize : WorldScale.Y;
	OutHeight = bEditorIcon ? IconSize : WorldScale.Z;
	if (!bEditorIcon || !bIsScreenSizeScaled || ScreenSize <= 0.0f || ProjectionScaleY <= 0.0f)
		return;

	// 화면 세로에서 차지하는 비율 = 높이 * 세로배율 / (2 * 깊이)  (직교는 깊이 1)
	float Depth = 1.0f;
	if (!bOrthographic)
	{
		const FVector Forward = CameraForward.Length() > 1e-6f ? CameraForward.Normalized() : (WorldPos - CameraPos).Normalized();
		Depth = (WorldPos - CameraPos).Dot(Forward);
		if (Depth <= 1e-3f)
			return;   // 카메라 뒤나 바로 앞: 어차피 잘린다
	}
	const float MaxHeight = 2.0f * ScreenSize * Depth / ProjectionScaleY;
	const float Largest = std::max(OutWidth, OutHeight);
	if (Largest > MaxHeight && Largest > 0.0f)
	{
		const float Scale = MaxHeight / Largest;
		OutWidth *= Scale;
		OutHeight *= Scale;
	}
}

void UBillboardComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	UMaterial* Loaded = Material;
	UMaterial::SerializeMaterialReference(Record.EnterField("Material"), Loaded);

	// 불러오기에 실패하면(null) 생성자가 넣어 둔 기본 머티리얼을 유지한다
	if (Record.GetUnderlyingArchive().IsLoading() && Loaded)
		Material = Loaded;
}
