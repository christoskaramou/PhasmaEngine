#if PE_VULKAN
#include "Renderer/RenderPasses/GbufferPass.h"
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
#include "Renderer/Framebuffer.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Queue.h"
#include "Renderer/RenderPasses/SSAOPass.h"
#include "Utilities/Downsampler.h"
#include "Scene/Geometry.h"
#include "Scene/Model.h"
#include "Systems/LightSystem.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"

namespace pe
{
    GbufferPass::GbufferPass()
    {
    }

    GbufferPass::~GbufferPass()
    {
    }

    void GbufferPass::Init()
    {
        m_renderQueue = RHII.GetRenderQueue();
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        srmRT = rs->GetRenderTarget("srm"); // Specular Roughness Metallic
        normalRT = rs->GetRenderTarget("normal");
        albedoRT = rs->GetRenderTarget("albedo");
        velocityRT = rs->GetRenderTarget("velocity");
        emissiveRT = rs->GetRenderTarget("emissive");
        viewportRT = rs->GetRenderTarget("viewport");
        transparencyRT = rs->GetRenderTarget("transparency");
        depthStencilRT = rs->GetDepthStencilTarget("depthStencil");

        m_attachmentsOpaque.resize(7);
        for (int i = 0; i < 6; i++)
        {
            m_attachmentsOpaque[i] = {};
            m_attachmentsOpaque[i].initialLayout = ImageLayout::Undefined;
            m_attachmentsOpaque[i].finalLayout = ImageLayout::ShaderReadOnly;
        }
        m_attachmentsOpaque[0].image = normalRT;
        m_attachmentsOpaque[1].image = albedoRT;
        m_attachmentsOpaque[2].image = srmRT;
        m_attachmentsOpaque[3].image = velocityRT;
        m_attachmentsOpaque[4].image = emissiveRT;
        m_attachmentsOpaque[5].image = transparencyRT;

        m_attachmentsOpaque[6] = {};
        m_attachmentsOpaque[6].image = depthStencilRT;
        m_attachmentsOpaque[6].initialLayout = ImageLayout::Attachment;
        m_attachmentsOpaque[6].finalLayout = ImageLayout::ShaderReadOnly;
        m_attachmentsOpaque[6].loadOp = AttachmentLoadOp::Load;
        m_attachmentsOpaque[6].storeOp = AttachmentStoreOp::Store;

        // copy opaque attachments to transparent
        m_attachmentsTransparent = m_attachmentsOpaque;
        for (int i = 0; i < 6; i++)
        {
            m_attachmentsTransparent[i].initialLayout = ImageLayout::ShaderReadOnly;
            m_attachmentsTransparent[i].loadOp = AttachmentLoadOp::Load;
        }
    }

    void GbufferPass::UpdatePassInfo()
    {
        Format colorformats[]{
            normalRT->GetFormat(),
            albedoRT->GetFormat(),
            srmRT->GetFormat(),
            velocityRT->GetFormat(),
            emissiveRT->GetFormat(),
            transparencyRT->GetFormat()};

        Format depthFormat = RHII.GetDepthFormat();

        // Opaque
        m_passInfoOpaque.name = "gbuffer_opaque_pipeline";
        m_passInfoOpaque.pVertShader = Shader::Create("Shaders/Gbuffer/GBufferVS.hlsl", ShaderStage::VertexBit);
        m_passInfoOpaque.pFragShader = Shader::Create("Shaders/Gbuffer/GBufferPS.hlsl", ShaderStage::FragmentBit);
        m_passInfoOpaque.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfoOpaque.cullMode = CullMode::Front;
        m_passInfoOpaque.alphaBlend = false;
        m_passInfoOpaque.colorBlendAttachments = {
            normalRT->GetBlendAttachment(),
            albedoRT->GetBlendAttachment(),
            srmRT->GetBlendAttachment(),
            velocityRT->GetBlendAttachment(),
            emissiveRT->GetBlendAttachment(),
            transparencyRT->GetBlendAttachment()};
        m_passInfoOpaque.colorFormats = colorformats;
        m_passInfoOpaque.depthFormat = depthFormat;
        m_passInfoOpaque.depthCompareOp = CompareOp::Equal; // we use depth prepass for opaque
        m_passInfoOpaque.depthTestEnable = true;
        m_passInfoOpaque.depthWriteEnable = false;
        m_passInfoOpaque.UpdateHash();

        // Transparent
        m_passInfoAlpha.name = "gbuffer_transparent_pipeline";
        m_passInfoAlpha.pVertShader = Shader::Create("Shaders/Gbuffer/GBufferVS.hlsl", ShaderStage::VertexBit);
        m_passInfoAlpha.pFragShader = Shader::Create("Shaders/Gbuffer/GBufferPS.hlsl", ShaderStage::FragmentBit);
        m_passInfoAlpha.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        m_passInfoAlpha.cullMode = CullMode::Front;
        m_passInfoAlpha.alphaBlend = true;
        m_passInfoAlpha.colorBlendAttachments = {
            normalRT->GetBlendAttachment(),
            albedoRT->GetBlendAttachment(),
            srmRT->GetBlendAttachment(),
            velocityRT->GetBlendAttachment(),
            emissiveRT->GetBlendAttachment(),
            transparencyRT->GetBlendAttachment()};
        m_passInfoAlpha.colorFormats = colorformats;
        m_passInfoAlpha.depthFormat = depthFormat;
        m_passInfoAlpha.depthCompareOp = Settings::Get<GlobalSettings>().reverse_depth ? CompareOp::GreaterOrEqual : CompareOp::LessOrEqual;
        m_passInfoAlpha.depthTestEnable = true;
        m_passInfoAlpha.depthWriteEnable = true;
        m_passInfoAlpha.UpdateHash();
    }

