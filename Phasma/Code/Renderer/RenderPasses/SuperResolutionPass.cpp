#include "Renderer/RenderPasses/SuperResolutionPass.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "Renderer/Image.h"
#include "Renderer/Surface.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"
#include "GUI/GUI.h"
#include "FRS2/ffx_fsr2.h"
#include "FRS2/vk/ffx_fsr2_vk.h"

namespace pe
{
    SuperResolutionPass::SuperResolutionPass()
    {
    }

    SuperResolutionPass::~SuperResolutionPass()
    {
    }

    static void FfxFsr2MessageCallback(FfxFsr2MsgType type, const wchar_t *message)
    {
        // convert wchar_t to char
        size_t size = wcslen(message) + 1;
        char *buffer = new char[size];
        size_t outSize;
        wcstombs_s(&outSize, buffer, size, message, size - 1);

        switch (type)
        {
        case FFX_FSR2_MESSAGE_TYPE_WARNING:
            std::cout << "FSR2 WARNING: " << buffer << std::endl;
            break;
        case FFX_FSR2_MESSAGE_TYPE_ERROR:
            std::cout << "FSR2 ERROR: " << buffer << std::endl;
            break;
        default:
            PE_ERROR("Unknown FfxFsr2MsgType");
            break;
        }
    }

