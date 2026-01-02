#include "SuperResolutionPass.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "Camera/Camera.h"
#include "FRS2/ffx_fsr2.h"
#include "FRS2/vk/ffx_fsr2_vk.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    static void FfxFsr2MessageCallback(FfxFsr2MsgType type, const wchar_t *message)
    {
        size_t size = wcslen(message) + 1;
        std::vector<char> buffer(size);
        std::wcstombs(buffer.data(), message, size);

        switch (type)
        {
        case FFX_FSR2_MESSAGE_TYPE_WARNING:
            std::cout << "FSR2 WARNING: " << buffer.data() << std::endl;
            break;
        case FFX_FSR2_MESSAGE_TYPE_ERROR:
            std::cout << "FSR2 ERROR: " << buffer.data() << std::endl;
            break;
        default:
            PE_ERROR("Unknown FfxFsr2MsgType");
            break;
        }
    }

    void SuperResolutionPass::Init()
    {
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();

        m_display = rs->GetDisplayRT();
        m_viewportRT = rs->GetViewportRT();
        m_depthStencil = rs->GetDepthStencilRT();
        m_velocityRT = rs->GetRenderTarget("velocity");

        m_context = std::make_shared<FfxFsr2Context>();
        m_contextDescription = std::make_shared<FfxFsr2ContextDescription>();
        const size_t scratchBufferSize = ffxFsr2GetScratchMemorySizeVK(RHII.GetGpu());
        void *scratchBuffer = malloc(scratchBufferSize);
        FfxErrorCode errorCode = ffxFsr2GetInterfaceVK(&m_contextDescription->callbacks, scratchBuffer, scratchBufferSize, RHII.GetGpu(), vkGetDeviceProcAddr);
        FFX_ASSERT(errorCode == FFX_OK);

        m_contextDescription->device = ffxGetDeviceVK(RHII.GetDevice());
        m_contextDescription->maxRenderSize.width = m_viewportRT->GetWidth();
        m_contextDescription->maxRenderSize.height = m_viewportRT->GetHeight();
        m_contextDescription->displaySize.width = m_display->GetWidth();
        m_contextDescription->displaySize.height = m_display->GetHeight();

        auto &gSettings = Settings::Get<GlobalSettings>();

        uint32_t flags = 0;
        if (gSettings.reverse_depth)
        {
            flags |= FFX_FSR2_ENABLE_DEPTH_INVERTED;
        }
        // flags |= FFX_FSR2_ENABLE_DEPTH_INFINITE;
        flags |= FFX_FSR2_ENABLE_AUTO_EXPOSURE;
        // if (Settings::Get<SRSettings>().fsr2Debug)
        // {
        //     flags |= FFX_FSR2_ENABLE_DEBUG_CHECKING;
        //     m_contextDescription->fpMessage = FfxFsr2MessageCallback;
        // }

        m_contextDescription->flags = flags;
        ffxFsr2ContextCreate(m_context.get(), m_contextDescription.get());

        m_jitter = vec2(0.0f);
    }

    void SuperResolutionPass::UpdatePassInfo()
    {
    }

    void SuperResolutionPass::CreateUniforms(CommandBuffer *cmd)
    {
    }

    void SuperResolutionPass::UpdateDescriptorSets()
    {
    }

    void SuperResolutionPass::Update()
    {
    }

    void SuperResolutionPass::Draw(CommandBuffer *cmd)
    {
        Camera &camera = *GetGlobalSystem<RendererSystem>()->GetScene().GetCamera(0);

        std::vector<ImageBarrierInfo> barriers(4);
        barriers[0].image = m_viewportRT;
        barriers[0].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[0].stageFlags = vk::PipelineStageFlagBits2::eComputeShader;
        barriers[0].accessMask = vk::AccessFlagBits2::eShaderRead;

        barriers[1].image = m_velocityRT;
        barriers[1].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[1].stageFlags = vk::PipelineStageFlagBits2::eComputeShader;
        barriers[1].accessMask = vk::AccessFlagBits2::eShaderRead;

        barriers[2].image = m_depthStencil;
        barriers[2].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[2].stageFlags = vk::PipelineStageFlagBits2::eComputeShader;
        barriers[2].accessMask = vk::AccessFlagBits2::eShaderRead;

        barriers[3].image = m_display;
        barriers[3].layout = vk::ImageLayout::eGeneral;
        barriers[3].stageFlags = vk::PipelineStageFlagBits2::eComputeShader;
        barriers[3].accessMask = vk::AccessFlagBits2::eShaderWrite;

        ImageBarrierInfo displayBarrier{};
        displayBarrier.image = m_display;
        displayBarrier.layout = vk::ImageLayout::eAttachmentOptimal;
        displayBarrier.stageFlags = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        displayBarrier.accessMask = vk::AccessFlagBits2::eColorAttachmentWrite;

        FfxFsr2DispatchDescription dd = {};
        dd.commandList = ffxGetCommandListVK(cmd->ApiHandle());
        wchar_t fsr2InputName[] = L"FSR2_Input";
        dd.color = ffxGetTextureResourceVK(m_context.get(),
                                           m_viewportRT->ApiHandle(),
                                           m_viewportRT->GetSRV(),
                                           m_viewportRT->GetWidth(),
                                           m_viewportRT->GetHeight(),
                                           static_cast<VkFormat>(m_viewportRT->GetFormat()),
                                           fsr2InputName);
        wchar_t fsr2DepthName[] = L"FSR2_Depth";
        dd.depth = ffxGetTextureResourceVK(m_context.get(),
                                           m_depthStencil->ApiHandle(),
                                           m_depthStencil->GetSRV(),
                                           m_depthStencil->GetWidth(),
                                           m_depthStencil->GetHeight(),
                                           static_cast<VkFormat>(m_depthStencil->GetFormat()),
                                           fsr2DepthName);
        wchar_t fsr2VelocityName[] = L"FSR2_Velocity";
        dd.motionVectors = ffxGetTextureResourceVK(m_context.get(),
                                                   m_velocityRT->ApiHandle(),
                                                   m_velocityRT->GetSRV(),
                                                   m_velocityRT->GetWidth(),
                                                   m_velocityRT->GetHeight(),
                                                   static_cast<VkFormat>(m_velocityRT->GetFormat()),
                                                   fsr2VelocityName);
        wchar_t fsr2ExposureName[] = L"FSR2_Exposure";
        dd.exposure = ffxGetTextureResourceVK(m_context.get(),
                                              nullptr,
                                              nullptr,
                                              1,
                                              1,
                                              VK_FORMAT_UNDEFINED,
                                              fsr2ExposureName);
        wchar_t fsr2ReactiveName[] = L"FSR2_Reactive";
        dd.reactive = ffxGetTextureResourceVK(m_context.get(),
                                              nullptr,
                                              nullptr,
                                              1,
                                              1,
                                              VK_FORMAT_UNDEFINED,
                                              fsr2ReactiveName);
        wchar_t fsr2TACName[] = L"FSR2_TransparencyAndComposition";
        dd.transparencyAndComposition = ffxGetTextureResourceVK(m_context.get(),
                                                                nullptr,
                                                                nullptr,
                                                                1,
                                                                1,
                                                                VK_FORMAT_UNDEFINED,
                                                                fsr2TACName);
        wchar_t fsr2OutputName[] = L"FSR2_Output";
        dd.output = ffxGetTextureResourceVK(m_context.get(),
                                            m_display->ApiHandle(),
                                            m_display->GetSRV(),
                                            m_display->GetWidth(),
                                            m_display->GetHeight(),
                                            static_cast<VkFormat>(m_display->GetFormat()),
                                            fsr2OutputName,
                                            FFX_RESOURCE_STATE_UNORDERED_ACCESS);

        auto &srSettings = Settings::Get<SRSettings>();
        auto &gSettings = Settings::Get<GlobalSettings>();

        dd.jitterOffset.x = m_jitter.x;
        dd.jitterOffset.y = m_jitter.y;
        dd.motionVectorScale.x = srSettings.motionScale.x * 0.5f * m_viewportRT->GetWidth_f();
        dd.motionVectorScale.y = srSettings.motionScale.y * 0.5f * m_viewportRT->GetHeight_f();
        dd.renderSize.width = m_viewportRT->GetWidth();
        dd.renderSize.height = m_viewportRT->GetHeight();
        dd.enableSharpening = true;
        dd.sharpness = 1.0f - (gSettings.render_scale * gSettings.render_scale);
        dd.frameTimeDelta = static_cast<float>(MILLI(FrameTimer::Instance().GetDelta()));
        dd.preExposure = 1.0f;
        dd.reset = false;
        dd.cameraNear = camera.GetNearPlane();
        dd.cameraFar = camera.GetFarPlane();
        dd.cameraFovAngleVertical = camera.Fovy();
        dd.viewSpaceToMetersFactor = 1.0f;

        cmd->BeginDebugRegion("Super Resolution");
        cmd->ImageBarriers(barriers);
        cmd->BeginDebugRegion("SuperResolution_Pass");
        PE_CHECK(ffxFsr2ContextDispatch(m_context.get(), &dd));
        cmd->EndDebugRegion();
        cmd->ImageBarrier(displayBarrier);
        cmd->EndDebugRegion();
    }

    void SuperResolutionPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
    }

    void SuperResolutionPass::Destroy()
    {
        ffxFsr2ContextDestroy(m_context.get());
    }

    void SuperResolutionPass::GenerateJitter()
    {
        auto &srSettings = Settings::Get<SRSettings>();
        uint64_t frameCounter = RHII.GetFrameCounter();

        static uint64_t lastFrame = -1;
        if (lastFrame == frameCounter)
            return;

        lastFrame = frameCounter;

        int jitterPhaseCount = ffxFsr2GetJitterPhaseCount(m_viewportRT->GetWidth(), m_display->GetWidth());
        ffxFsr2GetJitterOffset(&m_jitter.x, &m_jitter.y, frameCounter, jitterPhaseCount);

        m_projectionJitter.x = srSettings.projScale.x * (2.0f * m_jitter.x / m_viewportRT->GetWidth());
        m_projectionJitter.y = srSettings.projScale.y * (-2.0f * m_jitter.y / m_viewportRT->GetHeight());
    }
} // namespace pe
