#include "SSAOPass.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "API/Vulkan/VulkanImageViewImpl.h"
#include "CACAO/ffx_cacao_impl.h"
#include "Camera/Camera.h"
#include "Systems/RendererSystem.h"

#if defined(PE_WIN32)
#include "API/DX12/Dx12CommandBufferImpl.h"
#include "API/DX12/Dx12ImageImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12Translate.h"
#endif

namespace pe
{
    FFX_CACAO_VkContext *m_context = nullptr;
#if defined(PE_WIN32)
    FFX_CACAO_D3D12Context *m_d3d12Context = nullptr;

    namespace
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC MakeSrvDesc2D(DXGI_FORMAT format)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
            desc.Format = format;
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            desc.Texture2D.MostDetailedMip = 0;
            desc.Texture2D.MipLevels = 1;
            return desc;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC MakeDepthSrvDesc2D(DXGI_FORMAT format)
        {
            return MakeSrvDesc2D(pe_dx12::DepthToSRVFormat(format));
        }

        D3D12_UNORDERED_ACCESS_VIEW_DESC MakeUavDesc2D(DXGI_FORMAT format)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
            desc.Format = format;
            desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MipSlice = 0;
            return desc;
        }

        void ResyncSsaoStateAfterCacaoDraw(Image *ssaoRT)
        {
            if (Dx12ImageImpl *impl = Dx12ImageImpl::TryFrom(ssaoRT))
                impl->m_state = D3D12_RESOURCE_STATE_GENERIC_READ;
        }

        void TransitionSsaoForCacaoDraw(CommandBuffer *cmd, Image *ssaoRT)
        {
            Dx12ImageImpl *impl = Dx12ImageImpl::From(ssaoRT);
            constexpr D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            if (impl->m_state == after)
                return;

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = impl->GetResource();
            barrier.Transition.StateBefore = impl->m_state;
            barrier.Transition.StateAfter = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            GetDx12CommandList(cmd)->ResourceBarrier(1, &barrier);
            impl->m_state = after;
        }
    } // namespace
