#include "DepthPass.h"
#include "API/Command.h"
#include "Camera/Camera.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Render/SceneRendererHost.h"
#include "Render/SceneRenderTargets.h"

namespace pe
{
    namespace
    {
        Cutaway s_cutaway;

        // One end of the capsule: its pixel, the depth a little in front of it and its radius in pixels.
        vec4 ProjectCutawayEnd(const Camera &camera, const vec3 &point, float radius, float width, float height)
        {
            const mat4 viewProjection = camera.GetViewProjection();
            const vec4 clip = viewProjection * vec4(point, 1.0f);
            if (clip.w <= 0.0f)
                return vec4(0.0f);

            // ponytail: the front is a fixed 0.15 units toward the camera, so a mesh level with the capsule stays
            // whole; make it a Cutaway field when a caller needs another margin.
            const vec3 toCamera = normalize(camera.GetPosition() - point);
            const vec4 front = viewProjection * vec4(point + toCamera * 0.15f, 1.0f);
            const vec4 side = viewProjection * vec4(point + camera.GetRight() * radius, 1.0f);
            const vec2 size(width, height);
            const vec2 pixel = (vec2(clip) / clip.w * 0.5f + 0.5f) * size;
            const vec2 sidePixel = (vec2(side) / side.w * 0.5f + 0.5f) * size;
            return vec4(pixel, front.z / front.w, length(sidePixel - pixel));
        }
    } // namespace

    void SetCutaway(const Cutaway &cutaway)
    {
        s_cutaway = cutaway;
    }

    CutawayConstants GetCutawayConstants(float width, float height)
    {
        CutawayConstants constants;
        Scene *scene = GetActiveScene();
        const bool on = s_cutaway.radiusA > 0.0f || s_cutaway.radiusB > 0.0f;
        if (!on || !scene || scene->GetCameras().empty())
            return constants;

        const Camera &camera = *scene->GetActiveCamera();
        const vec4 a = ProjectCutawayEnd(camera, s_cutaway.a, s_cutaway.radiusA, width, height);
        const vec4 b = ProjectCutawayEnd(camera, s_cutaway.b, s_cutaway.radiusB, width, height);
        if (a.w <= 0.0f && b.w <= 0.0f)
            return constants;

        // The feather keeps its world width at the capsule's depth; the dither cell grows with the target so the
        // pattern reads the same at 1080p and 4K.
        const float pixelsPerUnit = std::max(a.w, b.w) / std::max(s_cutaway.radiusA, s_cutaway.radiusB);
        const float cell = std::max(1.0f, std::round(height / 1080.0f));
        constants.a = a;
        constants.b = b;
        constants.params = vec4(s_cutaway.feather * pixelsPerUnit,
                                1.0f / cell,
                                Settings::Get<SceneSettings>().reverse_depth ? 1.0f : -1.0f,
                                0.0f);
        return constants;
    }

    PushConstants_DepthPass MakeDepthPassConstants(const Scene &scene, const Image &depth)
    {
        PushConstants_DepthPass constants;
        constants.jointsCount = static_cast<uint32_t>(scene.GetMaxJointCount());
        constants.cutaway = GetCutawayConstants(depth.GetWidth_f(), depth.GetHeight_f());
        return constants;
    }

    BlendState VisibilityBlendState(bool write)
    {
        return BlendState(false, PE_BLEND_FACTOR_ONE, PE_BLEND_FACTOR_ZERO, PE_BLEND_OP_ADD, PE_BLEND_FACTOR_ONE,
                          PE_BLEND_FACTOR_ZERO, PE_BLEND_OP_ADD,
                          write ? PE_COLOR_COMPONENT_RGBA : static_cast<PeColorComponentFlags>(0));
    }

    void DepthPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_depthStencil = rs->GetDepthStencilTarget("depthStencil");
        m_visibility = SceneUsesOpenPBR() ? rs->GetRenderTarget("visibility") : nullptr;

        m_attachments.clear();
        if (m_visibility)
        {
            Attachment visibility{};
            visibility.image = m_visibility;
            visibility.loadOp = PE_LOAD_OP_CLEAR;
            visibility.storeOp = PE_STORE_OP_STORE;
            m_attachments.push_back(visibility);
        }

        Attachment depth{};
        depth.image = m_depthStencil;
        depth.loadOp = PE_LOAD_OP_CLEAR;
        depth.storeOp = PE_STORE_OP_STORE;
        m_attachments.push_back(depth);

