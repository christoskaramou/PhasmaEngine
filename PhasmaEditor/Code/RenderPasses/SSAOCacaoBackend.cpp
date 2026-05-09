#include "SSAOCacaoBackend.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "API/Vulkan/VulkanImageViewImpl.h"
#include "CACAO/ffx_cacao_impl.h"

#if defined(PE_WIN32)
#include "API/DX12/Dx12CommandBufferImpl.h"
#include "API/DX12/Dx12ImageImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12Translate.h"
#endif

#include <cstring>

namespace pe::SSAOCacaoBackend
{
    namespace
    {
        FFX_CACAO_VkContext *s_vkContext = nullptr;

#if defined(PE_WIN32)
        FFX_CACAO_D3D12Context *s_d3d12Context = nullptr;

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

        void InitD3D12ScreenResources(Image *depth, Image *normalRT, Image *ssaoRT)
        {
            Dx12ImageImpl *depthImpl = Dx12ImageImpl::From(depth);
            Dx12ImageImpl *normalImpl = Dx12ImageImpl::From(normalRT);
            Dx12ImageImpl *ssaoImpl = Dx12ImageImpl::From(ssaoRT);

            FFX_CACAO_D3D12ScreenSizeInfo screenSizeInfo{};
            screenSizeInfo.width = ssaoRT->GetWidth();
            screenSizeInfo.height = ssaoRT->GetHeight();
            screenSizeInfo.depthBufferResource = depthImpl->GetResource();
            screenSizeInfo.depthBufferSrvDesc = MakeDepthSrvDesc2D(depthImpl->GetViewFormat());
            screenSizeInfo.normalBufferResource = normalImpl->GetResource();
            screenSizeInfo.normalBufferSrvDesc = MakeSrvDesc2D(normalImpl->GetViewFormat());
            screenSizeInfo.outputResource = ssaoImpl->GetResource();
            screenSizeInfo.outputUavDesc = MakeUavDesc2D(ssaoImpl->GetViewFormat());
            screenSizeInfo.useDownsampledSsao = FFX_CACAO_FALSE;
            PE_CHECK(FFX_CACAO_D3D12InitScreenSizeDependentResources(s_d3d12Context, &screenSizeInfo));
        }
#endif

        FFX_CACAO_Matrix4x4 ToCacaoMatrix(mat4 matrix)
        {
            FFX_CACAO_Matrix4x4 result{};
            std::memcpy(&result.elements[0][0], &matrix[0].x, sizeof(result));
            return result;
        }

        FFX_CACAO_Settings DefaultCacaoSettings()
        {
            return {
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
        }

        void InitVulkanScreenResources(Image *depth, Image *normalRT, Image *ssaoRT)
        {
            FFX_CACAO_VkScreenSizeInfo screenSizeInfo{};
            screenSizeInfo.width = ssaoRT->GetWidth();
            screenSizeInfo.height = ssaoRT->GetHeight();
            screenSizeInfo.depthView = pe::GetVulkanImageView(depth->GetSRV());
            screenSizeInfo.normalsView = pe::GetVulkanImageView(normalRT->GetSRV());
            screenSizeInfo.output = pe::GetVulkanImage(ssaoRT);
            screenSizeInfo.outputView = pe::GetVulkanImageView(ssaoRT->GetRTV());
            PE_CHECK(FFX_CACAO_VkInitScreenSizeDependentResources(s_vkContext, &screenSizeInfo));
        }
    } // namespace

    void Init(Image *depth, Image *normalRT, Image *ssaoRT)
    {
        const PeGraphicsApi api = RHII.GetApi();

#if defined(PE_WIN32)
        if (api == PE_GRAPHICS_API_DX12)
        {
            if (!s_d3d12Context)
            {
                const size_t contextSize = FFX_CACAO_D3D12GetContextSize();
                s_d3d12Context = static_cast<FFX_CACAO_D3D12Context *>(std::malloc(contextSize));
                assert(s_d3d12Context);
                PE_INFO("SSAO: initializing FFX-CACAO D3D12 context");
                PE_CHECK(FFX_CACAO_D3D12InitContext(s_d3d12Context, GetDx12Device()));
                PE_INFO("SSAO: initializing FFX-CACAO D3D12 screen resources");
                InitD3D12ScreenResources(depth, normalRT, ssaoRT);
            }
            return;
        }
#endif

        if (api == PE_GRAPHICS_API_VULKAN && !s_vkContext)
        {
            const size_t contextSize = FFX_CACAO_VkGetContextSize();
            s_vkContext = static_cast<FFX_CACAO_VkContext *>(std::malloc(contextSize));
            assert(s_vkContext);

            FFX_CACAO_VkCreateInfo info{};
            info.physicalDevice = VulkanRhi::Gpu();
            info.device = VulkanRhi::Device();
            info.flags = FFX_CACAO_VK_CREATE_USE_DEBUG_MARKERS | FFX_CACAO_VK_CREATE_NAME_OBJECTS;
            PE_CHECK(FFX_CACAO_VkInitContext(s_vkContext, &info));
            InitVulkanScreenResources(depth, normalRT, ssaoRT);
        }
    }

