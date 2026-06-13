#include "GbufferPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Scene/PassInfoAsset.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Render/SceneRendererHost.h"

namespace pe
{
    void GbufferOpaquePass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

        m_srmRT = rs->GetRenderTarget("srm"); // Specular Roughness Metallic
        m_normalRT = rs->GetRenderTarget("normal");
        m_albedoRT = rs->GetRenderTarget("albedo");
        m_velocityRT = rs->GetRenderTarget("velocity");
        m_emissiveRT = rs->GetRenderTarget("emissive");
        m_viewportRT = rs->GetRenderTarget("viewport");
        m_transparencyRT = rs->GetRenderTarget("transparency");
        m_depthStencilRT = rs->GetDepthStencilTarget("depthStencil");

        m_attachments.resize(7);
        for (int i = 0; i < 7; i++)
        {
            m_attachments[i] = {};
        }
        m_attachments[0].image = m_normalRT;
        m_attachments[1].image = m_albedoRT;
        m_attachments[2].image = m_srmRT;
        m_attachments[3].image = m_velocityRT;
        m_attachments[4].image = m_emissiveRT;
        m_attachments[5].image = m_transparencyRT;

        m_attachments[6].image = m_depthStencilRT;
        m_attachments[6].loadOp = PE_LOAD_OP_LOAD;

