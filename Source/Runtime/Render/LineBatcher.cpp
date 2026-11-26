#include "EnginePCH.h"
#include "LineBatcher.h"
#include "Components/PrimitiveComponent.h"

#include "RenderCommand.h"
#include "RenderResourceManager.h"

#include "UObject/UObjectIterator.h"

FLineBatcher::~FLineBatcher()
{
	delete[] VertexBufferBase;
	delete[] TriangleVertexBase;
	delete[] SolidVertexBase;
}

bool FLineBatcher::Init(FRenderer* InRenderer, UWorld* InWorld)
{
	Renderer = InRenderer;
	World = InWorld;

	LineShader = FRenderResourceManager::GetShaderProgram("Resources/Shader/LineShader.hlsl");

	PipelineState.Shader = LineShader;
	PipelineState.Topology = EPrimitiveTopology::LineList;

	MaxVertices = 65536;
	VertexBufferBase = new FLineBatchVertex[MaxVertices];

	VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(sizeof(FLineBatchVertex) * MaxVertices, sizeof(FLineBatchVertex));

	TrianglePipelineState.Shader = LineShader;
	TrianglePipelineState.Topology = EPrimitiveTopology::TriangleList;
	TrianglePipelineState.RasterizerState = ERasterizerState::SolidNone;
	TrianglePipelineState.BlendState = EBlendState::AlphaBlend;
	TrianglePipelineState.DepthStencilState = EDepthStencilState::ReadOnly;

	MaxTriangleVertices = 65536 * 3;
	TriangleVertexBase = new FLineBatchVertex[MaxTriangleVertices];
	TriangleVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(sizeof(FLineBatchVertex) * MaxTriangleVertices, sizeof(FLineBatchVertex));

	SolidPipelineState = TrianglePipelineState;
	SolidPipelineState.RasterizerState = ERasterizerState::SolidBack;
	SolidVertexBase = new FLineBatchVertex[MaxTriangleVertices];
	SolidVertexBuffer = RenderCommand::CreateDynamicVertexBuffer(sizeof(FLineBatchVertex) * MaxTriangleVertices, sizeof(FLineBatchVertex));

	CB = RenderCommand::CreateConstantBuffer(sizeof(FMatrix));

	return true;
}

void FLineBatcher::BuildVertexBuffer()
{
	for (TObjectIterator<UPrimitiveComponent> Itr; Itr; ++Itr)
	{
		if (Itr->GetWorld() == World)
		{
			// 로컬 AABB와 월드 행렬을 그대로 넘기고 변환은 AddOrientedBox가 한다.
			// (월드 행렬이어야 부모에 붙은 컴포넌트도 제자리에 그려진다)
			AddOrientedBox(
				Itr->CalcLocalBounds(),
				Itr->GetWorldMatrix(),
				FVector4(1.0f, 1.0f, 1.0f, 1.0f));
		}
	}
}

void FLineBatcher::BeginFrame()
{
	VertexBufferPtr = VertexBufferBase;
	VertexCount = 0;
	TriangleVertexCount = 0;
	SolidVertexCount = 0;
}

void FLineBatcher::AddLine(const FVector& A, const FVector& B, const FVector4& Color)
{
	if (VertexCount + 2 > MaxVertices)
		return;   // 넘치면 버림

	*VertexBufferPtr++ = { A, Color };
	*VertexBufferPtr++ = { B, Color };

	VertexCount += 2;
}

void FLineBatcher::AddTriangle(const FVector& A, const FVector& B, const FVector& C, const FVector4& Color)
{
	if (TriangleVertexCount + 3 > MaxTriangleVertices)
		return;

	TriangleVertexBase[TriangleVertexCount++] = { A, Color };
	TriangleVertexBase[TriangleVertexCount++] = { B, Color };
	TriangleVertexBase[TriangleVertexCount++] = { C, Color };
}

