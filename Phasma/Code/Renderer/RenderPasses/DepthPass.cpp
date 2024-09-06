#include "Renderer/RenderPasses/DepthPass.h"
#include "Scene/Model.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "GUI/GUI.h"
#include "tinygltf/stb_image.h"
#include "Shader/Reflection.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Descriptor.h"
#include "Systems/LightSystem.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Renderer/Pipeline.h"
#include "Systems/RendererSystem.h"
#include "Scene/Geometry.h"

namespace pe
{
    DepthPass::DepthPass()
    {
    }

    DepthPass::~DepthPass()
    {
    }

    void DepthPass::Init()
    {
        m_renderQueue = RHII.GetRenderQueue();
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        m_depthStencil = rs->GetDepthStencilTarget("depthStencil");

        attachment = {};
        attachment.image = m_depthStencil;
        attachment.initialLayout = ImageLayout::Undefined;
        attachment.finalLayout = ImageLayout::Attachment;
        attachment.loadOp = AttachmentLoadOp::Clear;
        attachment.storeOp = AttachmentStoreOp::Store;
    }

    void DepthPass::UpdatePassInfo()
    {
        m_passInfo.name = "DepthPrePass_pipeline";
        m_passInfo.pVertShader = Shader::Create("Shaders/Depth/DepthVS.hlsl", ShaderStage::VertexBit);
        m_passInfo.pFragShader = Shader::Create("Shaders/Depth/DepthPS.hlsl", ShaderStage::FragmentBit);
        m_passInfo.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfo.cullMode = CullMode::Front;
        m_passInfo.depthFormat = RHII.GetDepthFormat();
        m_passInfo.depthTestEnable = true;
        m_passInfo.depthWriteEnable = true;
        m_passInfo.UpdateHash();
    }

    void DepthPass::CreateUniforms(CommandBuffer *cmd)
    {
        // depth uniforms are created with the geometry
    }

    void DepthPass::UpdateDescriptorSets()
    {
        // depth pass descriptor sets are updated with Scene::Update
    }

    void DepthPass::Update(Camera *camera)
    {
    }

    CommandBuffer *DepthPass::Draw()
    {
        PE_ERROR_IF(m_geometry == nullptr, "Geometry was not set");

        struct PushConstants_DepthPass
        {
            uint32_t meshIndex;
            uint32_t meshJointCount;
            uint32_t primitiveImageIndex[5];
            float alphaCut;
        };

        CommandBuffer *cmd = CommandBuffer::GetFree(m_renderQueue);
        cmd->Begin();
        cmd->BeginDebugRegion("Depth Prepass");

        auto &drawInfosOpaque = m_geometry->GetDrawInfosOpaque();
        if (drawInfosOpaque.empty())
        {
            ClearDepthStencil(cmd);
        }
        else
        {
            cmd->BeginPass({attachment}, "DepthPass");
            cmd->SetViewport(0.f, 0.f, m_depthStencil->GetWidth_f(), m_depthStencil->GetHeight_f());
            cmd->SetScissor(0, 0, m_depthStencil->GetWidth(), m_depthStencil->GetHeight());
            cmd->BindPipeline(m_passInfo);
            cmd->BindIndexBuffer(m_geometry->GetBuffer(), 0);
            cmd->BindVertexBuffer(m_geometry->GetBuffer(), m_geometry->GetPositionsOffset());

            PushConstants_DepthPass constants{};
            constants.meshJointCount = 0;
            constants.alphaCut = 0.0f;

            for (auto &drawInfo : drawInfosOpaque)
            {
                ModelGltf &model = *drawInfo.second.model;

                int node = drawInfo.second.node;
                int mesh = model.nodes[node].mesh;
                if (mesh < 0)
                    continue;
                constants.meshIndex = GetUboDataOffset(model.m_meshesInfo[mesh].dataOffset);

                int primitive = drawInfo.second.primitive;
                PrimitiveInfo &primitiveInfo = model.m_meshesInfo[mesh].primitivesInfo[primitive];
                for (int i = 0; i < 5; i++)
                    constants.primitiveImageIndex[i] = primitiveInfo.viewsIndex[i];

                cmd->SetConstants(constants);
                cmd->PushConstants();

                cmd->DrawIndexed(
                    primitiveInfo.indicesCount,
                    1,
                    primitiveInfo.indexOffset,
                    primitiveInfo.vertexOffset,
                    0);
            }

            cmd->EndPass();
        }

        m_geometry = nullptr;
        cmd->EndDebugRegion();
        cmd->End();

        return cmd;
    }

    void DepthPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void DepthPass::ClearDepthStencil(CommandBuffer *cmd)
    {
        cmd->ClearDepthStencils({m_depthStencil});
    }

    void DepthPass::Destroy()
    {
    }
}