    void UpdateSettings()
    {
        const FFX_CACAO_Settings cacaoSettings = DefaultCacaoSettings();

#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
            PE_ERROR_IF(!s_d3d12Context, "SSAOCacaoBackend::UpdateSettings: D3D12 context is not initialized");
            PE_CHECK(FFX_CACAO_D3D12UpdateSettings(s_d3d12Context, &cacaoSettings));
            return;
        }
#endif

        PE_ERROR_IF(!s_vkContext, "SSAOCacaoBackend::UpdateSettings: Vulkan context is not initialized");
        PE_CHECK(FFX_CACAO_VkUpdateSettings(s_vkContext, &cacaoSettings));
    }

    void Draw(CommandBuffer *cmd, Image *ssaoRT, mat4 projection, mat4 normalsToView)
    {
        const FFX_CACAO_Matrix4x4 cacaoProjection = ToCacaoMatrix(projection);
        const FFX_CACAO_Matrix4x4 cacaoNormalsToView = ToCacaoMatrix(normalsToView);

#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
            PE_ERROR_IF(!s_d3d12Context, "SSAOCacaoBackend::Draw: D3D12 context is not initialized");

            Dx12CommandBufferImpl *cmdImpl = Dx12CommandBufferImpl::From(cmd);
            cmdImpl->FlushBarriers();
            TransitionSsaoForCacaoDraw(cmd, ssaoRT);
            PE_CHECK(FFX_CACAO_D3D12Draw(s_d3d12Context, GetDx12CommandList(cmd), &cacaoProjection, &cacaoNormalsToView));
            cmdImpl->InvalidateShaderVisibleHeapBinding();
            ResyncSsaoStateAfterCacaoDraw(ssaoRT);
            return;
        }
#endif

        PE_ERROR_IF(!s_vkContext, "SSAOCacaoBackend::Draw: Vulkan context is not initialized");
        PE_CHECK(FFX_CACAO_VkDraw(s_vkContext, GetVulkanCommandBuffer(cmd), &cacaoProjection, &cacaoNormalsToView));
    }

    void Resize(Image *depth, Image *normalRT, Image *ssaoRT)
    {
#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
            PE_ERROR_IF(!s_d3d12Context, "SSAOCacaoBackend::Resize: D3D12 context is not initialized");
            PE_CHECK(FFX_CACAO_D3D12DestroyScreenSizeDependentResources(s_d3d12Context));
            InitD3D12ScreenResources(depth, normalRT, ssaoRT);
            return;
        }
#endif

        PE_ERROR_IF(!s_vkContext, "SSAOCacaoBackend::Resize: Vulkan context is not initialized");
        PE_CHECK(FFX_CACAO_VkDestroyScreenSizeDependentResources(s_vkContext));
        InitVulkanScreenResources(depth, normalRT, ssaoRT);
    }

    void Destroy()
    {
#if defined(PE_WIN32)
        if (s_d3d12Context)
        {
            PE_CHECK(FFX_CACAO_D3D12DestroyScreenSizeDependentResources(s_d3d12Context));
            PE_CHECK(FFX_CACAO_D3D12DestroyContext(s_d3d12Context));
            std::free(s_d3d12Context);
            s_d3d12Context = nullptr;
        }
#endif

        if (s_vkContext)
        {
            PE_CHECK(FFX_CACAO_VkDestroyScreenSizeDependentResources(s_vkContext));
            PE_CHECK(FFX_CACAO_VkDestroyContext(s_vkContext));
            std::free(s_vkContext);
            s_vkContext = nullptr;
        }
    }
} // namespace pe::SSAOCacaoBackend