void FLineBatcher::AddSolidTriangle(const FVector& A, const FVector& B, const FVector& C, const FVector4& Color, const FVector& Outward)
{
	if (SolidVertexCount + 3 > MaxTriangleVertices)
		return;

	// 이 엔진은 Cross(B - A, C - A)가 보는 쪽을 향하는 면이 앞면이다 (GeometryGenerator와 같은 감김)
	const bool bFlip = (B - A).Cross(C - A).Dot(Outward) < 0.0f;
	SolidVertexBase[SolidVertexCount++] = { A, Color };
	SolidVertexBase[SolidVertexCount++] = { bFlip ? C : B, Color };
	SolidVertexBase[SolidVertexCount++] = { bFlip ? B : C, Color };
}

namespace
{
	// 박스 모서리 12개. 코너 배열의 인덱스 쌍이며 AABB/OBB가 같은 순서를 쓴다.
	const int BOX_EDGES[12][2] = {
		{0,1},{1,2},{2,3},{3,0},      // 아랫면
		{4,5},{5,6},{6,7},{7,4},      // 윗면
		{0,4},{1,5},{2,6},{3,7},      // 기둥
	};

	// Min/Max로 코너 8개를 만든다. BOX_EDGES가 기대하는 순서다.
	void MakeCorners(const FVector& Min, const FVector& Max, FVector* OutCorners)
	{
		OutCorners[0] = { Min.X, Min.Y, Min.Z };
		OutCorners[1] = { Max.X, Min.Y, Min.Z };
		OutCorners[2] = { Max.X, Max.Y, Min.Z };
		OutCorners[3] = { Min.X, Max.Y, Min.Z };
		OutCorners[4] = { Min.X, Min.Y, Max.Z };
		OutCorners[5] = { Max.X, Min.Y, Max.Z };
		OutCorners[6] = { Max.X, Max.Y, Max.Z };
		OutCorners[7] = { Min.X, Max.Y, Max.Z };
	}
}

void FLineBatcher::AddBox(FBox BoundingBox, FVector4 Color)
{
	FVector C[8];
	MakeCorners(BoundingBox.Min, BoundingBox.Max, C);

	for (const auto& Edge : BOX_EDGES)
	{
		AddLine(C[Edge[0]], C[Edge[1]], Color);
	}
}

void FLineBatcher::AddOrientedBox(const FBox& LocalBox, const FMatrix& WorldMatrix, const FVector4& Color)
{
	FVector C[8];
	MakeCorners(LocalBox.Min, LocalBox.Max, C);

	// 변환 후에 Min/Max를 다시 재면 회전이 뭉개지므로,
	// 코너를 하나씩 옮긴 뒤 원래 순서 그대로 잇는다.
	for (FVector& Corner : C)
	{
		const FVector4 WorldCorner = FVector4(Corner, 1.0f) * WorldMatrix;
		Corner = FVector(WorldCorner.X, WorldCorner.Y, WorldCorner.Z);
	}

	for (const auto& Edge : BOX_EDGES)
	{
		AddLine(C[Edge[0]], C[Edge[1]], Color);
	}

}
void FLineBatcher::AddPath(const TArray<FVector>& Points, const FVector4& Color)
{
	if (Points.IsEmpty())
	{
		return;
	}
	for (size_t i = 0; i < Points.size() - 1; ++i) {
		AddLine(Points[i], Points[i + 1], Color);
	}
}

void FLineBatcher::AddPyramid(const FVector& BaseCenter, const FVector& Tip, float HalfWidth, const FVector4& Color)
{
	const FVector Offset = Tip - BaseCenter;
	if (Offset.SizeSquared() < 0.00000001f || HalfWidth <= 0.0f)
	{
		return;
	}

	// 축이 수직인 경우에도 외적이 영벡터가 되지 않도록 기준 축을 고릅니다.
	const FVector Direction = Offset.Normalized();
	const FVector Reference = fabsf(Direction.Z) < 0.99f ? FVector(0.0f, 0.0f, 1.0f) : FVector(0.0f, 1.0f, 0.0f);
	const FVector Right = Direction.Cross(Reference).Normalized() * HalfWidth;
	const FVector Up = Direction.Cross(Right).Normalized() * HalfWidth;
	const FVector Corners[] = {
		BaseCenter + Right + Up, BaseCenter - Right + Up,
		BaseCenter - Right - Up, BaseCenter + Right - Up
	};

	for (int32 Index = 0; Index < 4; ++Index)
	{
		AddLine(Corners[Index], Corners[(Index + 1) % 4], Color);
		AddLine(Corners[Index], Tip, Color);
	}
}