#endif

    void SSAOPass::Init()
    {
        const bool isDx12 = RHII.GetApi() == PE_GRAPHICS_API_DX12;
        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        m_ssaoRT = rs->GetRenderTarget("ssao");
        m_normalRT = rs->GetRenderTarget("normal");
        m_depth = rs->GetDepthStencilTarget("depthStencil");

#if defined(PE_WIN32)
        if (isDx12)
        {
            if (!m_d3d12Context)
            {
                size_t contextSize = FFX_CACAO_D3D12GetContextSize();
                m_d3d12Context = (FFX_CACAO_D3D12Context *)malloc(contextSize);
                assert(m_d3d12Context);
                PE_INFO("SSAO: initializing FFX-CACAO D3D12 context");
                PE_CHECK(FFX_CACAO_D3D12InitContext(m_d3d12Context, GetDx12Device()));

                Dx12ImageImpl *depthImpl = Dx12ImageImpl::From(m_depth);
                Dx12ImageImpl *normalImpl = Dx12ImageImpl::From(m_normalRT);
                Dx12ImageImpl *ssaoImpl = Dx12ImageImpl::From(m_ssaoRT);

                FFX_CACAO_D3D12ScreenSizeInfo screenSizeInfo{};
                screenSizeInfo.width = m_ssaoRT->GetWidth();
                screenSizeInfo.height = m_ssaoRT->GetHeight();
                screenSizeInfo.depthBufferResource = depthImpl->GetResource();
                screenSizeInfo.depthBufferSrvDesc = MakeDepthSrvDesc2D(depthImpl->GetViewFormat());
                screenSizeInfo.normalBufferResource = normalImpl->GetResource();
                screenSizeInfo.normalBufferSrvDesc = MakeSrvDesc2D(normalImpl->GetViewFormat());
                screenSizeInfo.outputResource = ssaoImpl->GetResource();
                screenSizeInfo.outputUavDesc = MakeUavDesc2D(ssaoImpl->GetViewFormat());
                screenSizeInfo.useDownsampledSsao = FFX_CACAO_FALSE;
                PE_INFO("SSAO: initializing FFX-CACAO D3D12 screen resources");
                PE_CHECK(FFX_CACAO_D3D12InitScreenSizeDependentResources(m_d3d12Context, &screenSizeInfo));
            }
            return;
        }
#endif

        if (!m_context)
        {
            size_t ffxCacaoContextSize = FFX_CACAO_VkGetContextSize();
            m_context = (FFX_CACAO_VkContext *)malloc(ffxCacaoContextSize);
            assert(m_context);
            FFX_CACAO_VkCreateInfo info = {};
            info.physicalDevice = VulkanRhi::Gpu();
            info.device = VulkanRhi::Device();
            info.flags = FFX_CACAO_VK_CREATE_USE_DEBUG_MARKERS | FFX_CACAO_VK_CREATE_NAME_OBJECTS; // | FFX_CACAO_VK_CREATE_USE_16_BIT;
            PE_CHECK(FFX_CACAO_VkInitContext(m_context, &info));

            FFX_CACAO_VkScreenSizeInfo screenSizeInfo = {};
            screenSizeInfo.width = m_ssaoRT->GetWidth();
            screenSizeInfo.height = m_ssaoRT->GetHeight();
            screenSizeInfo.depthView = pe::GetVulkanImageView(m_depth->GetSRV());
            screenSizeInfo.normalsView = pe::GetVulkanImageView(m_normalRT->GetSRV());
            screenSizeInfo.output = pe::GetVulkanImage(m_ssaoRT);
            screenSizeInfo.outputView = pe::GetVulkanImageView(m_ssaoRT->GetRTV());
            PE_CHECK(FFX_CACAO_VkInitScreenSizeDependentResources(m_context, &screenSizeInfo));
        }
    }

    void SSAOPass::Update()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        if (gSettings.ssao)
        {
            const bool isDx12 = RHII.GetApi() == PE_GRAPHICS_API_DX12;
            Camera *camera = GetGlobalSystem<RendererSystem>()->GetScene().GetActiveCamera();
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
                /* shadowMultiplier                  */ 0.3f,
                /* shadowPower                       */ 1.5f,
                /* shadowClamp                       */ 0.95f,
                /* horizonAngleThreshold             */ 0.06f,
                /* fadeOutFrom                       */ 50.0f,
                /* fadeOutTo                         */ 300.0f,
                /* qualityLevel                      */ FFX_CACAO_QUALITY_MEDIUM,
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

#if defined(PE_WIN32)
            if (isDx12 && m_d3d12Context)
            {
                PE_CHECK(FFX_CACAO_D3D12UpdateSettings(m_d3d12Context, &cacaoSettings));
                return;
            }
#endif
            PE_CHECK(FFX_CACAO_VkUpdateSettings(m_context, &cacaoSettings));
        }
    }

    void SSAOPass::DeclareInputs(RGBuilder &builder)
    {
        builder.ReadCompute(m_normalRT);
        builder.ReadCompute(m_depth);
        builder.WriteCompute(m_ssaoRT);
    }

    void SSAOPass::DeclareOutputs(RGBuilder &builder)
    {
        builder.OutputCustom(m_ssaoRT,
                             PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             PE_STAGE_COMPUTE_SHADER,
                             PE_ACCESS_SHADER_READ);
    }

    void SSAOPass::ExecutePass(CommandBuffer *cmd)
    {
        const bool isDx12 = RHII.GetApi() == PE_GRAPHICS_API_DX12;

        MemoryBarrierInfo barrier{};
        barrier.srcAccessMask = PE_ACCESS_SHADER_WRITE;
        barrier.dstAccessMask = PE_ACCESS_NONE;
        barrier.srcStageMask = PE_STAGE_COMPUTE_SHADER;
        barrier.dstStageMask = PE_STAGE_ALL_COMMANDS;

        cmd->BeginDebugRegion("SSAOPass");
        cmd->MemoryBarrier(barrier);

#if defined(PE_WIN32)
        if (isDx12)
        {
            Dx12CommandBufferImpl *cmdImpl = Dx12CommandBufferImpl::From(cmd);
            cmdImpl->FlushBarriers();
            TransitionSsaoForCacaoDraw(cmd, m_ssaoRT);
            PE_CHECK(FFX_CACAO_D3D12Draw(m_d3d12Context, GetDx12CommandList(cmd), &m_proj, &m_normalsToView));
            cmdImpl->InvalidateShaderVisibleHeapBinding();
            ResyncSsaoStateAfterCacaoDraw(m_ssaoRT);
            cmd->EndDebugRegion();
            return;
        }
#endif

        PE_CHECK(FFX_CACAO_VkDraw(m_context, GetVulkanCommandBuffer(cmd), &m_proj, &m_normalsToView));
        cmd->EndDebugRegion();
    }

    void SSAOPass::Resize(uint32_t width, uint32_t height)
    {
        const bool isDx12 = RHII.GetApi() == PE_GRAPHICS_API_DX12;

        RendererSystem *rs = GetGlobalSystem<RendererSystem>();
        m_ssaoRT = rs->GetRenderTarget("ssao");
        m_normalRT = rs->GetRenderTarget("normal");
        m_depth = rs->GetDepthStencilTarget("depthStencil");

#if defined(PE_WIN32)
        if (isDx12)
        {
            PE_CHECK(FFX_CACAO_D3D12DestroyScreenSizeDependentResources(m_d3d12Context));

            Dx12ImageImpl *depthImpl = Dx12ImageImpl::From(m_depth);
            Dx12ImageImpl *normalImpl = Dx12ImageImpl::From(m_normalRT);
            Dx12ImageImpl *ssaoImpl = Dx12ImageImpl::From(m_ssaoRT);

            FFX_CACAO_D3D12ScreenSizeInfo screenSizeInfo{};
            screenSizeInfo.width = m_ssaoRT->GetWidth();
            screenSizeInfo.height = m_ssaoRT->GetHeight();
            screenSizeInfo.depthBufferResource = depthImpl->GetResource();
            screenSizeInfo.depthBufferSrvDesc = MakeDepthSrvDesc2D(depthImpl->GetViewFormat());
            screenSizeInfo.normalBufferResource = normalImpl->GetResource();
            screenSizeInfo.normalBufferSrvDesc = MakeSrvDesc2D(normalImpl->GetViewFormat());
            screenSizeInfo.outputResource = ssaoImpl->GetResource();
            screenSizeInfo.outputUavDesc = MakeUavDesc2D(ssaoImpl->GetViewFormat());
            screenSizeInfo.useDownsampledSsao = FFX_CACAO_FALSE;
            PE_CHECK(FFX_CACAO_D3D12InitScreenSizeDependentResources(m_d3d12Context, &screenSizeInfo));
            return;
        }
#endif

        PE_CHECK(FFX_CACAO_VkDestroyScreenSizeDependentResources(m_context));

        FFX_CACAO_VkScreenSizeInfo screenSizeInfo = {};
        screenSizeInfo.width = m_ssaoRT->GetWidth();
        screenSizeInfo.height = m_ssaoRT->GetHeight();
        screenSizeInfo.depthView = pe::GetVulkanImageView(m_depth->GetSRV());
        screenSizeInfo.normalsView = pe::GetVulkanImageView(m_normalRT->GetSRV());
        screenSizeInfo.output = pe::GetVulkanImage(m_ssaoRT);
        screenSizeInfo.outputView = pe::GetVulkanImageView(m_ssaoRT->GetRTV());
        PE_CHECK(FFX_CACAO_VkInitScreenSizeDependentResources(m_context, &screenSizeInfo));
    }

    void SSAOPass::Destroy()
    {
#if defined(PE_WIN32)
        if (m_d3d12Context)
        {
            PE_CHECK(FFX_CACAO_D3D12DestroyScreenSizeDependentResources(m_d3d12Context));
            PE_CHECK(FFX_CACAO_D3D12DestroyContext(m_d3d12Context));
            free(m_d3d12Context);
            m_d3d12Context = nullptr;
            return;
        }
#endif
        if (m_context)
        {
            PE_CHECK(FFX_CACAO_VkDestroyScreenSizeDependentResources(m_context));
            PE_CHECK(FFX_CACAO_VkDestroyContext(m_context));
            free(m_context);
            m_context = nullptr;
        }
    }
} // namespace pe