    void SuperResolutionPass::Init()
    {
        m_renderQueue = RHII.GetRenderQueue();
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
        m_contextDescription->flags = FFX_FSR2_ENABLE_DEPTH_INVERTED | FFX_FSR2_ENABLE_DEPTH_INFINITE | FFX_FSR2_ENABLE_AUTO_EXPOSURE;
        if (Settings::Get<SRSettings>().fsr2Debug)
        {
            m_contextDescription->flags |= FFX_FSR2_ENABLE_DEBUG_CHECKING;
            m_contextDescription->fpMessage = FfxFsr2MessageCallback;
        }

        const uint64_t memoryUsageBefore = RHII.GetMemoryUsageSnapshot();
        ffxFsr2ContextCreate(m_context.get(), m_contextDescription.get());
        const uint64_t memoryUsageAfter = RHII.GetMemoryUsageSnapshot();
        m_memoryUsageInMegabytes = (memoryUsageAfter - memoryUsageBefore) * 0.000001f;

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

    void SuperResolutionPass::Update(Camera *camera)
    {
        // GenerateJitter in main camera update
    }

    CommandBuffer *SuperResolutionPass::Draw()
    {
        CommandBuffer *cmd = CommandBuffer::GetFree(m_renderQueue);
        cmd->Begin();

        cmd->BeginDebugRegion("Super Resolution");

        std::vector<ImageBarrierInfo> barriers(4);
        barriers[0].image = m_viewportRT;
        barriers[0].layout = ImageLayout::ShaderReadOnly;
        barriers[0].stageFlags = PipelineStage::ComputeShaderBit;
        barriers[0].accessMask = Access::ShaderReadBit;
        
        barriers[1].image = m_velocityRT;
        barriers[1].layout = ImageLayout::ShaderReadOnly;
        barriers[1].stageFlags = PipelineStage::ComputeShaderBit;
        barriers[1].accessMask = Access::ShaderReadBit;

        barriers[2].image = m_depthStencil;
        barriers[2].layout = ImageLayout::ShaderReadOnly;
        barriers[2].stageFlags = PipelineStage::ComputeShaderBit;
        barriers[2].accessMask = Access::ShaderReadBit;

        barriers[3].image = m_display;
        barriers[3].layout = ImageLayout::General;
        barriers[3].stageFlags = PipelineStage::ComputeShaderBit;
        barriers[3].accessMask = Access::ShaderWriteBit;

        cmd->ImageBarriers(barriers);

        Camera &camera = *GetGlobalSystem<CameraSystem>()->GetCamera(0);

        FfxFsr2DispatchDescription dd = {};
        dd.commandList = ffxGetCommandListVK(cmd->Handle());
        wchar_t fsr2InputName[] = L"FSR2_Input";
        dd.color = ffxGetTextureResourceVK(m_context.get(),
                                           m_viewportRT->Handle(),
                                           m_viewportRT->GetSRV(),
                                           m_viewportRT->GetWidth(),
                                           m_viewportRT->GetHeight(),
                                           Translate<VkFormat>(m_viewportRT->GetFormat()),
                                           fsr2InputName);
        wchar_t fsr2DepthName[] = L"FSR2_Depth";
        dd.depth = ffxGetTextureResourceVK(m_context.get(),
                                           m_depthStencil->Handle(),
                                           m_depthStencil->GetSRV(),
                                           m_depthStencil->GetWidth(),
                                           m_depthStencil->GetHeight(),
                                           Translate<VkFormat>(m_depthStencil->GetFormat()),
                                           fsr2DepthName);
        wchar_t fsr2VelocityName[] = L"FSR2_Velocity";
        dd.motionVectors = ffxGetTextureResourceVK(m_context.get(),
                                                   m_velocityRT->Handle(),
                                                   m_velocityRT->GetSRV(),
                                                   m_velocityRT->GetWidth(),
                                                   m_velocityRT->GetHeight(),
                                                   Translate<VkFormat>(m_velocityRT->GetFormat()),
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
                                            m_display->Handle(),
                                            m_display->GetSRV(),
                                            m_display->GetWidth(),
                                            m_display->GetHeight(),
                                            Translate<VkFormat>(m_display->GetFormat()),
                                            fsr2OutputName,
                                            FFX_RESOURCE_STATE_UNORDERED_ACCESS);

        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &srSettings = Settings::Get<SRSettings>();

        dd.jitterOffset.x = m_jitter.x;
        dd.jitterOffset.y = m_jitter.y;
        dd.motionVectorScale.x = srSettings.motionScale.x * m_viewportRT->GetWidth_f();
        dd.motionVectorScale.y = srSettings.motionScale.y * m_viewportRT->GetHeight_f();
        dd.renderSize.width = m_viewportRT->GetWidth();
        dd.renderSize.height = m_viewportRT->GetHeight();
        dd.enableSharpening = true;
        dd.sharpness = 1.0f - (gSettings.render_scale * gSettings.render_scale);
        dd.frameTimeDelta = static_cast<float>(MILLI(FrameTimer::Instance().GetDelta()));
        dd.preExposure = 1.0f;
        dd.reset = false;
        dd.cameraNear = gSettings.reverse_depth ? camera.GetFarPlane() : camera.GetNearPlane();
        dd.cameraFar = gSettings.reverse_depth ? camera.GetNearPlane() : camera.GetFarPlane();
        dd.cameraFovAngleVertical = camera.Fovy();

        cmd->BeginDebugRegion("SuperResolution_Pass");
        PE_CHECK(ffxFsr2ContextDispatch(m_context.get(), &dd));
        cmd->EndDebugRegion();

        ImageBarrierInfo info{};
        info.image = m_display;
        info.layout = ImageLayout::Attachment;
        info.stageFlags = PipelineStage::ColorAttachmentOutputBit;
        info.accessMask = Access::ColorAttachmentWriteBit;
        cmd->ImageBarrier(info);

        cmd->EndDebugRegion();
        
        cmd->AddFlags(CommandType::ComputeBit);

        cmd->End();
        return cmd;
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
        
        int jitterPhaseCount = ffxFsr2GetJitterPhaseCount(m_viewportRT->GetWidth(), m_display->GetHeight());
        ffxFsr2GetJitterOffset(&m_jitter.x, &m_jitter.y, RHII.GetFrameCounter(), jitterPhaseCount);

        m_projectionJitter.x = srSettings.projScale.x * (2.0f * m_jitter.x / m_viewportRT->GetWidth());
        m_projectionJitter.y = srSettings.projScale.y * (2.0f * m_jitter.y / m_viewportRT->GetHeight());
    }
}
