#include "Renderer/RenderPasses/GbufferPass.h"
#include "Scene/Model.h"
#include "Shader/Shader.h"
#include "tinygltf/stb_image.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Renderer/Pipeline.h"
#include "Scene/Geometry.h"
#include "Scene/Model.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"

namespace pe
{
    void GbufferOpaquePass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        srmRT = rs->GetRenderTarget("srm"); // Specular Roughness Metallic
        normalRT = rs->GetRenderTarget("normal");
        albedoRT = rs->GetRenderTarget("albedo");
        velocityRT = rs->GetRenderTarget("velocity");
        emissiveRT = rs->GetRenderTarget("emissive");
        viewportRT = rs->GetRenderTarget("viewport");
        transparencyRT = rs->GetRenderTarget("transparency");
        depthStencilRT = rs->GetDepthStencilTarget("depthStencil");

        m_attachments.resize(7);
        for (int i = 0; i < 6; i++)
        {
            m_attachments[i] = {};
            m_attachments[i].initialLayout = ImageLayout::Undefined;
            m_attachments[i].finalLayout = ImageLayout::ShaderReadOnly;
        }
        m_attachments[0].image = normalRT;
        m_attachments[1].image = albedoRT;
        m_attachments[2].image = srmRT;
        m_attachments[3].image = velocityRT;
        m_attachments[4].image = emissiveRT;
        m_attachments[5].image = transparencyRT;