void FLineBatcher::AddSphere(const FVector& Center, float Radius, const FVector4& Color, int32 Segments)
{
	if (Radius <= 0.0f || Segments < 3)
	{
		return;
	}

	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float AngleA = 2.0f * PI * static_cast<float>(Index) / Segments;
		const float AngleB = 2.0f * PI * static_cast<float>(Index + 1) / Segments;
		const float CosA = cosf(AngleA) * Radius;
		const float SinA = sinf(AngleA) * Radius;
		const float CosB = cosf(AngleB) * Radius;
		const float SinB = sinf(AngleB) * Radius;

		AddLine(Center + FVector(CosA, SinA, 0.0f), Center + FVector(CosB, SinB, 0.0f), Color);
		AddLine(Center + FVector(CosA, 0.0f, SinA), Center + FVector(CosB, 0.0f, SinB), Color);
		AddLine(Center + FVector(0.0f, CosA, SinA), Center + FVector(0.0f, CosB, SinB), Color);
	}
}

void FLineBatcher::AddCapsule(const FVector& Center, const FVector& Up, const FVector& Right,
	float Radius, float HalfHeight, const FVector4& Color, int32 Segments)
{
	constexpr float Pi = PI;

	const float Segment = std::max(HalfHeight - Radius, 0.0f);
	const FVector CapsuleA = Center + Up * Segment;
	const FVector CapsuleB = Center - Up * Segment;
	const FVector Y = Up.Cross(Right).GetSafeNormal();
	

	// 위·아래 고리
	AddWireArc(CapsuleA, Right, Y, Radius, 0, 2 * Pi, Color, Segments);
	AddWireArc(CapsuleB, Right, Y, Radius, 0, 2 * Pi, Color, Segments);
	// 위 반구 , 아래 반구
	AddWireArc(CapsuleA, Right, Up, Radius, 0, Pi, Color, Segments);
	AddWireArc(CapsuleA, Y, Up, Radius, 0, Pi, Color, Segments);
	AddWireArc(CapsuleB, Right, Up, Radius, 0, -Pi, Color, Segments);
	AddWireArc(CapsuleB, Y, Up, Radius, 0, -Pi, Color, Segments);
	// 옆선 4개
	for (const FVector& Side : { Right, Right * -1.0f, Y, Y * -1.0f })
		AddLine(CapsuleA + Side * Radius, CapsuleB + Side * Radius, Color);
}

void FLineBatcher::AddSwingCone(const FVector& Apex, const FVector& Axis, const FVector& Right,
	float Swing1Radian, float Swing2Radian, float Length, const FVector4& Color, int32 Segments, float FillAlpha)
{
	if (Length <= 0.0f || Segments < 3)
	{
		return;
	}

	const FVector Up2 = Axis.Cross(Right).GetSafeNormal();

	// 두 각이 모두 0이면 축 방향 선 하나만 그린다.
	if (Swing1Radian <= 0.0f && Swing2Radian <= 0.0f)
	{
		AddLine(Apex, Apex + Axis * Length, Color);
		return;
	}

	// 고리 위의 점: Axis를 W 방향으로 |W|만큼 기울인 끝점
	auto PointAt = [&](float T)
	{
		const FVector W = Right * (Swing1Radian * std::cos(T)) + Up2 * (Swing2Radian * std::sin(T));
		const float Theta = W.Length();
		if (Theta < 1e-6f)
		{
			return Apex + Axis * Length;   // 한쪽 각이 0일 때 그 방향은 축 그대로
		}
		const FVector Dir = Axis * std::cos(Theta) + (W / Theta) * std::sin(Theta);
		return Apex + Dir * Length;
	};

	// 옆선은 4개 (Right, Up2, -Right, -Up2 방향)
	const int32 SideStep = std::max(Segments / 4, 1);
	const FVector4 FillColor(Color.X, Color.Y, Color.Z, FillAlpha);

	FVector Prev = PointAt(0.0f);
	AddLine(Apex, Prev, Color);
	for (int32 i = 1; i <= Segments; ++i)
	{
		const FVector P = PointAt(2.0f * PI * i / Segments);
		AddLine(Prev, P, Color);
		if (FillAlpha > 0.0f)
		{
			AddTriangle(Apex, Prev, P, FillColor);
		}
		if (i % SideStep == 0 && i != Segments)
		{
			AddLine(Apex, P, Color);
		}
		Prev = P;
	}
}

