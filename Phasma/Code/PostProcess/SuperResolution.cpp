#include "SuperResolution.h"
#include "Renderer/RHI.h"
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
    SuperResolution::SuperResolution()
    {
    }

    SuperResolution::~SuperResolution()
    {
    }

    void SuperResolution::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        m_viewportRT = rs->GetRenderTarget("viewport");
        m_velocityRT = rs->GetRenderTarget("velocity");
        m_depth = rs->GetDepthTarget("depth");
        m_display = rs->GetRenderTarget("display");

        m_context = std::make_shared<FfxFsr2Context>();
        m_contextDescription = std::make_shared<FfxFsr2ContextDescription>();
        const size_t scratchBufferSize = ffxFsr2GetScratchMemorySizeVK(RHII.GetGpu());
        void *scratchBuffer = malloc(scratchBufferSize);
        FfxErrorCode errorCode = ffxFsr2GetInterfaceVK(&m_contextDescription->callbacks, scratchBuffer, scratchBufferSize, RHII.GetGpu(), vkGetDeviceProcAddr);
        FFX_ASSERT(errorCode == FFX_OK);

        m_contextDescription->device = ffxGetDeviceVK(RHII.GetDevice());
        m_contextDescription->maxRenderSize.width = m_display->imageInfo.width;   // m_viewportRT->imageInfo.width;
        m_contextDescription->maxRenderSize.height = m_display->imageInfo.height; // m_viewportRT->imageInfo.height;
        m_contextDescription->displaySize.width = m_display->imageInfo.width;
        m_contextDescription->displaySize.height = m_display->imageInfo.height;
        m_contextDescription->flags = FFX_FSR2_ENABLE_DEPTH_INVERTED |
                                      FFX_FSR2_ENABLE_AUTO_EXPOSURE |
                                      FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE;

        const uint64_t memoryUsageBefore = RHII.GetMemoryUsageSnapshot();
        ffxFsr2ContextCreate(m_context.get(), m_contextDescription.get());
        const uint64_t memoryUsageAfter = RHII.GetMemoryUsageSnapshot();
        m_memoryUsageInMegabytes = (memoryUsageAfter - memoryUsageBefore) * 0.000001f;
    }

    void SuperResolution::UpdatePipelineInfo()
    {
    }

    void SuperResolution::CreateUniforms(CommandBuffer *cmd)
    {
    }

    void SuperResolution::UpdateDescriptorSets()
    {
    }

    void SuperResolution::Update(Camera *camera)
    {
        // GenerateJitter in main camera update
    }

    void SuperResolution::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->BeginDebugRegion("Super Resolution");

        // SUPER RESOLUTION
        // Input
        cmd->ImageBarrier(m_viewportRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(m_velocityRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(m_depth, ImageLayout::ShaderReadOnly);
        // Output
        cmd->ImageBarrier(m_display, ImageLayout::General);

        Camera &camera = *CONTEXT->GetSystem<CameraSystem>()->GetCamera(0);

        FfxFsr2DispatchDescription dd = {};
        dd.commandList = ffxGetCommandListVK(cmd->Handle());
        dd.color = ffxGetTextureResourceVK(m_context.get(),
                                           m_viewportRT->Handle(),
                                           m_viewportRT->GetImageView(),
                                           m_viewportRT->imageInfo.width,
                                           m_viewportRT->imageInfo.height,
                                           Translate<VkFormat>(m_viewportRT->imageInfo.format),
                                           L"FSR2_Input");
        dd.depth = ffxGetTextureResourceVK(m_context.get(),
                                           m_depth->Handle(),
                                           m_depth->GetImageView(),
                                           m_depth->imageInfo.width,
                                           m_depth->imageInfo.height,
                                           Translate<VkFormat>(m_depth->imageInfo.format),
                                           L"FSR2_Depth");
        dd.motionVectors = ffxGetTextureResourceVK(m_context.get(),
                                                   m_velocityRT->Handle(),
                                                   m_velocityRT->GetImageView(),
                                                   m_velocityRT->imageInfo.width,
                                                   m_velocityRT->imageInfo.height,
                                                   Translate<VkFormat>(m_velocityRT->imageInfo.format),
                                                   L"FSR2_Velocity");
        dd.exposure = ffxGetTextureResourceVK(m_context.get(),
                                              nullptr,
                                              nullptr,
                                              1,
                                              1,
                                              VK_FORMAT_UNDEFINED,
                                              L"FSR2_Exposure");
        dd.reactive = ffxGetTextureResourceVK(m_context.get(),
                                              nullptr,
                                              nullptr,
                                              1,
                                              1,
                                              VK_FORMAT_UNDEFINED,
                                              L"FSR2_Reactive");
        dd.transparencyAndComposition = ffxGetTextureResourceVK(m_context.get(),
                                                                nullptr,
                                                                nullptr,
                                                                1,
                                                                1,
                                                                VK_FORMAT_UNDEFINED,
                                                                L"FSR2_TransparencyAndComposition");
        dd.output = ffxGetTextureResourceVK(m_context.get(),
                                            m_display->Handle(),
                                            m_display->GetImageView(),
                                            m_display->imageInfo.width,
                                            m_display->imageInfo.height,
                                            Translate<VkFormat>(m_display->imageInfo.format),
                                            L"FSR2_Output",
                                            FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        dd.jitterOffset.x = m_jitter.x;
        dd.jitterOffset.y = m_jitter.y;
        dd.motionVectorScale.x = GUI::FSR2_MotionScaleX * m_viewportRT->width_f;
        dd.motionVectorScale.y = GUI::FSR2_MotionScaleY * m_viewportRT->height_f;
        dd.renderSize.width = m_viewportRT->imageInfo.width;
        dd.renderSize.height = m_viewportRT->imageInfo.height;
        dd.enableSharpening = true;
        dd.sharpness = 1.0f - (GUI::renderTargetsScale * GUI::renderTargetsScale);
        dd.frameTimeDelta = static_cast<float>(MILLI(FrameTimer::Instance().GetDelta()));
        dd.preExposure = 1.0f;
        dd.reset = false;
        dd.cameraNear = GlobalSettings::ReverseZ ? camera.farPlane : camera.nearPlane;
        dd.cameraFar = GlobalSettings::ReverseZ ? camera.nearPlane : camera.farPlane;
        dd.cameraFovAngleVertical = camera.Fovy();

        PE_CHECK(ffxFsr2ContextDispatch(m_context.get(), &dd));

        cmd->ImageBarrier(m_display, ImageLayout::ColorAttachment);

        cmd->EndDebugRegion();
    }

    void SuperResolution::Resize(uint32_t width, uint32_t height)
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        m_viewportRT = rs->GetRenderTarget("viewport");
        m_velocityRT = rs->GetRenderTarget("velocity");
        m_depth = rs->GetDepthTarget("depth");
        m_display = rs->GetRenderTarget("display");
    }

    void SuperResolution::Destroy()
    {
        ffxFsr2ContextDestroy(m_context.get());
    }

    void SuperResolution::GenerateJitter()
    {
        int32_t phaseCount = ffxFsr2GetJitterPhaseCount(m_viewportRT->imageInfo.width, m_display->imageInfo.width);
        ffxFsr2GetJitterOffset(&m_jitter.x, &m_jitter.y, RHII.GetFrameCounter(), phaseCount);

        m_projectionJitter.x = GUI::FSR2_ProjScaleX * 2.0f * m_jitter.x / m_viewportRT->width_f;
        m_projectionJitter.y = GUI::FSR2_ProjScaleY * 2.0f * m_jitter.y / m_viewportRT->height_f;
    }
}