    void GbufferPass::CreateUniforms(CommandBuffer *cmd)
    {
    }

    void GbufferPass::UpdateDescriptorSets()
    {
    }

    void GbufferPass::Update(Camera *camera)
    {
    }

    void GbufferPass::PassBarriers(CommandBuffer *cmd)
    {
        if (m_blendType == BlendType::Opaque)
        {
        }
        else if (m_blendType == BlendType::Transparent)
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
    }

    CommandBuffer *GbufferPass::Draw()
    {
        PE_ERROR_IF(m_geometry == nullptr, "Geometry was not set");
        PE_ERROR_IF(m_blendType == BlendType::None, "BlendType is None");

        CommandBuffer *cmd = CommandBuffer::GetFree(m_renderQueue);
        cmd->Begin();
        cmd->BeginDebugRegion("GBuffers");

        Camera &camera = *GetGlobalSystem<CameraSystem>()->GetCamera(0);
        ShadowPass *shadows = GetGlobalComponent<ShadowPass>();

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

        if (!m_geometry->HasDrawInfo() && m_blendType == BlendType::Opaque)
        {
            // Just clear the render targets
            ClearRenderTargets(cmd);
        }
        else if (m_blendType == BlendType::Opaque)
        {
            if (m_geometry->HasOpaqueDrawInfo())
            {
                Image *colorTargets[6]{normalRT, albedoRT, srmRT, velocityRT, emissiveRT, transparencyRT};

                cmd->BeginPass(m_attachmentsOpaque, "GBuffers_Opaque");
                cmd->BindIndexBuffer(m_geometry->GetBuffer(), 0);
                cmd->BindVertexBuffer(m_geometry->GetBuffer(), m_geometry->GetVerticesOffset());
                cmd->SetViewport(0.f, 0.f, depthStencilRT->GetWidth_f(), depthStencilRT->GetHeight_f());
                cmd->SetScissor(0, 0, depthStencilRT->GetWidth(), depthStencilRT->GetHeight());
                cmd->BindPipeline(m_passInfoOpaque);

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

                    cmd->SetConstant(0, constants);
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
        else // Alpha pass
        {
            if (m_geometry->HasAlphaDrawInfo())
            {
                PassBarriers(cmd);

                cmd->BeginPass(m_attachmentsTransparent, "GBuffers_Transparent");
                cmd->BindIndexBuffer(m_geometry->GetBuffer(), 0);
                cmd->BindVertexBuffer(m_geometry->GetBuffer(), m_geometry->GetVerticesOffset());
                cmd->SetViewport(0.f, 0.f, depthStencilRT->GetWidth_f(), depthStencilRT->GetHeight_f());
                cmd->SetScissor(0, 0, depthStencilRT->GetWidth(), depthStencilRT->GetHeight());
                cmd->BindPipeline(m_passInfoAlpha);

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

                    cmd->SetConstant(0, constants);
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

                    cmd->SetConstant(0, constants);
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
        cmd->EndDebugRegion();
        cmd->End();

        m_geometry = nullptr;
        m_blendType = BlendType::None;

        return cmd;
    }

    void GbufferPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void GbufferPass::ClearRenderTargets(CommandBuffer *cmd)
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

    void GbufferPass::ClearDepthStencil(CommandBuffer *cmd)
    {
        cmd->ClearDepthStencils({depthStencilRT});
    }

    void GbufferPass::Destroy()
    {
        Buffer::Destroy(uniform);
    }
}
#endif
