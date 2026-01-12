#include "SSAOPass.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "CACAO/ffx_cacao_impl.h"
#include "Camera/Camera.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    FFX_CACAO_VkContext *m_context = nullptr;

    void SSAOPass::Init()
    {
        if (!m_context)
        {
            RendererSystem *rs = GetGlobalSystem<RendererSystem>();
            m_ssaoRT = rs->GetRenderTarget("ssao");
            m_normalRT = rs->GetRenderTarget("normal");
            m_depth = rs->GetDepthStencilTarget("depthStencil");

            size_t ffxCacaoContextSize = FFX_CACAO_VkGetContextSize();
            m_context = (FFX_CACAO_VkContext *)malloc(ffxCacaoContextSize);
            assert(m_context);
            FFX_CACAO_VkCreateInfo info = {};
            info.physicalDevice = RHII.GetGpu();
            info.device = RHII.GetDevice();
            info.flags = FFX_CACAO_VK_CREATE_USE_DEBUG_MARKERS | FFX_CACAO_VK_CREATE_NAME_OBJECTS; // | FFX_CACAO_VK_CREATE_USE_16_BIT;
            PE_CHECK(FFX_CACAO_VkInitContext(m_context, &info));

            FFX_CACAO_VkScreenSizeInfo screenSizeInfo = {};
            screenSizeInfo.width = m_ssaoRT->GetWidth();
            screenSizeInfo.height = m_ssaoRT->GetHeight();
            screenSizeInfo.depthView = m_depth->GetSRV()->ApiHandle();
            screenSizeInfo.normalsView = m_normalRT->GetSRV()->ApiHandle();
            screenSizeInfo.output = m_ssaoRT->ApiHandle();
            screenSizeInfo.outputView = m_ssaoRT->GetRTV()->ApiHandle();
            PE_CHECK(FFX_CACAO_VkInitScreenSizeDependentResources(m_context, &screenSizeInfo));
        }
    }

    void SSAOPass::Update()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        if (gSettings.ssao)
        {
            Camera *camera = GetGlobalSystem<RendererSystem>()->GetScene().GetCamera(0);
            mat4 projection = camera->GetProjection();
            memcpy(&m_proj.elements[0][0], &projection[0].x, sizeof(m_proj));

            static const mat4 biasMat = mat4(
                1.0, 0.0, 0.0, 0.0,
                0.0, -1.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 0.0, 0.0, 1.0);

            mat4 normalToView = transpose(biasMat * camera->GetInvView());
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

    void SSAOPass::ExecutePass(CommandBuffer *cmd)
    {
        vk::MemoryBarrier2 barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits2::eNone;
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;

        std::vector<ImageBarrierInfo> barriers(3);
        barriers[0].image = m_normalRT;
        barriers[0].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[0].stageFlags = vk::PipelineStageFlagBits2::eComputeShader;
        barriers[0].accessMask = vk::AccessFlagBits2::eShaderSampledRead;
        barriers[1].image = m_depth;
        barriers[1].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barriers[1].stageFlags = vk::PipelineStageFlagBits2::eComputeShader;
        barriers[1].accessMask = vk::AccessFlagBits2::eShaderSampledRead;
        barriers[2].image = m_ssaoRT;
        barriers[2].layout = vk::ImageLayout::eGeneral;
        barriers[2].stageFlags = vk::PipelineStageFlagBits2::eComputeShader;
        barriers[2].accessMask = vk::AccessFlagBits2::eShaderWrite;

        cmd->BeginDebugRegion("SSAOPass");
        cmd->MemoryBarrier(barrier);
        cmd->ImageBarriers(barriers);
        cmd->BeginDebugRegion("SSAO_Pass");
        PE_CHECK(FFX_CACAO_VkDraw(m_context, cmd->ApiHandle(), &m_proj, &m_normalsToView));
        cmd->EndDebugRegion();
        cmd->EndDebugRegion();

        // image barrier transition happens in draw, so set to correct layout
        ImageBarrierInfo ssaoInfo{};
        ssaoInfo.image = m_ssaoRT;
        ssaoInfo.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        ssaoInfo.stageFlags = vk::PipelineStageFlagBits2::eComputeShader;
        ssaoInfo.accessMask = vk::AccessFlagBits2::eShaderRead;
        m_ssaoRT->SetCurrentInfoAll(ssaoInfo);
    }

    void SSAOPass::Resize(uint32_t width, uint32_t height)
    {
        PE_CHECK(FFX_CACAO_VkDestroyScreenSizeDependentResources(m_context));

        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        m_ssaoRT = rs->GetRenderTarget("ssao");
        m_normalRT = rs->GetRenderTarget("normal");
        m_depth = rs->GetDepthStencilTarget("depthStencil");

        FFX_CACAO_VkScreenSizeInfo screenSizeInfo = {};
        screenSizeInfo.width = m_ssaoRT->GetWidth();
        screenSizeInfo.height = m_ssaoRT->GetHeight();
        screenSizeInfo.depthView = m_depth->GetSRV()->ApiHandle();
        screenSizeInfo.normalsView = m_normalRT->GetSRV()->ApiHandle();
        screenSizeInfo.output = m_ssaoRT->ApiHandle();
        screenSizeInfo.outputView = m_ssaoRT->GetRTV()->ApiHandle();
        PE_CHECK(FFX_CACAO_VkInitScreenSizeDependentResources(m_context, &screenSizeInfo));
    }

    void SSAOPass::Destroy()
    {
        PE_CHECK(FFX_CACAO_VkDestroyScreenSizeDependentResources(m_context));
        PE_CHECK(FFX_CACAO_VkDestroyContext(m_context));
        free(m_context);
        m_context = nullptr;
    }
} // namespace pe