void FLineBatcher::AddConvex(const TArray<FVector>& LocalVertices, const TArray<uint32>& Indices,
	const FMatrix& WorldMatrix, const FVector4& Color)
{
	const uint32 VertexNum = static_cast<uint32>(LocalVertices.Num());
	if (VertexNum == 0 || Indices.Num() % 3 != 0)
	{
		return;
	}

	// 정점을 먼저 한 번씩만 월드로 옮긴다
	TArray<FVector> WorldVertices;
	WorldVertices.Reserve(VertexNum);
	for (const FVector& V : LocalVertices)
	{
		const FVector4 W = FVector4(V, 1.0f) * WorldMatrix;
		WorldVertices.Add(FVector(W.X, W.Y, W.Z));
	}

	// 이웃 삼각형이 공유하는 엣지는 한 번만 그린다.
	TSet<uint64> Drawn;
	auto DrawEdge = [&](uint32 A, uint32 B)
	{
		if (A >= VertexNum || B >= VertexNum)
		{
			return;
		}
		const uint64 Key = (static_cast<uint64>(std::min(A, B)) << 32) | std::max(A, B);
		if (Drawn.Add(Key))
		{
			AddLine(WorldVertices[A], WorldVertices[B], Color);
		}
	};

	for (int32 i = 0; i < Indices.Num(); i += 3)
	{
		DrawEdge(Indices[i], Indices[i + 1]);
		DrawEdge(Indices[i + 1], Indices[i + 2]);
		DrawEdge(Indices[i + 2], Indices[i]);
	}
}

void FLineBatcher::AddTwistArc(const FVector& Center, const FVector& Axis, const FVector& Ref,
	float LimitRadian, float Radius, const FVector4& Color, int32 Segments, float FillAlpha)
{
	if (Radius <= 0.0f)
	{
		return;
	}

	const FVector Up2 = Axis.Cross(Ref).GetSafeNormal();

	AddLine(Center, Center + Ref * Radius, Color);

	if (LimitRadian <= 0.0f)
	{
		return;
	}

	if (FillAlpha > 0.0f)
	{
		const float Limit = std::min(LimitRadian, PI);
		const FVector4 FillColor(Color.X, Color.Y, Color.Z, FillAlpha);
		auto ArcPoint = [&](float T) { return Center + (Ref * std::cos(T) + Up2 * std::sin(T)) * Radius; };
		FVector Prev = ArcPoint(-Limit);
		for (int32 i = 1; i <= Segments; ++i)
		{
			const FVector P = ArcPoint(-Limit + 2.0f * Limit * i / Segments);
			AddTriangle(Center, Prev, P, FillColor);
			Prev = P;
		}
	}

	if (LimitRadian >= PI)
	{
		AddWireArc(Center, Ref, Up2, Radius, -PI, PI, Color, Segments);
		return;
	}

	// 부채꼴: 호 + 양 끝 반지름 선
	AddWireArc(Center, Ref, Up2, Radius, -LimitRadian, LimitRadian, Color, Segments);
	const FVector EndPlus = Center + (Ref * std::cos(LimitRadian) + Up2 * std::sin(LimitRadian)) * Radius;
	const FVector EndMinus = Center + (Ref * std::cos(LimitRadian) - Up2 * std::sin(LimitRadian)) * Radius;
	AddLine(Center, EndPlus, Color);
	AddLine(Center, EndMinus, Color);
}


