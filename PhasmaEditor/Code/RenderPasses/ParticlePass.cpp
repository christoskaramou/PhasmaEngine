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

        // Graphics Pass Info (inherited m_passInfo)
        m_passInfo = std::make_shared<PassInfo>();
        m_passInfo->name = "ParticleGraphicsPipeline";

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
        if (m_fireTextures.empty())
        {
            m_fireTextures.push_back(Image::LoadRGBA8(cmd, Path::Assets + "Particles/fire_01.png"));
            m_fireTextures.push_back(Image::LoadRGBA8(cmd, Path::Assets + "Particles/fire_02.png"));
            m_fireTextures.push_back(Image::LoadRGBA8(cmd, Path::Assets + "Particles/fire_03.png"));

            vk::SamplerCreateInfo samplerCI = Sampler::CreateInfoInit();
            samplerCI.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerCI.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            samplerCI.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            m_sampler = new Sampler(samplerCI, "ParticleSampler");
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
            descriptors[0]->Update();

            if (!m_fireTextures.empty() && descriptors.size() > 1)
            {
                std::vector<vk::ImageView> views;
                for (auto *img : m_fireTextures)
                    views.push_back(img->GetSRV());
                descriptors[1]->SetImageViews(0, views, {});
                descriptors[1]->SetSampler(1, m_sampler->ApiHandle());
                descriptors[1]->Update();
            }
        }
    }

    void ParticlePass::Update()
    {
    }

    void ParticlePass::Draw(CommandBuffer *cmd)
    {
        if (!m_scene)
            return;

        if (!m_scene->GetParticleManager())
            return;

        Buffer *particleBuffer = m_scene->GetParticleManager()->GetParticleBuffer();
        if (!particleBuffer)
            return;

        // 1. Barrier: Wait for Compute Write to finish before Vertex Read
        vk::BufferMemoryBarrier2 barrier{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        barrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eVertexShader;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
        barrier.buffer = particleBuffer->ApiHandle();
        barrier.size = VK_WHOLE_SIZE;
        cmd->BufferBarrier(barrier);

        // 2. Render Particles (Graphics)
        struct PushConstants
        {
            mat4 viewProjection;
            vec4 cameraRight;
            vec4 cameraUp;
        } pc{};

        // Camera logic remains same...
        Camera *camera = m_scene->GetCamera(0); // Assuming main camera
        if (camera)
        {
            pc.viewProjection = camera->GetViewProjection();
            pc.cameraRight = vec4(camera->GetRight(), 0.0f);
            pc.cameraUp = vec4(camera->GetUp(), 0.0f);
        }

        cmd->BeginPass(static_cast<uint32_t>(m_attachments.size()), m_attachments.data(), "ParticlePass");
        cmd->BindPipeline(*m_passInfo);

        // Dynamic Viewport & Scissor
        Image *image = m_attachments[0].image;
        cmd->SetViewport(0.0f, 0.0f, image->GetWidth_f(), image->GetHeight_f());
        cmd->SetScissor(0, 0, image->GetWidth(), image->GetHeight());

        cmd->SetConstants(pc);
        cmd->PushConstants();

        // Draw quads (6 vertices per particle)
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
        for (auto *img : m_fireTextures)
            Image::Destroy(img);
        m_fireTextures.clear();

        if (m_sampler)
        {
            delete m_sampler;
            m_sampler = nullptr;
        }
    }
} // namespace pe
