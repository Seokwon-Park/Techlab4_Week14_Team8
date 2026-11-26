#include "EnginePCH.h"
#include "Render/VertexFactory.h"
#include "Render/RenderCommand.h"
#include <iterator>

namespace
{
	// 변형 매크로 이름은 셰이더 쪽 키와 같아야 한다(FRenderResourceManager::ScanShaders가 "Base#매크로"로 등록)
	const FVertexFactoryTypeInfo GTypeInfos[] =
	{
		{ "Local",          nullptr,                    false },
		{ "GpuSkin",        "GPU_SKIN",                 false },
		{ "MeshParticle",   "MESH_PARTICLE_INSTANCING", false },
		{ "SpriteParticle", nullptr,                    true  },
		{ "Trail",          nullptr,                    true  },
	};
	static_assert(std::size(GTypeInfos) == static_cast<size_t>(EVertexFactoryType::Count));
}

const FVertexFactoryTypeInfo& GetVertexFactoryTypeInfo(EVertexFactoryType Type)
{
	return GTypeInfos[static_cast<uint8>(Type)];
}

void FVertexFactory::Bind() const
{
	RenderCommand::BindVertexBuffer(VertexBuffer);
	RenderCommand::BindIndexBuffer(IndexBuffer);
}

void FGpuSkinVertexFactory::Bind() const
{
	FVertexFactory::Bind();
	if (BoneBuffer)
		RenderCommand::BindShaderResource(BoneMatrixBufferSlot, BoneBuffer, EShaderBindFlagBits::Vertex);
}