void FLineBatcher::AddCylinder(const FVector& Start, const FVector& End, float Radius, const FVector4& Color, int32 Segments)
{
	const FVector Axis = (End - Start).GetSafeNormal();
	if (Radius <= 0.0f || Segments < 3 || Axis.SizeSquared() <= 0.0f)
	{
		return;
	}

	const FVector Helper = std::abs(Axis.Z) < 0.99f ? FVector(0, 0, 1) : FVector(1, 0, 0);
	const FVector U = Helper.Cross(Axis).GetSafeNormal();
	const FVector V = Axis.Cross(U);
	auto RimOffset = [&](int32 i)
	{
		const float T = 2.0f * PI * i / Segments;
		return (U * std::cos(T) + V * std::sin(T)) * Radius;
	};

	FVector Prev = RimOffset(0);
	for (int32 i = 1; i <= Segments; ++i)
	{
		const FVector Cur = RimOffset(i);
		AddTriangle(Start + Prev, End + Prev, End + Cur, Color);
		AddTriangle(Start + Prev, End + Cur, Start + Cur, Color);
		AddTriangle(Start, Start + Cur, Start + Prev, Color);
		AddTriangle(End, End + Prev, End + Cur, Color);
		Prev = Cur;
	}
}

void FLineBatcher::AddSolidCapsule(const FVector& Center, const FVector& Up, const FVector& Right,
	float Radius, float HalfHeight, const FVector4& Color, int32 Segments, int32 Rings)
{
	if (Radius <= 0.0f || Segments < 3 || Rings < 1)
	{
		return;
	}

	const float Segment = std::max(HalfHeight - Radius, 0.0f);
	const FVector Forward = Up.Cross(Right).GetSafeNormal();

	// 위 극점에서 아래 극점까지 고리마다 (축 방향 위치, 반지름)
	TArray<std::pair<float, float>> Profile;
	for (int32 i = 0; i <= Rings; ++i)
	{
		const float Phi = 0.5f * PI * (1.0f - static_cast<float>(i) / Rings);
		Profile.Add({ Segment + Radius * std::sin(Phi), Radius * std::cos(Phi) });
	}
	for (int32 i = 0; i <= Rings; ++i)
	{
		const float Phi = -0.5f * PI * static_cast<float>(i) / Rings;
		Profile.Add({ -Segment + Radius * std::sin(Phi), Radius * std::cos(Phi) });
	}

	auto PointAt = [&](int32 RingIndex, int32 SegmentIndex)
	{
		const float T = 2.0f * PI * SegmentIndex / Segments;
		const auto& [Axial, RingRadius] = Profile[RingIndex];
		return Center + Up * Axial + (Right * std::cos(T) + Forward * std::sin(T)) * RingRadius;
	};

	auto OutwardOf = [&](const FVector& P)
	{
		const float Axial = std::clamp((P - Center).Dot(Up), -Segment, Segment);
		return P - (Center + Up * Axial);
	};

	for (int32 r = 0; r + 1 < Profile.Num(); ++r)
	{
		for (int32 s = 0; s < Segments; ++s)
		{
			const FVector A = PointAt(r, s);
			const FVector B = PointAt(r, s + 1);
			const FVector C = PointAt(r + 1, s + 1);
			const FVector D = PointAt(r + 1, s);
			const FVector Outward = OutwardOf((A + B + C + D) * 0.25f);
			AddSolidTriangle(A, B, C, Color, Outward);
			AddSolidTriangle(A, C, D, Color, Outward);
		}
	}
}

