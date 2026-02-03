#include "RenderPasses/ParticlePass.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Particles/ParticleManager.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    ParticlePass::~ParticlePass()
    {
        Destroy();
    }

    void ParticlePass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_attachments.resize(2);
        m_attachments[0].image = rs->GetRenderTarget("viewport");
        m_attachments[0].loadOp = vk::AttachmentLoadOp::eLoad;
        m_attachments[0].storeOp = vk::AttachmentStoreOp::eStore;

        m_attachments[1].image = rs->GetDepthStencilTarget("depthStencil");
        m_attachments[1].loadOp = vk::AttachmentLoadOp::eLoad;
        m_attachments[1].storeOp = vk::AttachmentStoreOp::eStore;
    }

    void ParticlePass::UpdatePassInfo()
    {
        // Depth test but no write (soft particles)
        // Need depth buffer?
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        auto *depthRT = rs->GetDepthStencilTarget("depthStencil");
        m_passInfo->name = "ParticleGraphicsPipeline";
        m_passInfo->pVertShader = Shader::Create(Path::Assets + "Shaders/Particle/ParticleVS.hlsl", vk::ShaderStageFlagBits::eVertex, "mainVS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->pFragShader = Shader::Create(Path::Assets + "Shaders/Particle/ParticlePS.hlsl", vk::ShaderStageFlagBits::eFragment, "mainPS", std::vector<Define>{}, ShaderCodeType::HLSL);
        m_passInfo->topology = vk::PrimitiveTopology::eTriangleList;
        m_passInfo->cullMode = vk::CullModeFlagBits::eNone;
        m_passInfo->blendEnable = true;
        m_passInfo->colorBlendAttachments = {PipelineColorBlendAttachmentState::ParticlesBlend}; // Standard alpha blending with all channels
        m_passInfo->colorFormats = {m_attachments[0].image->GetFormat()};
        m_passInfo->depthFormat = depthRT->GetFormat();
        m_passInfo->depthTestEnable = false; // Disable for debugging
        m_passInfo->depthWriteEnable = false;
        m_passInfo->depthCompareOp = Settings::Get<GlobalSettings>().reverse_depth ? vk::CompareOp::eGreaterOrEqual : vk::CompareOp::eLessOrEqual;
        m_passInfo->dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        m_passInfo->Update();
    }

    void ParticlePass::CreateUniforms(CommandBuffer *cmd)
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        Scene &scene = rs->GetScene();
        if (scene.GetParticleManager())
        {
            scene.GetParticleManager()->InitTextures(cmd);
        }

        UpdateDescriptorSets();
    }

    void ParticlePass::UpdateDescriptorSets()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        Scene &scene = rs->GetScene();
        if (!scene.GetParticleManager())
            return;

        Buffer *particleBuffer = scene.GetParticleManager()->GetParticleBuffer();
        if (!particleBuffer)
            return;

        for (uint32_t i = 0; i < RHII.GetSwapchainImageCount(); i++)
        {
            auto &descriptors = m_passInfo->GetDescriptors(i);
            descriptors[0]->SetBuffer(0, particleBuffer);

            Buffer *emitterBuffer = scene.GetParticleManager()->GetEmitterBuffer();
            if (emitterBuffer)
                descriptors[0]->SetBuffer(1, emitterBuffer);

            descriptors[0]->Update();

            auto *pm = scene.GetParticleManager();
            const auto &textures = pm->GetTextures();
            if (!textures.empty() && descriptors.size() > 1)
            {
                std::vector<ImageView *> views;
                for (auto *img : textures)
                    views.push_back(img->GetSRV());
                descriptors[1]->SetImageViews(0, views);
                descriptors[1]->SetSampler(1, pm->GetSampler());
                descriptors[1]->Update();
            }
        }
    }

    void ParticlePass::Update()
    {
        if (m_scene && m_scene->GetParticleManager())
        {
            if (m_scene->GetParticleManager()->HasTextureChanged())
            {
                UpdateDescriptorSets();
                m_scene->GetParticleManager()->ResetTextureChanged();
            }

            if (m_scene->GetParticleManager()->GetBufferVersion() > m_lastBufferVersion)
            {
                UpdateDescriptorSets();
                m_lastBufferVersion = m_scene->GetParticleManager()->GetBufferVersion();
            }
        }
    }

    void ParticlePass::ExecutePass(CommandBuffer *cmd)
    {
        if (!m_scene)
            return;

        if (!m_scene->GetParticleManager())
            return;

        Buffer *particleBuffer = m_scene->GetParticleManager()->GetParticleBuffer();
        if (!particleBuffer)
            return;

        // 1. Barrier: Wait for Compute Write to finish before Vertex Read
        BufferBarrierInfo barrier{};
        barrier.stageMask = vk::PipelineStageFlagBits2::eVertexShader;
        barrier.accessMask = vk::AccessFlagBits2::eShaderRead;
        barrier.buffer = particleBuffer;
        barrier.size = VK_WHOLE_SIZE;
        cmd->BufferBarrier(barrier);

        struct PushConstants
        {
            mat4 viewProjection;
            vec4 cameraRight;
            vec4 cameraUp;
            vec4 cameraPosition;
            vec4 cameraForward;
        } pc{};

        Camera *camera = m_scene->GetActiveCamera();
        if (camera)
        {
            pc.viewProjection = camera->GetViewProjection();
            pc.cameraRight = vec4(camera->GetRight(), 0.0f);
            pc.cameraUp = vec4(camera->GetUp(), 0.0f);
            pc.cameraPosition = vec4(camera->GetPosition(), 1.0f);
            pc.cameraForward = vec4(camera->GetFront(), 0.0f);
        }

        cmd->BeginPass(static_cast<uint32_t>(m_attachments.size()), m_attachments.data(), "ParticlePass");
        cmd->BindPipeline(*m_passInfo);

        // Dynamic Viewport & Scissor
        Image *image = m_attachments[0].image;
        cmd->SetViewport(0.0f, 0.0f, image->GetWidth_f(), image->GetHeight_f());
        cmd->SetScissor(0, 0, image->GetWidth(), image->GetHeight());

        cmd->SetConstants(pc);
        cmd->PushConstants();

        cmd->Draw(m_scene->GetParticleManager()->GetParticleCount() * 6, 1, 0, 0);

        cmd->EndPass();
    }

    void ParticlePass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void ParticlePass::Destroy()
    {
    }
} // namespace pe
