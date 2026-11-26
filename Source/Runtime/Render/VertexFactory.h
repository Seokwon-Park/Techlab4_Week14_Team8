#pragma once

#include <span>
#include "Core/Types.h"

class FVertexBuffer;
class FIndexBuffer;
class FStructuredBuffer;
struct FMeshParticleInstanceVertex;

inline constexpr uint32 BoneMatrixBufferSlot = 14; // GpuSkinCommon.hlsli의 t14

// 정점 스트림의 종류. 패스는 이 값으로 같은 셰이더의 정점 타입별 변형을 고른다.
// UE의 FVertexFactoryType과 같은 자리다.
enum class EVertexFactoryType : uint8
{
	Local,           // FVertex (스태틱 메시, CPU 스킨 결과)
	GpuSkin,         // FGpuSkinVertex + 본 행렬 버퍼(t14)
	MeshParticle,    // FVertex + 인스턴스 스트림(slot 1)
	SpriteParticle,  // 전용 셰이더(ParticleSpriteShader)가 입력을 직접 정의한다
	Trail,           // 전용 셰이더(ParticleTrailShader)가 입력을 직접 정의한다
	Count
};

struct FVertexFactoryTypeInfo
{
	const char* Name;
	// 같은 셰이더의 정점 타입 변형을 찾을 때 쓰는 매크로(셰이더 키 "Base#Macro"). nullptr이면 기본 셰이더를 그대로 쓴다
	const char* VariantMacro;
	// true면 전용 셰이더가 입력을 정의하므로 다른 셰이더의 변형으로 바꾸지 않는다
	bool bDedicatedShader;
};

const FVertexFactoryTypeInfo& GetVertexFactoryTypeInfo(EVertexFactoryType Type);

// 정점 스트림의 레이아웃과 바인딩을 맡는다. 패스는 정점 버퍼가 어떤 모양인지 몰라도 된다.
// 객체는 메시/프록시/FRenderQueue가 소유하고, FMeshDrawBatch는 포인터만 들고 그 프레임 안에서 그린다.
class FVertexFactory
{
public:
	virtual ~FVertexFactory() = default;
	virtual EVertexFactoryType GetType() const = 0;
	virtual void Bind() const;

	FVertexBuffer* GetVertexBuffer() const { return VertexBuffer; }
	FIndexBuffer* GetIndexBuffer() const { return IndexBuffer; }
	bool IsValid() const { return VertexBuffer && IndexBuffer; }

protected:
	FVertexBuffer* VertexBuffer = nullptr;
	FIndexBuffer* IndexBuffer = nullptr;
};

class FLocalVertexFactory : public FVertexFactory
{
public:
	void Init(FVertexBuffer* VB, FIndexBuffer* IB) { VertexBuffer = VB; IndexBuffer = IB; }
	EVertexFactoryType GetType() const override { return EVertexFactoryType::Local; }
};

// Cloth는 CPU에서 정점 버퍼에 직접 써 넣으므로 본 버퍼만 바인딩한다
class FGpuSkinVertexFactory : public FVertexFactory
{
public:
	void Init(FVertexBuffer* VB, FIndexBuffer* IB, const FStructuredBuffer* Bones)
	{
		VertexBuffer = VB;
		IndexBuffer = IB;
		BoneBuffer = Bones;
	}
	EVertexFactoryType GetType() const override { return EVertexFactoryType::GpuSkin; }
	void Bind() const override;
	const FStructuredBuffer* GetBoneBuffer() const { return BoneBuffer; }

private:
	const FStructuredBuffer* BoneBuffer = nullptr;
};

// 메시 파티클 인스턴싱. 인스턴스 데이터는 FRenderQueue가 그 프레임 동안 들고 있고,
// FRenderer가 동적 인스턴스 버퍼에 올려 slot 1에 바인딩한다(올리지 못하면 인스턴스마다 그리는 폴백).
class FMeshParticleVertexFactory : public FVertexFactory
{
public:
	void Init(FVertexBuffer* VB, FIndexBuffer* IB, std::span<const FMeshParticleInstanceVertex> InInstances)
	{
		VertexBuffer = VB;
		IndexBuffer = IB;
		Instances = InInstances;
	}
	EVertexFactoryType GetType() const override { return EVertexFactoryType::MeshParticle; }
	std::span<const FMeshParticleInstanceVertex> GetInstances() const { return Instances; }

private:
	std::span<const FMeshParticleInstanceVertex> Instances;
};

class FSpriteParticleVertexFactory : public FVertexFactory
{
public:
	void Init(FVertexBuffer* VB, FIndexBuffer* IB) { VertexBuffer = VB; IndexBuffer = IB; }
	EVertexFactoryType GetType() const override { return EVertexFactoryType::SpriteParticle; }
};

// 빔, 리본 공용. 정점이 이미 월드 좌표이고 인스턴싱하지 않는다
class FTrailVertexFactory : public FVertexFactory
{
public:
	void Init(FVertexBuffer* VB, FIndexBuffer* IB) { VertexBuffer = VB; IndexBuffer = IB; }
	EVertexFactoryType GetType() const override { return EVertexFactoryType::Trail; }
};