void FLineBatcher::AddSolidOrientedBox(const FBox& LocalBox, const FMatrix& WorldMatrix, const FVector4& Color)
{
	FVector Corners[8];
	for (int32 c = 0; c < 8; ++c)
	{
		const FVector Local((c & 1) ? LocalBox.Max.X : LocalBox.Min.X,
			(c & 2) ? LocalBox.Max.Y : LocalBox.Min.Y,
			(c & 4) ? LocalBox.Max.Z : LocalBox.Min.Z);
		Corners[c] = WorldMatrix.TransformPosition(Local);
	}

	static const int32 Faces[6][4] = {
		{ 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 },
		{ 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 },
	};
	const FVector BoxCenter = WorldMatrix.TransformPosition((LocalBox.Min + LocalBox.Max) * 0.5f);
	for (const auto& Face : Faces)
	{
		const FVector FaceCenter = (Corners[Face[0]] + Corners[Face[1]] + Corners[Face[2]] + Corners[Face[3]]) * 0.25f;
		const FVector Outward = FaceCenter - BoxCenter;
		AddSolidTriangle(Corners[Face[0]], Corners[Face[1]], Corners[Face[2]], Color, Outward);
		AddSolidTriangle(Corners[Face[0]], Corners[Face[2]], Corners[Face[3]], Color, Outward);
	}
}

void FLineBatcher::AddWireArc(const FVector& Center, const FVector& AxisA, const FVector& AxisB,
	float Radius, float StartRadian, float EndRadian, const FVector4& Color, int32 Segments)
{
	auto PointAt = [&](float T) { return Center + AxisA * (std::cos(T) * Radius) + AxisB * (std::sin(T) * Radius); };
	FVector Prev = PointAt(StartRadian);
	for (int32 i = 1; i <= Segments; ++i)
	{
		const FVector P = PointAt(StartRadian + (EndRadian - StartRadian) * i / Segments);
		AddLine(Prev, P, Color);
		Prev = P;
	}
}


void FLineBatcher::SetDepthTest(bool bEnable)
{
	PipelineState.DepthStencilState = bEnable ? EDepthStencilState::Default : EDepthStencilState::Disabled;
	TrianglePipelineState.DepthStencilState = bEnable ? EDepthStencilState::ReadOnly : EDepthStencilState::Disabled;
	SolidPipelineState.DepthStencilState = TrianglePipelineState.DepthStencilState;
}

void FLineBatcher::Flush()
{
	// 반투명 면을 먼저 그려 선이 그 위에 보이게 한다
	if (SolidVertexCount > 0)
	{
		RenderCommand::BindPipelineState(SolidPipelineState);
		RenderCommand::BindVertexBuffer(SolidVertexBuffer.get());
		RenderCommand::BindConstantBuffer(0, CB.get(), EShaderBindFlagBits::Vertex);
		RenderCommand::Draw(SolidVertexCount, 0);
	}
	if (TriangleVertexCount > 0)
	{
		RenderCommand::BindPipelineState(TrianglePipelineState);
		RenderCommand::BindVertexBuffer(TriangleVertexBuffer.get());
		RenderCommand::BindConstantBuffer(0, CB.get(), EShaderBindFlagBits::Vertex);
		RenderCommand::Draw(TriangleVertexCount, 0);
	}

	if (VertexCount == 0)
	{
		return;
	}

	RenderCommand::BindPipelineState(PipelineState);
	RenderCommand::BindVertexBuffer(VertexBuffer.get());
	RenderCommand::BindConstantBuffer(0, CB.get(), EShaderBindFlagBits::Vertex);

	RenderCommand::Draw(VertexCount, 0);
}

void FLineBatcher::OnRender(const FMatrix& ViewProjection)
{
	if (VertexCount == 0 && TriangleVertexCount == 0 && SolidVertexCount == 0)
	{
		return;
	}

	FMatrix VP = ViewProjection;
	if (VertexCount > 0)
	{
		RenderCommand::UpdateBufferData(VertexBuffer.get(), VertexBufferBase, sizeof(FLineBatchVertex) * VertexCount);
	}
	if (TriangleVertexCount > 0)
	{
		RenderCommand::UpdateBufferData(TriangleVertexBuffer.get(), TriangleVertexBase, sizeof(FLineBatchVertex) * TriangleVertexCount);
	}
	if (SolidVertexCount > 0)
	{
		RenderCommand::UpdateBufferData(SolidVertexBuffer.get(), SolidVertexBase, sizeof(FLineBatchVertex) * SolidVertexCount);
	}
	RenderCommand::UpdateBufferData(CB.get(), &VP, sizeof(FMatrix));

	Flush();
}