        if (!m_passInfoDS)
            m_passInfoDS = std::make_shared<PassInfo>();
        m_scene = nullptr;
    }

    void GbufferOpaquePass::UpdatePassInfo()
    {
        if (!m_passAsset)
            m_passAsset = ResourceManager::Get().Load<PassInfoAsset>(Path::RuntimeAssets + "PassInfo/standard_pbr.pass");

        std::vector<::PeFormat> colorformats{
            m_normalRT->GetFormat(),
            m_albedoRT->GetFormat(),
            m_srmRT->GetFormat(),
            m_velocityRT->GetFormat(),
            m_emissiveRT->GetFormat(),
            m_transparencyRT->GetFormat()};

        ::PeFormat depthFormat = RHII.GetDepthFormat();

        const PassVariant *surface = m_passAsset->GetVariant("surface");
        PE_ERROR_IF(!surface, "standard_pbr.pass missing 'surface' variant");

        m_passInfo->name = "gbuffer_opaque_pipeline";
        m_passInfo->Apply(*surface);
        m_passInfo->colorFormats = colorformats;
        m_passInfo->depthFormat = depthFormat;
        m_passInfo->Update();

        Shader *oldDSVert = m_passInfoDS->pVertShader;
        Shader *oldDSFrag = m_passInfoDS->pFragShader;
        m_passInfoDS->name = "gbuffer_opaque_ds_pipeline";
        try
        {
            m_passInfoDS->Apply(*surface);
            m_passInfoDS->cullMode = PE_CULL_MODE_NONE;
            m_passInfoDS->colorFormats = colorformats;
            m_passInfoDS->depthFormat = depthFormat;
            m_passInfoDS->Update();
        }
        catch (...)
        {
            if (m_passInfoDS->pVertShader != oldDSVert)
                Shader::Destroy(m_passInfoDS->pVertShader);
            if (m_passInfoDS->pFragShader != oldDSFrag)
                Shader::Destroy(m_passInfoDS->pFragShader);
            m_passInfoDS->pVertShader = oldDSVert;
            m_passInfoDS->pFragShader = oldDSFrag;
            throw;
        }
    }

    void GbufferOpaquePass::Update()
    {
        Scene &scene = *GetActiveScene();
        uint32_t frame = RHII.GetFrameIndex();

        uint64_t geoVersion = scene.GetGeometryVersion();
        if (geoVersion != m_lastGeometryVersion)
        {
            m_lastGeometryVersion = geoVersion;

            // Update ALL frames' texture descriptors since buffers changed
            for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
            {
                const auto &sets = m_passInfo->GetDescriptors(i);
                Descriptor *setTextures = sets[1];
                setTextures->SetBuffer(0, scene.GetMeshConstants());
                setTextures->SetSampler(1, scene.GetDefaultSampler());
                setTextures->SetBuffer(2, scene.GetMaterialTable());
                setTextures->SetBuffer(3, scene.GetMaterialByteBuffer());
                setTextures->SetImageViews(4, scene.GetImageViews());
                setTextures->Update();
            }
        }

        if (scene.GetMeshCount() > 0)
        {
            const auto &sets = m_passInfo->GetDescriptors(frame);
            Descriptor *setUniforms = sets[0];
            setUniforms->SetBuffer(0, scene.GetUniforms(frame));
            setUniforms->SetBuffer(1, scene.GetMeshConstants());
            setUniforms->Update();
        }
    }

    void GbufferOpaquePass::PassBarriers(CommandBuffer *cmd)
    {
    }

    void GbufferOpaquePass::ExecutePass(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_scene == nullptr, "Scene was not set");

        Camera *camera = GetActiveScene()->GetActiveCamera();

        if (m_scene->GetMeshCount() == 0)
        {
            // Just clear the render targets
            ClearRenderTargets(cmd);
        }
        else
        {
            PushConstants_GBuffer pushConstants{};
            pushConstants.jointsCount = static_cast<uint32_t>(m_scene->GetMaxJointCount());
            pushConstants.projJitter = camera->GetProjJitter();
            pushConstants.prevProjJitter = camera->GetPrevProjJitter();
            pushConstants.passType = 0u;

            uint32_t frame = RHII.GetFrameIndex();

            cmd->BeginPass(7, m_attachments.data(), "GbufferOpaquePass");
            cmd->SetViewport(0.f, 0.f, m_depthStencilRT->GetWidth_f(), m_depthStencilRT->GetHeight_f());
            cmd->SetScissor(0, 0, m_depthStencilRT->GetWidth(), m_depthStencilRT->GetHeight());

            cmd->BindPipeline(*m_passInfo);
            cmd->BindIndexBuffer(m_scene->GetBuffer(), 0);
            cmd->BindVertexBuffer(m_scene->GetBuffer(), m_scene->GetVerticesOffset());
            cmd->SetConstants(pushConstants);
            cmd->PushConstants();
            cmd->DrawIndexedIndirectCount(m_scene->GetIndirectOpaqueSS(frame), 0, m_scene->GetCullingCountersBuffer(frame), 0 * sizeof(uint32_t), m_scene->GetMeshCount());
            cmd->DrawIndexedIndirectCount(m_scene->GetIndirectAlphaCutSS(frame), 0, m_scene->GetCullingCountersBuffer(frame), 1 * sizeof(uint32_t), m_scene->GetMeshCount());

            cmd->BindPipeline(*m_passInfoDS, false);
            cmd->DrawIndexedIndirectCount(m_scene->GetIndirectOpaqueDS(frame), 0, m_scene->GetCullingCountersBuffer(frame), 5 * sizeof(uint32_t), m_scene->GetMeshCount());
            cmd->DrawIndexedIndirectCount(m_scene->GetIndirectAlphaCutDS(frame), 0, m_scene->GetCullingCountersBuffer(frame), 6 * sizeof(uint32_t), m_scene->GetMeshCount());

            cmd->EndPass();
        }

        m_scene = nullptr;
    }

    void GbufferOpaquePass::Resize(uint32_t width, uint32_t height)
    {
        Init();
    }

    void GbufferOpaquePass::ClearRenderTargets(CommandBuffer *cmd)
    {
        std::vector<Image *> colorTargets{m_normalRT, m_albedoRT, m_srmRT, m_velocityRT, m_emissiveRT, m_viewportRT, m_transparencyRT};
        cmd->ClearColors(colorTargets);
    }

    void GbufferOpaquePass::ClearDepthStencil(CommandBuffer *cmd)
    {
        cmd->ClearDepthStencils({m_depthStencilRT});
    }

    void GbufferOpaquePass::Destroy()
    {
        if (!m_passInfoDS)
            return;

        Shader::Destroy(m_passInfoDS->pVertShader);
        Shader::Destroy(m_passInfoDS->pFragShader);
        m_passInfoDS.reset();
    }

    void GbufferTransparentPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

        m_srmRT = rs->GetRenderTarget("srm"); // Specular Roughness Metallic
        m_normalRT = rs->GetRenderTarget("normal");
        m_albedoRT = rs->GetRenderTarget("albedo");
        m_velocityRT = rs->GetRenderTarget("velocity");
        m_emissiveRT = rs->GetRenderTarget("emissive");
        m_viewportRT = rs->GetRenderTarget("viewport");
        m_transparencyRT = rs->GetRenderTarget("transparency");
        m_depthStencilRT = rs->GetDepthStencilTarget("depthStencil");

        m_attachments.resize(7);
        for (int i = 0; i < 7; i++)
        {
            m_attachments[i] = {};
            m_attachments[i].loadOp = PE_LOAD_OP_LOAD;
        }
        m_attachments[0].image = m_normalRT;
        m_attachments[1].image = m_albedoRT;
        m_attachments[2].image = m_srmRT;
        m_attachments[3].image = m_velocityRT;
        m_attachments[4].image = m_emissiveRT;
        m_attachments[5].image = m_transparencyRT;
        m_attachments[6].image = m_depthStencilRT;

        m_scene = nullptr;
    }

    void GbufferTransparentPass::UpdatePassInfo()
    {
        if (!m_passAsset)
            m_passAsset = ResourceManager::Get().Load<PassInfoAsset>(Path::RuntimeAssets + "PassInfo/standard_pbr.pass");

        std::vector<::PeFormat> colorformats{
            m_normalRT->GetFormat(),
            m_albedoRT->GetFormat(),
            m_srmRT->GetFormat(),
            m_velocityRT->GetFormat(),
            m_emissiveRT->GetFormat(),
            m_transparencyRT->GetFormat()};

        ::PeFormat depthFormat = RHII.GetDepthFormat();

        const PassVariant *transparent = m_passAsset->GetVariant("transparent");
        PE_ERROR_IF(!transparent, "standard_pbr.pass missing 'transparent' variant");

        m_passInfo->name = "gbuffer_transparent_pipeline";
        m_passInfo->Apply(*transparent);
        m_passInfo->colorFormats = colorformats;
        m_passInfo->depthFormat = depthFormat;
        if (Settings::Get<GlobalSettings>().reverse_depth)
            m_passInfo->depthCompareOp = PE_COMPARE_OP_GREATER_OR_EQUAL;
        m_passInfo->Update();
    }

    void GbufferTransparentPass::Update()
    {
        Scene &scene = *GetActiveScene();
        uint32_t frame = RHII.GetFrameIndex();

        uint64_t geoVersion = scene.GetGeometryVersion();
        if (geoVersion != m_lastGeometryVersion)
        {
            m_lastGeometryVersion = geoVersion;

            // Update ALL frames' texture descriptors since buffers changed
            for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
            {
                const auto &sets = m_passInfo->GetDescriptors(i);
                Descriptor *setTextures = sets[1];
                setTextures->SetBuffer(0, scene.GetMeshConstants());
                setTextures->SetSampler(1, scene.GetDefaultSampler());
                setTextures->SetBuffer(2, scene.GetMaterialTable());
                setTextures->SetBuffer(3, scene.GetMaterialByteBuffer());
                setTextures->SetImageViews(4, scene.GetImageViews());
                setTextures->Update();
            }
        }

        if (scene.GetMeshCount() > 0)
        {
            const auto &sets = m_passInfo->GetDescriptors(frame);
            Descriptor *setUniforms = sets[0];
            setUniforms->SetBuffer(0, scene.GetUniforms(frame));
            setUniforms->SetBuffer(1, scene.GetMeshConstants());
            setUniforms->Update();
        }
    }

    void GbufferTransparentPass::PassBarriers(CommandBuffer *cmd)
    {
    }

    void GbufferTransparentPass::ExecutePass(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_scene == nullptr, "Scene was not set");

        const bool hasAlphaBlendMeshes = m_scene->HasAlphaBlendMeshes();
        const bool hasTransmissionMeshes = m_scene->HasTransmissionMeshes();
        if (m_scene->GetMeshCount() == 0 || (!hasAlphaBlendMeshes && !hasTransmissionMeshes))
        {
            m_scene = nullptr;
            return;
        }

        Camera *camera = GetActiveScene()->GetActiveCamera();

        PushConstants_GBuffer pushConstants{};
        pushConstants.jointsCount = static_cast<uint32_t>(m_scene->GetMaxJointCount());
        pushConstants.projJitter = camera->GetProjJitter();
        pushConstants.prevProjJitter = camera->GetPrevProjJitter();
        pushConstants.passType = 1u;
        uint32_t frame = RHII.GetFrameIndex();

        if (hasAlphaBlendMeshes)
        {
            cmd->BeginPass(7, m_attachments.data(), "GbufferTransparentPass_AlphaBlend");
            cmd->SetViewport(0.f, 0.f, m_depthStencilRT->GetWidth_f(), m_depthStencilRT->GetHeight_f());
            cmd->SetScissor(0, 0, m_depthStencilRT->GetWidth(), m_depthStencilRT->GetHeight());
            cmd->BindPipeline(*m_passInfo);
            cmd->BindIndexBuffer(m_scene->GetBuffer(), 0);
            cmd->BindVertexBuffer(m_scene->GetBuffer(), m_scene->GetVerticesOffset());
            cmd->SetConstants(pushConstants);
            cmd->PushConstants();
            cmd->DrawIndexedIndirectCount(m_scene->GetIndirectAlphaBlend(frame), 0, m_scene->GetCullingCountersBuffer(frame), 2 * sizeof(uint32_t), m_scene->GetMeshCount());
            cmd->EndPass();
        }

        if (hasTransmissionMeshes)
        {
            pushConstants.passType = 2u;
            cmd->BeginPass(7, m_attachments.data(), "GbufferTransparentPass_Transmission");
            cmd->SetViewport(0.f, 0.f, m_depthStencilRT->GetWidth_f(), m_depthStencilRT->GetHeight_f());
            cmd->SetScissor(0, 0, m_depthStencilRT->GetWidth(), m_depthStencilRT->GetHeight());
            cmd->BindPipeline(*m_passInfo);
            cmd->BindIndexBuffer(m_scene->GetBuffer(), 0);
            cmd->BindVertexBuffer(m_scene->GetBuffer(), m_scene->GetVerticesOffset());
            cmd->SetConstants(pushConstants);
            cmd->PushConstants();
            cmd->DrawIndexedIndirectCount(m_scene->GetIndirectTransmission(frame), 0, m_scene->GetCullingCountersBuffer(frame), 3 * sizeof(uint32_t), m_scene->GetMeshCount());
            cmd->EndPass();
        }

        m_scene = nullptr;
    }

    void GbufferTransparentPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
    }

    void GbufferTransparentPass::ClearRenderTargets(CommandBuffer *cmd)
    {
        std::vector<Image *> colorTargets{m_normalRT, m_albedoRT, m_srmRT, m_velocityRT, m_emissiveRT, m_viewportRT, m_transparencyRT};
        cmd->ClearColors(colorTargets);
    }

    void GbufferTransparentPass::ClearDepthStencil(CommandBuffer *cmd)
    {
        cmd->ClearDepthStencils({m_depthStencilRT});
    }

    void GbufferTransparentPass::Destroy()
    {
    }
} // namespace pe
