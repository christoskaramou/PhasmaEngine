#include "GbufferPass.h"
#include "API/Buffer.h"
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
        if (!m_voxelPassInfo)
            m_voxelPassInfo = std::make_shared<PassInfo>();
        m_lastVoxelAtlasView = nullptr;
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

        Shader *oldVoxelVert = m_voxelPassInfo->pVertShader;
        Shader *oldVoxelFrag = m_voxelPassInfo->pFragShader;
        auto restoreVoxelShaders = [&]()
        {
            if (m_voxelPassInfo->pVertShader != oldVoxelVert)
                Shader::Destroy(m_voxelPassInfo->pVertShader);
            if (m_voxelPassInfo->pFragShader != oldVoxelFrag)
                Shader::Destroy(m_voxelPassInfo->pFragShader);
            m_voxelPassInfo->pVertShader = oldVoxelVert;
            m_voxelPassInfo->pFragShader = oldVoxelFrag;
        };

        try
        {
            if (!m_voxelPassAsset)
                m_voxelPassAsset = ResourceManager::Get().Load<PassInfoAsset>(Path::RuntimeAssets + "Shaders/Voxel/voxel_gbuffer.passinfo");

            const PassVariant *voxelSurface = m_voxelPassAsset ? m_voxelPassAsset->GetVariant("surface") : nullptr;
            if (!voxelSurface)
            {
                PE_WARN("voxel_gbuffer.passinfo missing 'surface' variant");
            }
            else
            {
                m_voxelPassInfo->name = "gbuffer_voxel_pipeline";
                m_voxelPassInfo->Apply(*voxelSurface);
                m_voxelPassInfo->colorFormats = colorformats;
                m_voxelPassInfo->depthFormat = depthFormat;
                m_voxelPassInfo->Update();
            }
        }
        catch (const std::exception &e)
        {
            restoreVoxelShaders();
            PE_WARN("GbufferOpaquePass voxel pipeline update failed: %s", e.what());
        }
        catch (...)
        {
            restoreVoxelShaders();
            PE_WARN("GbufferOpaquePass voxel pipeline update failed");
        }
    }

    void GbufferOpaquePass::Update()
    {
        Scene &scene = *GetActiveScene();
        uint32_t frame = RHII.GetFrameIndex();
        const bool voxelPipelineReady = m_voxelPassInfo && m_voxelPassInfo->pFragShader;

        auto updateVoxelAtlasDescriptors = [&](ImageView *atlasView)
        {
            if (!atlasView)
            {
                m_lastVoxelAtlasView = nullptr;
                return;
            }
            if (!voxelPipelineReady)
                return;

            for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
            {
                const auto &sets = m_voxelPassInfo->GetDescriptors(i);
                if (sets.size() <= 1)
                    continue;

                Descriptor *setAtlas = sets[1];
                setAtlas->SetSampler(0, scene.GetDefaultSampler());
                setAtlas->SetImageView(1, atlasView);
                setAtlas->Update();
            }

            m_lastVoxelAtlasView = atlasView;
        };

        uint64_t geoVersion = scene.GetGeometryVersion();
        if (geoVersion != m_lastGeometryVersion)
        {
            m_lastGeometryVersion = geoVersion;

            // Update ALL frames' texture descriptors since buffers changed
            for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
            {
                for (PassInfo *passInfo : {m_passInfo.get(), m_passInfoDS.get()})
                {
                    if (!passInfo)
                        continue;

                    const auto &sets = passInfo->GetDescriptors(i);
                    Descriptor *setTextures = sets[1];
                    setTextures->SetBuffer(0, scene.GetMeshConstants());
                    setTextures->SetSampler(1, scene.GetDefaultSampler());
                    setTextures->SetBuffer(2, scene.GetMaterialTable());
                    setTextures->SetBuffer(3, scene.GetMaterialByteBuffer());
                    setTextures->SetImageViews(4, scene.GetImageViews());
                    setTextures->Update();
                }
            }

            updateVoxelAtlasDescriptors(scene.GetVoxelAtlasView());
        }

        if (scene.HasArenaVoxels() && scene.GetVoxelAtlasView() != m_lastVoxelAtlasView)
            updateVoxelAtlasDescriptors(scene.GetVoxelAtlasView());

        if (scene.GetMeshCount() > 0)
        {
            for (PassInfo *passInfo : {m_passInfo.get(), m_passInfoDS.get()})
            {
                if (!passInfo)
                    continue;

                const auto &sets = passInfo->GetDescriptors(frame);
                Descriptor *setUniforms = sets[0];
                setUniforms->SetBuffer(0, scene.GetUniforms(frame));
                setUniforms->SetBuffer(1, scene.GetMeshConstants());
                setUniforms->Update();
            }

            if (voxelPipelineReady)
            {
                const auto &sets = m_voxelPassInfo->GetDescriptors(frame);
                if (!sets.empty())
                {
                    Descriptor *setUniforms = sets[0];
                    setUniforms->SetBuffer(0, scene.GetUniforms(frame));
                    setUniforms->SetBuffer(1, scene.GetMeshConstants());
                    setUniforms->Update();
                }
            }
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
            const uint32_t mesh = m_scene->GetMeshCount();

            // Two-phase Hi-Z: when occlusion culling is on, draw set A (CullPhase1@180) then set B
            // (CullPhase2@260). Their depth was written by DepthPass@200 (A) and DepthLatePass@270
            // (B), so the reverse-Z EQUAL test selects the nearest surface per pixel for both sets.
            // Occluded objects are in neither set, so they are never drawn. Otherwise draw the
            // frustum-culled set from CullingPass@50.
            const bool occlusion = Settings::Get<SceneSettings>().occlusion_culling;
            const bool hasArenaVoxels = m_scene->HasArenaVoxels();
            const bool hasVoxelAtlas = m_scene->GetVoxelAtlasView() != nullptr;
            const bool hasVoxelFrag = m_voxelPassInfo && m_voxelPassInfo->pFragShader;
            Buffer *voxelIndirect = m_scene->GetIndirectVoxels(frame);
            const bool hasVoxelIndirect = voxelIndirect != nullptr;
            const bool hasVoxelGeometry = m_scene->GetVoxelVertexBuffer() && m_scene->GetVoxelIndexBuffer();
            const bool voxelDrawReady =
                hasArenaVoxels && hasVoxelAtlas && hasVoxelFrag && hasVoxelIndirect && hasVoxelGeometry;

            cmd->BeginPass(7, m_attachments.data(), "GbufferOpaquePass");
            cmd->SetViewport(0.f, 0.f, m_depthStencilRT->GetWidth_f(), m_depthStencilRT->GetHeight_f());
            cmd->SetScissor(0, 0, m_depthStencilRT->GetWidth(), m_depthStencilRT->GetHeight());

            cmd->BindPipeline(*m_passInfo);
            cmd->BindIndexBuffer(m_scene->GetBuffer(), 0);
            cmd->BindVertexBuffer(m_scene->GetBuffer(), m_scene->GetVerticesOffset());
            cmd->SetConstants(pushConstants);
            cmd->PushConstants();
            if (occlusion)
            {
                cmd->DrawIndexedIndirectCount(m_scene->GetOccOpaqueSSA(frame), 0, m_scene->GetOccCountersA(frame), 0 * sizeof(uint32_t), mesh);
                cmd->DrawIndexedIndirectCount(m_scene->GetOccAlphaCutSSA(frame), 0, m_scene->GetOccCountersA(frame), 1 * sizeof(uint32_t), mesh);
                cmd->DrawIndexedIndirectCount(m_scene->GetOccOpaqueSSB(frame), 0, m_scene->GetOccCountersB(frame), 0 * sizeof(uint32_t), mesh);
                cmd->DrawIndexedIndirectCount(m_scene->GetOccAlphaCutSSB(frame), 0, m_scene->GetOccCountersB(frame), 1 * sizeof(uint32_t), mesh);

                cmd->BindPipeline(*m_passInfoDS);
                cmd->DrawIndexedIndirectCount(m_scene->GetOccOpaqueDSA(frame), 0, m_scene->GetOccCountersA(frame), 5 * sizeof(uint32_t), mesh);
                cmd->DrawIndexedIndirectCount(m_scene->GetOccAlphaCutDSA(frame), 0, m_scene->GetOccCountersA(frame), 6 * sizeof(uint32_t), mesh);
                cmd->DrawIndexedIndirectCount(m_scene->GetOccOpaqueDSB(frame), 0, m_scene->GetOccCountersB(frame), 5 * sizeof(uint32_t), mesh);
                cmd->DrawIndexedIndirectCount(m_scene->GetOccAlphaCutDSB(frame), 0, m_scene->GetOccCountersB(frame), 6 * sizeof(uint32_t), mesh);
            }
            else
            {
                cmd->DrawIndexedIndirectCount(m_scene->GetIndirectOpaqueSS(frame), 0, m_scene->GetCullingCountersBuffer(frame), 0 * sizeof(uint32_t), mesh);
                cmd->DrawIndexedIndirectCount(m_scene->GetIndirectAlphaCutSS(frame), 0, m_scene->GetCullingCountersBuffer(frame), 1 * sizeof(uint32_t), mesh);

                cmd->BindPipeline(*m_passInfoDS);
                cmd->DrawIndexedIndirectCount(m_scene->GetIndirectOpaqueDS(frame), 0, m_scene->GetCullingCountersBuffer(frame), 5 * sizeof(uint32_t), mesh);
                cmd->DrawIndexedIndirectCount(m_scene->GetIndirectAlphaCutDS(frame), 0, m_scene->GetCullingCountersBuffer(frame), 6 * sizeof(uint32_t), mesh);
            }

            if (voxelDrawReady)
            {
                cmd->BindPipeline(*m_voxelPassInfo);
                cmd->BindIndexBuffer(m_scene->GetVoxelIndexBuffer(), 0, PE_INDEX_TYPE_UINT16);
                cmd->BindVertexBuffer(m_scene->GetVoxelVertexBuffer(), 0);
                cmd->SetConstants(pushConstants);
                cmd->PushConstants();

                cmd->DrawIndexedIndirectCount(voxelIndirect,
                                              0,
                                              m_scene->GetCullingCountersBuffer(frame),
                                              7 * sizeof(uint32_t),
                                              mesh);
            }

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
        if (m_passInfoDS)
        {
            Shader::Destroy(m_passInfoDS->pVertShader);
            Shader::Destroy(m_passInfoDS->pFragShader);
            m_passInfoDS.reset();
        }

        if (m_voxelPassInfo)
        {
            Shader::Destroy(m_voxelPassInfo->pVertShader);
            Shader::Destroy(m_voxelPassInfo->pFragShader);
            m_voxelPassInfo.reset();
        }
        m_lastVoxelAtlasView = nullptr;
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

        if (!m_voxelPassInfo)
            m_voxelPassInfo = std::make_shared<PassInfo>();
        m_lastVoxelAtlasView = nullptr;
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
        if (Settings::Get<SceneSettings>().reverse_depth)
            m_passInfo->depthCompareOp = PE_COMPARE_OP_GREATER_OR_EQUAL;
        m_passInfo->Update();

        // Voxel water pipeline: the transparent variant of voxel_gbuffer (blend on, transparency MRT).
        // depthCompareOp is left unset so it inherits the reverse-Z default (an explicit value here once
        // made opaque voxels invisible under reverse-Z — see voxel core-engine notes). Failure to build
        // (e.g. shader edit) is non-fatal: the standard transparent draw above still works.
        Shader *oldVoxelVert = m_voxelPassInfo->pVertShader;
        Shader *oldVoxelFrag = m_voxelPassInfo->pFragShader;
        auto restoreVoxelShaders = [&]()
        {
            if (m_voxelPassInfo->pVertShader != oldVoxelVert)
                Shader::Destroy(m_voxelPassInfo->pVertShader);
            if (m_voxelPassInfo->pFragShader != oldVoxelFrag)
                Shader::Destroy(m_voxelPassInfo->pFragShader);
            m_voxelPassInfo->pVertShader = oldVoxelVert;
            m_voxelPassInfo->pFragShader = oldVoxelFrag;
        };
        try
        {
            if (!m_voxelPassAsset)
                m_voxelPassAsset = ResourceManager::Get().Load<PassInfoAsset>(Path::RuntimeAssets + "Shaders/Voxel/voxel_gbuffer.passinfo");
            const PassVariant *voxelTransparent = m_voxelPassAsset ? m_voxelPassAsset->GetVariant("transparent") : nullptr;
            if (!voxelTransparent)
            {
                PE_WARN("voxel_gbuffer.passinfo missing 'transparent' variant");
            }
            else
            {
                m_voxelPassInfo->name = "gbuffer_voxel_transparent_pipeline";
                m_voxelPassInfo->Apply(*voxelTransparent);
                m_voxelPassInfo->colorFormats = colorformats;
                m_voxelPassInfo->depthFormat = depthFormat;
                m_voxelPassInfo->Update();
            }
        }
        catch (const std::exception &e)
        {
            restoreVoxelShaders();
            PE_WARN("GbufferTransparentPass voxel pipeline update failed: %s", e.what());
        }
        catch (...)
        {
            restoreVoxelShaders();
            PE_WARN("GbufferTransparentPass voxel pipeline update failed");
        }
    }

    void GbufferTransparentPass::Update()
    {
        Scene &scene = *GetActiveScene();
        uint32_t frame = RHII.GetFrameIndex();
        const bool voxelPipelineReady = m_voxelPassInfo && m_voxelPassInfo->pFragShader;

        // set1 of the voxel water pipeline = sampler s0 + Texture2DArray atlas t1 (same as opaque voxel).
        auto updateVoxelAtlasDescriptors = [&](ImageView *atlasView)
        {
            if (!atlasView)
            {
                m_lastVoxelAtlasView = nullptr;
                return;
            }
            if (!voxelPipelineReady)
                return;

            for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
            {
                const auto &sets = m_voxelPassInfo->GetDescriptors(i);
                if (sets.size() <= 1)
                    continue;
                Descriptor *setAtlas = sets[1];
                setAtlas->SetSampler(0, scene.GetDefaultSampler());
                setAtlas->SetImageView(1, atlasView);
                setAtlas->Update();
            }
            m_lastVoxelAtlasView = atlasView;
        };

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

            updateVoxelAtlasDescriptors(scene.GetVoxelAtlasView());
        }

        if (scene.HasArenaVoxels() && scene.GetVoxelAtlasView() != m_lastVoxelAtlasView)
            updateVoxelAtlasDescriptors(scene.GetVoxelAtlasView());

        if (scene.GetMeshCount() > 0)
        {
            const auto &sets = m_passInfo->GetDescriptors(frame);
            Descriptor *setUniforms = sets[0];
            setUniforms->SetBuffer(0, scene.GetUniforms(frame));
            setUniforms->SetBuffer(1, scene.GetMeshConstants());
            setUniforms->Update();

            if (voxelPipelineReady)
            {
                const auto &vsets = m_voxelPassInfo->GetDescriptors(frame);
                if (!vsets.empty())
                {
                    Descriptor *setVoxelUniforms = vsets[0];
                    setVoxelUniforms->SetBuffer(0, scene.GetUniforms(frame));
                    setVoxelUniforms->SetBuffer(1, scene.GetMeshConstants());
                    setVoxelUniforms->Update();
                }
            }
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

        const bool voxelReady = m_voxelPassInfo && m_voxelPassInfo->pFragShader &&
                                m_scene->HasArenaVoxels() && m_scene->GetVoxelAtlasView() &&
                                m_scene->GetVoxelVertexBuffer() && m_scene->GetVoxelIndexBuffer();
        std::vector<Scene::VoxelTransparentDraw> voxelDraws;
        if (voxelReady)
            voxelDraws = m_scene->GetVoxelTransparentDraws();
        const bool hasVoxelWater = !voxelDraws.empty();

        if (m_scene->GetMeshCount() == 0 || (!hasAlphaBlendMeshes && !hasTransmissionMeshes && !hasVoxelWater))
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

        if (hasVoxelWater)
        {
            // Voxel water: dedicated voxel pipeline + buffers, drawn UNCULLED per section (water is
            // sparse). firstInstance = slot drives the voxel VS's identity-host transform + section-origin
            // AABB; passType=1 makes the voxel PS write the transparency mask so LightTransparent composites
            // it like standard transparent meshes. ponytail: route through a cull bucket if water coverage
            // ever grows enough to matter.
            pushConstants.passType = 1u;
            cmd->BeginPass(7, m_attachments.data(), "GbufferTransparentPass_VoxelWater");
            cmd->SetViewport(0.f, 0.f, m_depthStencilRT->GetWidth_f(), m_depthStencilRT->GetHeight_f());
            cmd->SetScissor(0, 0, m_depthStencilRT->GetWidth(), m_depthStencilRT->GetHeight());
            cmd->BindPipeline(*m_voxelPassInfo);
            cmd->BindIndexBuffer(m_scene->GetVoxelIndexBuffer(), 0, PE_INDEX_TYPE_UINT16);
            cmd->BindVertexBuffer(m_scene->GetVoxelVertexBuffer(), 0);
            cmd->SetConstants(pushConstants);
            cmd->PushConstants();
            for (const auto &d : voxelDraws)
                cmd->DrawIndexed(d.indexCount, 1, d.firstIndex, d.vertexOffset, d.slot);
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
        if (m_voxelPassInfo)
        {
            Shader::Destroy(m_voxelPassInfo->pVertShader);
            Shader::Destroy(m_voxelPassInfo->pFragShader);
            m_voxelPassInfo.reset();
        }
        m_lastVoxelAtlasView = nullptr;
    }
} // namespace pe