        if (!m_passInfoDS)
            m_passInfoDS = std::make_shared<PassInfo>();
    }

    void DepthPass::UpdatePassInfo()
    {
        const bool visibility = m_visibility != nullptr;
        auto configureDepthPass = [visibility](PassInfo &passInfo, const std::string &name, PeCullMode cullMode)
        {
            const std::vector<Define> defines = visibility ? std::vector<Define>{Define{"VISIBILITY", "1"}}
                                                           : std::vector<Define>{};
            const std::string fragmentSource = visibility ? "Shaders/Depth/VisibilityPS.hlsl" : "Shaders/Depth/DepthPS.hlsl";

            passInfo.name = name;
            passInfo.pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Depth/DepthVS.hlsl",
                                                   .entryPoint = "mainVS",
                                                   .stage = PE_SHADER_STAGE_VERTEX,
                                                   .defines = defines});
            passInfo.pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + fragmentSource,
                                                   .entryPoint = "mainPS",
                                                   .stage = PE_SHADER_STAGE_FRAGMENT,
                                                   .defines = defines});
            passInfo.dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
            passInfo.cullMode = cullMode;

            passInfo.colorFormats = visibility ? std::vector<::PeFormat>{PE_FORMAT_R32G32B32A32_UINT}
                                               : std::vector<::PeFormat>{};
            passInfo.colorBlendAttachments = visibility ? std::vector<BlendState>{VisibilityBlendState(true)}
                                                        : std::vector<BlendState>{};

            passInfo.depthFormat = RHII.GetDepthFormat();
            passInfo.depthTestEnable = true;
            passInfo.depthWriteEnable = true;
            passInfo.Update();
        };

        configureDepthPass(*m_passInfo, "DepthPrePass_pipeline", PE_CULL_MODE_FRONT);

        if (!m_passInfoDS)
            m_passInfoDS = std::make_shared<PassInfo>();
        configureDepthPass(*m_passInfoDS, "DepthPrePass_DS_pipeline", PE_CULL_MODE_NONE);
        m_masks.Rebuild();
    }

    void DepthPass::Update()
    {
        Scene &scene = *GetActiveScene();
        m_masks.Update(scene);
        uint32_t frame = RHII.GetFrameIndex();

        uint64_t geoVersion = scene.GetGeometryVersion();
        if (geoVersion != m_lastGeometryVersion)
        {
            m_lastGeometryVersion = geoVersion;

            for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
            {
                for (PassInfo *passInfo : {m_passInfo.get(), m_passInfoDS.get()})
                {
                    if (!passInfo)
                        continue;

                    const auto &dpSets = passInfo->GetDescriptors(i);
                    Descriptor *dpSetTextures = dpSets[1];
                    dpSetTextures->SetBuffer(0, scene.GetMeshConstants());
                    dpSetTextures->SetSampler(1, scene.GetDefaultSampler());
                    dpSetTextures->Update();
                }
            }
        }

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

                if (setUniforms->HasBinding(2)) // DX12 VISIBILITY variant: DepthVS's IndirectAll
                    setUniforms->SetBuffer(2, scene.GetIndirectAll());

                if (setUniforms->HasBinding(3))
                    setUniforms->SetBuffer(3, scene.GetOpenPBRSkinningBinding(frame));

                setUniforms->Update();
            }
        }
    }

    void DepthPass::CreateUniforms(CommandBuffer *cmd)
    {
        // depth uniforms are created with the geometry
    }

    void DepthPass::UpdateDescriptorSets()
    {
        // Force geometry-dependent descriptor rebind on next Update()
        m_lastGeometryVersion = ~0ull;
    }

    void DepthPass::DeclareInputs(RGBuilder &builder)
    {
        Scene *scene = GetActiveScene();
        if (!scene)
            return;
        const uint32_t frame = RHII.GetFrameIndex();
        builder.ReadBuffer(scene->GetOpenPBRSkinningBuffer(frame));
        builder.ReadBuffer(Settings::Get<SceneSettings>().occlusion_culling ? scene->GetOccCountersA(frame)
                                                                            : scene->GetCullingCountersBuffer(frame));
    }

    void DepthPass::ExecutePass(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_scene == nullptr, "Scene was not set");

        if (m_scene->GetMeshCount() == 0)
        {
            ClearDepthStencil(cmd);

            if (m_visibility) // the resolve must never read last frame's IDs
                cmd->ClearColors({m_visibility});
        }
        else
        {
            const PushConstants_DepthPass pushConstants = MakeDepthPassConstants(*m_scene, *m_depthStencil);

            uint32_t frame = RHII.GetFrameIndex();
            const uint32_t mesh = m_scene->GetIndirectCapacity();

            // Two-phase Hi-Z: when occlusion culling is on, the depth prepass draws only set A
            // (objects visible last frame, produced by CullPhase1@180); set B (newly disoccluded)
            // is added later by DepthLatePass@270. Otherwise it draws the frustum-culled set from
            // CullingPass@50. Both layouts share the 7-slot counter convention (0/1 = opaque/alpha-cut
            // single-sided, 5/6 = double-sided).
            const bool occlusion = Settings::Get<SceneSettings>().occlusion_culling;
            Buffer *counters = occlusion ? m_scene->GetOccCountersA(frame) : m_scene->GetCullingCountersBuffer(frame);
            Buffer *opaqueSS = occlusion ? m_scene->GetOccOpaqueSSA(frame) : m_scene->GetIndirectOpaqueSS(frame);
            Buffer *alphaCutSS = occlusion ? m_scene->GetOccAlphaCutSSA(frame) : m_scene->GetIndirectAlphaCutSS(frame);
            Buffer *opaqueDS = occlusion ? m_scene->GetOccOpaqueDSA(frame) : m_scene->GetIndirectOpaqueDS(frame);
            Buffer *alphaCutDS = occlusion ? m_scene->GetOccAlphaCutDSA(frame) : m_scene->GetIndirectAlphaCutDS(frame);

            m_masks.Prepare(cmd, *m_scene);

            cmd->BeginPass(static_cast<uint32_t>(m_attachments.size()), m_attachments.data(), "DepthPass");
            cmd->SetViewport(0.f, 0.f, m_depthStencil->GetWidth_f(), m_depthStencil->GetHeight_f());
            cmd->SetScissor(0, 0, m_depthStencil->GetWidth(), m_depthStencil->GetHeight());
            cmd->BindPipeline(*m_passInfo);
            cmd->BindIndexBuffer(m_scene->GetBuffer(), 0);
            cmd->BindVertexBuffer(m_scene->GetBuffer(), m_scene->GetPositionsOffset());
            m_scene->BindDrawIdBuffer(cmd);
            cmd->SetConstants(pushConstants);
            cmd->PushConstants();
            m_scene->DrawBucket(cmd, opaqueSS, counters, 0, mesh);
            m_scene->DrawBucket(cmd, alphaCutSS, counters, 1, mesh);

            cmd->BindPipeline(*m_passInfoDS);
            m_scene->DrawBucket(cmd, opaqueDS, counters, 5, mesh);
            m_scene->DrawBucket(cmd, alphaCutDS, counters, 6, mesh);
            m_masks.Draw(cmd, *m_scene, alphaCutSS, counters, 1, mesh, false);
            m_masks.Draw(cmd, *m_scene, alphaCutDS, counters, 6, mesh, true);
            cmd->EndPass();
        }

        m_scene = nullptr;
    }

    void DepthPass::Resize(uint32_t width, uint32_t height)
    {
        Init();

        // Depth-only passes need an explicit rebuild when visibility changes the shaders and color formats.
        const std::vector<::PeFormat> wanted = m_visibility ? std::vector<::PeFormat>{PE_FORMAT_R32G32B32A32_UINT}
                                                            : std::vector<::PeFormat>{};
        if (m_passInfo && m_passInfo->colorFormats != wanted)
        {
            for (PassInfo *pi : GetPassInfos())
            {
                if (pi)
                    pi->DestroyShaders();
            }

            UpdatePassInfo();
        }

        UpdateDescriptorSets();
    }

    void DepthPass::ClearDepthStencil(CommandBuffer *cmd)
    {
        cmd->ClearDepthStencils({m_depthStencil});
    }

    void DepthPass::Destroy()
    {
        m_masks.Clear();
        m_lastGeometryVersion = ~0ull; // a re-initialized pass starts with empty descriptors
        if (!m_passInfoDS)
            return;

        Shader::Destroy(m_passInfoDS->pVertShader);
        Shader::Destroy(m_passInfoDS->pFragShader);
        m_passInfoDS.reset();
    }
} // namespace pe
