#include "ParticleComputePass.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Particles/ParticleManager.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    void ParticleComputePass::Init()
    {
    }

    void ParticleComputePass::UpdatePassInfo()
    {
        m_passInfo->name = "ParticleCompute_pipeline";
        m_passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Particle/ParticleCS.hlsl", .entryPoint = "mainCS", .stage = PE_SHADER_STAGE_COMPUTE, .defines = std::vector<Define>{}});
        m_passInfo->Update();
    }

    void ParticleComputePass::CreateUniforms(CommandBuffer *cmd)
    {
        UpdateDescriptorSets();
    }

    void ParticleComputePass::UpdateDescriptorSets()
    {
        Scene &scene = *GetActiveScene();
        if (!scene.GetParticleManager())
            return;

        Buffer *particleBuffer = scene.GetParticleManager()->GetParticleBuffer();
        if (!particleBuffer)
            return;

        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto &descriptors = m_passInfo->GetDescriptors(i);
            if (descriptors.size() > 0)
            {
                descriptors[0]->SetBuffer(0, particleBuffer);
                if (Buffer *emitterBuffer = scene.GetParticleManager()->GetEmitterBuffer(i))
                    descriptors[0]->SetBuffer(1, emitterBuffer);
                descriptors[0]->Update();
            }
        }
    }

    void ParticleComputePass::Update()
    {
        if (m_scene && m_scene->GetParticleManager())
        {
            if (m_scene->GetParticleManager()->GetBufferVersion() > m_lastBufferVersion)
            {
                UpdateDescriptorSets();
                m_lastBufferVersion = m_scene->GetParticleManager()->GetBufferVersion();
            }
        }
    }

    void ParticleComputePass::ExecutePass(CommandBuffer *cmd)
    {
        Scene &scene = *GetActiveScene();
        if (!scene.GetParticleManager())
            return;

        // Skip empty dispatch; keep pass alive so InitTextures still has a valid cmd path.
        if (scene.GetParticleManager()->GetEmitterCount() == 0 &&
            scene.GetParticleManager()->GetParticleCount() == 0)
            return;

        Buffer *particleBuffer = scene.GetParticleManager()->GetParticleBuffer();
        if (!particleBuffer)
            return;

        const uint32_t frame = RHII.GetFrameIndex();
        Buffer *emitterBuffer = scene.GetParticleManager()->PrepareEmitterBufferForFrame(frame);
        if (!emitterBuffer)
            return;

        struct PushConstants
        {
            vec4 cameraPosition;
            vec4 cameraForward;
            float deltaTime;
            uint32_t particleCount;
            float totalTime;
            uint32_t emitterCount;
        } pc{};

        pc.deltaTime = static_cast<float>(FrameTimer::Instance().GetDelta()) * Settings::Get<SceneSettings>().time_scale;
        pc.particleCount = scene.GetParticleManager()->GetParticleCount();
        pc.emitterCount = scene.GetParticleManager()->GetEmitterCount();

        static float accumTime = 0.0f;
        accumTime += pc.deltaTime;
        pc.totalTime = accumTime;

        // Update Push Constants
        if (m_scene)
        {
            Camera *camera = m_scene->GetActiveCamera();
            if (camera)
            {
                pc.cameraPosition = vec4(camera->GetPosition(), 1.0f);
                pc.cameraForward = vec4(camera->GetFront(), 1.0f);
            }
        }

        auto &descriptors = m_passInfo->GetDescriptors(frame);
        if (!descriptors.empty())
        {
            descriptors[0]->SetBuffer(0, particleBuffer);
            descriptors[0]->SetBuffer(1, emitterBuffer);
            descriptors[0]->Update();
        }

        cmd->BeginDebugRegion("ParticleComputePass");
        scene.GetParticleManager()->FlushPendingParticleClears(cmd);

        BufferBarrierInfo barrier{};
        barrier.stageMask = PE_STAGE_COMPUTE_SHADER;
        barrier.accessMask = PE_ACCESS_SHADER_STORAGE_WRITE;
        barrier.buffer = particleBuffer;
        barrier.size = PE_WHOLE_SIZE;
        cmd->BufferBarrier(barrier);

        cmd->BindPipeline(*m_passInfo);
        cmd->SetConstants(pc);
        cmd->PushConstants();

        uint32_t groupCount = (pc.particleCount + 255) / 256;
        cmd->Dispatch(groupCount, 1, 1);

        cmd->EndDebugRegion();
    }

    void ParticleComputePass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void ParticleComputePass::Destroy()
    {
    }
} // namespace pe
