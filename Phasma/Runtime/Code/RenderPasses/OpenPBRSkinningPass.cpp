#include "OpenPBRSkinningPass.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    namespace
    {
        struct SkinningConstants
        {
            uint32_t verticesOffset;
            uint32_t jobsOffset;
            uint32_t jobBase;
            uint32_t frustumCulling;
            vec4 frustumPlanes[6];
        };
        static_assert(sizeof(SkinningConstants) == 112, "Skinning push constants must match OpenPBRSkinningCS.hlsl");

        void DispatchSkinning(CommandBuffer *cmd, const Scene &scene)
        {
            constexpr uint32_t maxGroups = 65535u;
            const uint32_t jobs = scene.GetOpenPBRSkinningJobCount();
            SkinningConstants pc{};
            pc.verticesOffset = static_cast<uint32_t>(scene.GetVerticesOffset());
            pc.jobsOffset = scene.GetOpenPBRSkinningJobsOffset();

            // Mirrors Scene::DispatchCullingPhase: the camera passes cull with these planes.
            Camera *camera = scene.GetCameras().empty() ? nullptr : scene.GetCameras()[0];
            if (camera && Settings::Get<SceneSettings>().frustum_culling)
            {
                pc.frustumCulling = 1u;
                const auto &planes = camera->GetFrustumPlanes();
                for (int i = 0; i < 6; i++)
                    pc.frustumPlanes[i] = vec4(planes[i].normal[0], planes[i].normal[1], planes[i].normal[2], planes[i].d);
            }

            for (uint32_t job = 0; job < jobs; job += std::min(jobs - job, maxGroups))
            {
                pc.jobBase = job;
                cmd->SetConstants(pc);
                cmd->PushConstants();
                cmd->Dispatch(std::min(jobs - job, maxGroups), 1u, 1u);
            }
        }
    } // namespace

    void OpenPBRSkinningPass::UpdatePassInfo()
    {
        m_passInfo->name = "OpenPBRSkinning_pipeline";
        m_passInfo->pCompShader = Shader::Create({
            .sourcePath = Path::RuntimeAssets + "Shaders/Compute/OpenPBRSkinningCS.hlsl",
            .entryPoint = "mainCS",
            .stage = PE_SHADER_STAGE_COMPUTE,
        });
        m_passInfo->Update();
    }

    void OpenPBRSkinningPass::Update()
    {
        Scene *scene = GetActiveScene();
        const uint32_t frame = RHII.GetFrameIndex();
        if (!scene || !scene->GetOpenPBRSkinningBuffer(frame))
            return;

        Descriptor *set = m_passInfo->GetDescriptors(frame)[0];
        set->SetBuffer(0, scene->GetBuffer());
        set->SetBuffer(1, scene->GetUniforms(frame));
        set->SetBuffer(2, scene->GetMeshConstants());
        set->SetBuffer(3, scene->GetOpenPBRSkinningBuffer(frame));
        set->Update();
    }

    void OpenPBRSkinningPass::DeclareInputs(RGBuilder &builder)
    {
        if (Scene *scene = GetActiveScene())
            builder.WriteBuffer(scene->GetOpenPBRSkinningBuffer(RHII.GetFrameIndex()));
    }

    void OpenPBRSkinningPass::ExecutePass(CommandBuffer *cmd)
    {
        Scene *scene = GetActiveScene();
        Buffer *buffer = scene ? scene->GetOpenPBRSkinningBuffer(RHII.GetFrameIndex()) : nullptr;
        if (!buffer)
            return;

        PE_ERROR_IF(scene->GetVerticesOffset() > UINT32_MAX, "OpenPBR skinning geometry exceeds uint32 addressing");
        cmd->BeginDebugRegion("OpenPBRSkinningPass");
        scene->UploadOpenPBRSkinningMetadata(cmd);
        cmd->BufferBarrier({.buffer = scene->GetBuffer(),
                            .stageMask = PE_STAGE_COMPUTE_SHADER | PE_STAGE_VERTEX_INPUT | PE_STAGE_VERTEX_SHADER,
                            .accessMask = PE_ACCESS_SHADER_READ | PE_ACCESS_VERTEX_ATTRIBUTE_READ | PE_ACCESS_INDEX_READ});
        cmd->BufferBarrier({.buffer = buffer,
                            .stageMask = PE_STAGE_COMPUTE_SHADER,
                            .accessMask = PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE});
        cmd->BindPipeline(*m_passInfo);

        DispatchSkinning(cmd, *scene);

        cmd->BufferBarrier({.buffer = buffer,
                            .stageMask = PE_STAGE_VERTEX_SHADER | PE_STAGE_COMPUTE_SHADER,
                            .accessMask = PE_ACCESS_SHADER_READ});
        cmd->EndDebugRegion();
    }

    void OpenPBRSkinningPass::Destroy()
    {
        if (m_passInfo)
            Shader::Destroy(m_passInfo->pCompShader);
    }
} // namespace pe