        m_attachments[6] = {};
        m_attachments[6].image = depthStencilRT;
        m_attachments[6].initialLayout = ImageLayout::Attachment;
        m_attachments[6].finalLayout = ImageLayout::ShaderReadOnly;
        m_attachments[6].loadOp = AttachmentLoadOp::Load;
        m_attachments[6].storeOp = AttachmentStoreOp::Store;
    }

    void GbufferOpaquePass::UpdatePassInfo()
    {
        std::vector<Format> colorformats{
            normalRT->GetFormat(),
            albedoRT->GetFormat(),
            srmRT->GetFormat(),
            velocityRT->GetFormat(),
            emissiveRT->GetFormat(),
            transparencyRT->GetFormat()};

        Format depthFormat = RHII.GetDepthFormat();

        m_passInfo->name = "gbuffer_opaque_pipeline";
        m_passInfo->pVertShader = Shader::Create("Shaders/Gbuffer/GBufferVS.hlsl", ShaderStage::VertexBit);
        m_passInfo->pFragShader = Shader::Create("Shaders/Gbuffer/GBufferPS.hlsl", ShaderStage::FragmentBit);
        m_passInfo->dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfo->cullMode = CullMode::Front;
        m_passInfo->blendEnable = false;
        m_passInfo->colorBlendAttachments = {
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default};
        m_passInfo->colorFormats = colorformats;
        m_passInfo->depthFormat = depthFormat;
        m_passInfo->depthCompareOp = CompareOp::Equal; // we use depth prepass for opaque
        m_passInfo->depthTestEnable = true;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->ReflectDescriptors();
        m_passInfo->UpdateHash();
    }

    void GbufferOpaquePass::PassBarriers(CommandBuffer *cmd)
    {
    }

    void GbufferOpaquePass::Draw(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_geometry == nullptr, "Geometry was not set");

        Camera &camera = *GetGlobalSystem<CameraSystem>()->GetCamera(0);

        struct PushConstants_GBuffer
        {
            uint32_t jointsCount;
            vec2 projJitter;
            vec2 prevProjJitter;
            float alphaCut;
            uint32_t transparentPass;
            uint32_t meshIndex;
            uint32_t primitiveDataIndex;
            uint32_t primitiveImageIndex[5];
        };

        if (!m_geometry->HasDrawInfo())
        {
            // Just clear the render targets
            ClearRenderTargets(cmd);
        }
        else
        {
            if (m_geometry->HasOpaqueDrawInfo())
            {
                Image *colorTargets[6]{normalRT, albedoRT, srmRT, velocityRT, emissiveRT, transparencyRT};

                cmd->BeginPass(m_attachments, "GbufferOpaquePass");
                cmd->BindIndexBuffer(m_geometry->GetBuffer(), 0);
                cmd->BindVertexBuffer(m_geometry->GetBuffer(), m_geometry->GetVerticesOffset());
                cmd->SetViewport(0.f, 0.f, depthStencilRT->GetWidth_f(), depthStencilRT->GetHeight_f());
                cmd->SetScissor(0, 0, depthStencilRT->GetWidth(), depthStencilRT->GetHeight());
                cmd->BindPipeline(*m_passInfo);

                PushConstants_GBuffer constants{};
                constants.jointsCount = 0u;
                constants.projJitter = camera.GetProjJitter();
                constants.prevProjJitter = camera.GetPrevProjJitter();
                constants.alphaCut = 0.0f;
                constants.transparentPass = 0u;

                for (auto &drawInfo : m_geometry->GetDrawInfosOpaque())
                {
                    ModelGltf &model = *drawInfo.second.model;

                    int node = drawInfo.second.node;
                    int mesh = model.nodes[node].mesh;
                    if (mesh < 0)
                        continue;

                    int primitive = drawInfo.second.primitive;
                    PrimitiveInfo &primitiveInfo = model.m_meshesInfo[mesh].primitivesInfo[primitive];

                    constants.meshIndex = GetUboDataOffset(model.m_meshesInfo[mesh].dataOffset);
                    constants.primitiveDataIndex = GetUboDataOffset(primitiveInfo.dataOffset);
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
        }

        m_geometry = nullptr;
    }

    void GbufferOpaquePass::Resize(uint32_t width, uint32_t height)
    {
        Init();
    }

    void GbufferOpaquePass::ClearRenderTargets(CommandBuffer *cmd)
    {
        std::vector<Image *> colorTargets{normalRT, albedoRT, srmRT, velocityRT, emissiveRT, viewportRT, transparencyRT};
        cmd->ClearColors(colorTargets);

        std::vector<ImageBarrierInfo> barriers(colorTargets.size());
        for (size_t i = 0; i < barriers.size(); i++)
        {
            barriers[i].image = colorTargets[i];
            barriers[i].layout = ImageLayout::Attachment;
            barriers[i].stageFlags = PipelineStage::ColorAttachmentOutputBit;
            barriers[i].accessMask = Access::ColorAttachmentWriteBit;
        }

        cmd->ImageBarriers(barriers);
    }

    void GbufferOpaquePass::ClearDepthStencil(CommandBuffer *cmd)
    {
        cmd->ClearDepthStencils({depthStencilRT});
    }

    void GbufferOpaquePass::Destroy()
    {
        Buffer::Destroy(uniform);
    }

    void GbufferTransparentPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        srmRT = rs->GetRenderTarget("srm"); // Specular Roughness Metallic
        normalRT = rs->GetRenderTarget("normal");
        albedoRT = rs->GetRenderTarget("albedo");
        velocityRT = rs->GetRenderTarget("velocity");
        emissiveRT = rs->GetRenderTarget("emissive");
        viewportRT = rs->GetRenderTarget("viewport");
        transparencyRT = rs->GetRenderTarget("transparency");
        depthStencilRT = rs->GetDepthStencilTarget("depthStencil");

        m_attachments.resize(7);
        for (int i = 0; i < 6; i++)
        {
            m_attachments[i] = {};
            m_attachments[i].initialLayout = ImageLayout::ShaderReadOnly;
            m_attachments[i].finalLayout = ImageLayout::ShaderReadOnly;
            m_attachments[i].loadOp = AttachmentLoadOp::Load;
        }
        m_attachments[0].image = normalRT;
        m_attachments[1].image = albedoRT;
        m_attachments[2].image = srmRT;
        m_attachments[3].image = velocityRT;
        m_attachments[4].image = emissiveRT;
        m_attachments[5].image = transparencyRT;

        m_attachments[6] = {};
        m_attachments[6].image = depthStencilRT;
        m_attachments[6].initialLayout = ImageLayout::Attachment;
        m_attachments[6].finalLayout = ImageLayout::ShaderReadOnly;
        m_attachments[6].loadOp = AttachmentLoadOp::Load;
        m_attachments[6].storeOp = AttachmentStoreOp::Store;
    }

    void GbufferTransparentPass::UpdatePassInfo()
    {
        std::vector<Format> colorformats{
            normalRT->GetFormat(),
            albedoRT->GetFormat(),
            srmRT->GetFormat(),
            velocityRT->GetFormat(),
            emissiveRT->GetFormat(),
            transparencyRT->GetFormat()};

        Format depthFormat = RHII.GetDepthFormat();

        m_passInfo->name = "gbuffer_transparent_pipeline";
        m_passInfo->pVertShader = Shader::Create("Shaders/Gbuffer/GBufferVS.hlsl", ShaderStage::VertexBit);
        m_passInfo->pFragShader = Shader::Create("Shaders/Gbuffer/GBufferPS.hlsl", ShaderStage::FragmentBit);
        m_passInfo->dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfo->cullMode = CullMode::Front;
        m_passInfo->blendEnable = true;
        m_passInfo->colorBlendAttachments = {
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default,
            PipelineColorBlendAttachmentState::Default};
        m_passInfo->colorFormats = colorformats;
        m_passInfo->depthFormat = depthFormat;
        m_passInfo->depthCompareOp = Settings::Get<GlobalSettings>().reverse_depth ? CompareOp::GreaterOrEqual : CompareOp::LessOrEqual;
        m_passInfo->depthTestEnable = true;
        m_passInfo->depthWriteEnable = true;
        m_passInfo->ReflectDescriptors();
        m_passInfo->UpdateHash();
    }

    void GbufferTransparentPass::PassBarriers(CommandBuffer *cmd)
    {
        Image *rts[7]{normalRT, albedoRT, srmRT, velocityRT, emissiveRT, transparencyRT, depthStencilRT};

        // Color targets
        std::vector<ImageBarrierInfo> barriers(7);
        for (size_t i = 0; i < 6; i++)
        {
            barriers[i].image = rts[i];
            barriers[i].layout = ImageLayout::Attachment;
            barriers[i].stageFlags = PipelineStage::ColorAttachmentOutputBit;
            barriers[i].accessMask = Access::ColorAttachmentWriteBit;
        }

        // Depth
        barriers[6].image = rts[6];
        barriers[6].layout = ImageLayout::Attachment;
        barriers[6].stageFlags = PipelineStage::EarlyFragmentTestsBit | PipelineStage::LateFragmentTestsBit;
        barriers[6].accessMask = Access::DepthStencilAttachmentWriteBit;

        cmd->ImageBarriers(barriers);
    }

    void GbufferTransparentPass::Draw(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_geometry == nullptr, "Geometry was not set");

        Camera &camera = *GetGlobalSystem<CameraSystem>()->GetCamera(0);

        struct PushConstants_GBuffer
        {
            uint32_t jointsCount;
            vec2 projJitter;
            vec2 prevProjJitter;
            float alphaCut;
            uint32_t transparentPass;
            uint32_t meshIndex;
            uint32_t primitiveDataIndex;
            uint32_t primitiveImageIndex[5];
        };

        if (m_geometry->HasAlphaDrawInfo())
        {
            PassBarriers(cmd);

            cmd->BeginPass(m_attachments, "GbufferTransparentPass");
            cmd->BindIndexBuffer(m_geometry->GetBuffer(), 0);
            cmd->BindVertexBuffer(m_geometry->GetBuffer(), m_geometry->GetVerticesOffset());
            cmd->SetViewport(0.f, 0.f, depthStencilRT->GetWidth_f(), depthStencilRT->GetHeight_f());
            cmd->SetScissor(0, 0, depthStencilRT->GetWidth(), depthStencilRT->GetHeight());
            cmd->BindPipeline(*m_passInfo);

            PushConstants_GBuffer constants{};
            constants.jointsCount = 0u;
            constants.projJitter = camera.GetProjJitter();
            constants.prevProjJitter = camera.GetPrevProjJitter();
            constants.transparentPass = 1u;

            for (auto &drawInfo : m_geometry->GetDrawInfosAlphaCut())
            {
                ModelGltf &model = *drawInfo.second.model;

                int node = drawInfo.second.node;
                int mesh = model.nodes[node].mesh;
                if (mesh < 0)
                    continue;

                int primitive = drawInfo.second.primitive;
                PrimitiveInfo &primitiveInfo = model.m_meshesInfo[mesh].primitivesInfo[primitive];
                auto &primitiveGltf = model.meshes[mesh].primitives[primitive];

                constants.alphaCut = static_cast<float>(model.materials[primitiveGltf.material].alphaCutoff);
                constants.meshIndex = GetUboDataOffset(model.m_meshesInfo[mesh].dataOffset);
                constants.primitiveDataIndex = GetUboDataOffset(primitiveInfo.dataOffset);
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

            for (auto &drawInfo : m_geometry->GetDrawInfosAlphaBlend())
            {
                ModelGltf &model = *drawInfo.second.model;

                int node = drawInfo.second.node;
                int mesh = model.nodes[node].mesh;
                if (mesh < 0)
                    continue;

                int primitive = drawInfo.second.primitive;
                PrimitiveInfo &primitiveInfo = model.m_meshesInfo[mesh].primitivesInfo[primitive];

                constants.alphaCut = 0.0f;
                constants.meshIndex = GetUboDataOffset(model.m_meshesInfo[mesh].dataOffset);
                constants.primitiveDataIndex = GetUboDataOffset(primitiveInfo.dataOffset);
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
    }

    void GbufferTransparentPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
    }

    void GbufferTransparentPass::ClearRenderTargets(CommandBuffer *cmd)
    {
        std::vector<Image *> colorTargets{normalRT, albedoRT, srmRT, velocityRT, emissiveRT, viewportRT, transparencyRT};
        cmd->ClearColors(colorTargets);

        std::vector<ImageBarrierInfo> barriers(colorTargets.size());
        for (size_t i = 0; i < barriers.size(); i++)
        {
            barriers[i].image = colorTargets[i];
            barriers[i].layout = ImageLayout::Attachment;
            barriers[i].stageFlags = PipelineStage::ColorAttachmentOutputBit;
            barriers[i].accessMask = Access::ColorAttachmentWriteBit;
        }

        cmd->ImageBarriers(barriers);
    }

    void GbufferTransparentPass::ClearDepthStencil(CommandBuffer *cmd)
    {
        cmd->ClearDepthStencils({depthStencilRT});
    }

    void GbufferTransparentPass::Destroy()
    {
        Buffer::Destroy(uniform);
    }
}
