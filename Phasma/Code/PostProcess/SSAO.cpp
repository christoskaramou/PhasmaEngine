#include "SSAO.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "GUI/GUI.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Queue.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Buffer.h"
#include "Systems/CameraSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    SSAO::SSAO()
    {
    }

    SSAO::~SSAO()
    {
    }

    void SSAO::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();
        ssaoRT = rs->GetRenderTarget("ssao");
        ssaoBlurRT = rs->GetRenderTarget("ssaoBlur");
        normalRT = rs->GetRenderTarget("normal");
        depth = rs->GetDepthTarget("depth");

        size_t ffxCacaoContextSize = FFX_CACAO_VkGetContextSize();
        m_context = (FFX_CACAO_VkContext *)malloc(ffxCacaoContextSize);
        assert(m_context);
        FFX_CACAO_VkCreateInfo info = {};
        info.physicalDevice = RHII.GetGpu();
        info.device = RHII.GetDevice();
        info.flags = FFX_CACAO_VK_CREATE_USE_16_BIT | FFX_CACAO_VK_CREATE_USE_DEBUG_MARKERS | FFX_CACAO_VK_CREATE_NAME_OBJECTS;
        PE_CHECK(FFX_CACAO_VkInitContext(m_context, &info));

        FFX_CACAO_VkScreenSizeInfo screenSizeInfo = {};
        screenSizeInfo.width = ssaoBlurRT->imageInfo.width;
        screenSizeInfo.height = ssaoBlurRT->imageInfo.height;
        screenSizeInfo.depthView = depth->GetSRV();
        screenSizeInfo.normalsView = normalRT->GetSRV();
        screenSizeInfo.output = ssaoBlurRT->Handle();
        screenSizeInfo.outputView = ssaoBlurRT->GetRTV();
        PE_CHECK(FFX_CACAO_VkInitScreenSizeDependentResources(m_context, &screenSizeInfo));
    }

    void SSAO::Update(Camera *camera)
    {
        if (GUI::show_ssao)
        {
            memcpy(&m_proj.elements[0][0], &camera->projection[0].x, sizeof(m_proj));

            static const mat4 biasMat = mat4(
                1.0, 0.0, 0.0, 0.0,
                0.0, -1.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 0.0, 0.0, 1.0);

            mat4 normalToView = transpose(biasMat * camera->invView);
            memcpy(&m_normalsToView.elements[0][0], &normalToView[0].x, sizeof(m_normalsToView));

            // radius                               world view radius of the occlusion sphere
            // shadowMultiplier                     effect strength linear multiplier
            // shadowPower                          effect strength power multiplier
            // shadowClamp                          effect max limit
            // horizonAngleThreshold                minimum horizon angle for contributions to occlusion to limit self shadowing
            // fadeOutFrom                          effect fade out from world space distance
            // fadeOutTo                            effect fade out to world space distance
            // qualityLevel                         the quality of the effect, ranging from lowest to highest (adaptive). This affects the number of samples taken to generate SSAO.
            // adaptiveQualityLimit                 quality limit for adaptive quality
            // blurPassCount                        a number of edge sensitive blurs from 1 to 8 to perform after SSAO generation
            // sharpness                            how much to bleed over edges - 0 = ignore edges, 1 = don't bleed over edges
            // temporalSupersamplingAngleOffset     sampling angle offset for temporal super sampling
            // temporalSupersamplingRadiusOffset    sampling effect radius offset for temporal super sampling
            // detailShadowStrength                 used to generate details in high res AO
            // generateNormals                      should the effect generate normals from the depth buffer or use a provided normal buffer
            // bilateralSigmaSquared                a parameter for use in bilateral upsampling. Higher values create more blur to help reduce noise
            // bilateralSimilarityDistanceSigma     a parameter for use in bilateral upsampling. Lower values create reduce bluring across edge boundaries

            static const FFX_CACAO_Settings cacaoSettings = {
                /* radius                            */ 1.2f,
                /* shadowMultiplier                  */ 0.75f,
                /* shadowPower                       */ 1.5f,
                /* shadowClamp                       */ 0.95f,
                /* horizonAngleThreshold             */ 0.06f,
                /* fadeOutFrom                       */ 50.0f,
                /* fadeOutTo                         */ 300.0f,
                /* qualityLevel                      */ FFX_CACAO_QUALITY_HIGHEST,
                /* adaptiveQualityLimit              */ 0.45f,
                /* blurPassCount                     */ 2,
                /* sharpness                         */ 0.98f,
                /* temporalSupersamplingAngleOffset  */ 0.0f,
                /* temporalSupersamplingRadiusOffset */ 0.0f,
                /* detailShadowStrength              */ 0.5f,
                /* generateNormals                   */ FFX_CACAO_FALSE,
                /* bilateralSigmaSquared             */ 5.0f,
                /* bilateralSimilarityDistanceSigma  */ 0.01f,
            };

            PE_CHECK(FFX_CACAO_VkUpdateSettings(m_context, &cacaoSettings));
        }
    }

    void SSAO::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->BeginDebugRegion("SSAO");

        cmd->ImageBarrier(normalRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(depth, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(ssaoBlurRT, ImageLayout::General);

        PE_CHECK(FFX_CACAO_VkDraw(m_context, cmd->Handle(), &m_proj, &m_normalsToView));

        // image barrier transition happens in draw, so set to correct layout
        for (uint32_t i = 0; i < ssaoBlurRT->imageInfo.arrayLayers; i++)
            for (uint32_t j = 0; j < ssaoBlurRT->imageInfo.mipLevels; j++)
                ssaoBlurRT->SetCurrentLayout(ImageLayout::ShaderReadOnly, i, j);
                
        cmd->ImageBarrier(ssaoBlurRT, ImageLayout::General);


        cmd->EndDebugRegion();
    }

    void SSAO::Resize(uint32_t width, uint32_t height)
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();
        ssaoRT = rs->GetRenderTarget("ssao");
        ssaoBlurRT = rs->GetRenderTarget("ssaoBlur");
        normalRT = rs->GetRenderTarget("normal");
        depth = rs->GetDepthTarget("depth");

        FFX_CACAO_VkScreenSizeInfo screenSizeInfo = {};
        screenSizeInfo.width = ssaoBlurRT->imageInfo.width;
        screenSizeInfo.height = ssaoBlurRT->imageInfo.height;
        screenSizeInfo.depthView = depth->GetSRV();
        screenSizeInfo.normalsView = normalRT->GetSRV();
        screenSizeInfo.output = ssaoBlurRT->Handle();
        screenSizeInfo.outputView = ssaoBlurRT->GetRTV();
        PE_CHECK(FFX_CACAO_VkInitScreenSizeDependentResources(m_context, &screenSizeInfo));
    }

    void SSAO::Destroy()
    {

        PE_CHECK(FFX_CACAO_VkDestroyContext(m_context));
        free(m_context);
    }
}
