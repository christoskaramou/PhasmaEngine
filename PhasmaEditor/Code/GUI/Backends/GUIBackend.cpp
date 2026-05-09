#include "GUI/Backends/GUIBackend.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/RenderPass.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"
#include "API/Vulkan/VulkanDescriptorImpl.h"
#include "API/Vulkan/VulkanImageViewImpl.h"
#include "API/Vulkan/VulkanQueueImpl.h"
#include "API/Vulkan/VulkanRenderPassImpl.h"
#include "API/Vulkan/VulkanSamplerImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12CommandBufferImpl.h"
#include "API/DX12/Dx12DescriptorHeap.h"
#include "API/DX12/Dx12ImageViewImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12Translate.h"
#include "imgui_impl_dx12.h"
#endif
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_vulkan.h"
#include <cstdint>
#include <unordered_map>

namespace pe::GUIBackend
{
#if defined(PE_WIN32)
    namespace
    {
        std::unordered_map<uint64_t, uint32_t> s_dx12ImGuiSlots;
    }
#endif

    bool IsSupported()
    {
        const PeGraphicsApi api = RHII.GetApi();
#if defined(PE_WIN32)
        return api == PE_GRAPHICS_API_VULKAN || api == PE_GRAPHICS_API_DX12;
#else
        return api == PE_GRAPHICS_API_VULKAN;
#endif
    }

    bool SupportsPlatformWindows()
    {
        return RHII.GetApi() == PE_GRAPHICS_API_VULKAN;
    }

    void ConfigureIO()
    {
        ImGuiIO &io = ImGui::GetIO();
        if (SupportsPlatformWindows())
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        else
            io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    }

    void Init(Attachment *attachment)
    {
        const PeGraphicsApi api = RHII.GetApi();
        Queue *queue = RHII.GetMainQueue();

        if (api == PE_GRAPHICS_API_VULKAN)
        {
            ImGui_ImplSDL2_InitForVulkan(RHII.GetWindow());

            PE_ERROR_IF(!(ImGui::GetIO().BackendFlags & ImGuiBackendFlags_PlatformHasViewports),
                        "SDL2 backend doesn't support platform viewports!");

            ImGui_ImplVulkan_InitInfo initInfo{};
            initInfo.Instance = VulkanRhi::Instance();
            initInfo.PhysicalDevice = VulkanRhi::Gpu();
            initInfo.Device = VulkanRhi::Device();
            initInfo.QueueFamily = queue->GetFamilyId();
            initInfo.Queue = pe::GetVulkanQueue(queue);
            initInfo.PipelineCache = nullptr;
            initInfo.DescriptorPool = pe::GetVulkanDescriptorPool(RHII.GetDescriptorPool());
            initInfo.Subpass = 0;
            initInfo.MinImageCount = RHII.GetSwapchainImageCount();
            initInfo.ImageCount = RHII.GetSwapchainImageCount();
            initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            initInfo.Allocator = nullptr;
            initInfo.CheckVkResultFn = nullptr;

            RenderPass *renderPass = CommandBuffer::GetRenderPass(1, attachment);
            initInfo.UseDynamicRendering = false;
            initInfo.RenderPass = pe::GetVulkanRenderPass(renderPass);

            ImGui_ImplVulkan_Init(&initInfo);
            return;
        }

#if defined(PE_WIN32)
        if (api == PE_GRAPHICS_API_DX12)
        {
            Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
            PE_ERROR_IF(!rhi || !rhi->GetDevice() || !rhi->GetGraphicsQueue(), "GUIBackend::Init: DX12 RHI is not initialized");
            PE_ERROR_IF(!rhi->GetCbvSrvUavHeap(), "GUIBackend::Init: DX12 CBV/SRV/UAV heap is not initialized");
            PE_ERROR_IF(!attachment || !attachment->image, "GUIBackend::Init: display RT must exist before ImGui DX12 init");

            ImGui_ImplSDL2_InitForD3D(RHII.GetWindow());

            ImGui_ImplDX12_InitInfo initInfo{};
            initInfo.Device = rhi->GetDevice();
            initInfo.CommandQueue = rhi->GetGraphicsQueue();
            initInfo.NumFramesInFlight = static_cast<int>(RHII.GetSwapchainImageCount());
            initInfo.RTVFormat = pe_dx12::Format(attachment->image->GetFormat());
            initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
            initInfo.SrvDescriptorHeap = rhi->GetCbvSrvUavHeap()->Get();
            initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *,
                                               D3D12_CPU_DESCRIPTOR_HANDLE *outCpu,
                                               D3D12_GPU_DESCRIPTOR_HANDLE *outGpu)
            {
                Dx12RhiImpl *dx12 = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
                PE_ERROR_IF(!dx12 || !dx12->GetCbvSrvUavHeap(), "GUI DX12 descriptor allocation requires an initialized heap");

                uint32_t slot = dx12->GetCbvSrvUavHeap()->Allocate();
                *outCpu = dx12->GetCbvSrvUavHeap()->GetCpuHandle(slot);
                *outGpu = dx12->GetCbvSrvUavHeap()->GetGpuHandle(slot);
                s_dx12ImGuiSlots.emplace(static_cast<uint64_t>(outGpu->ptr), slot);
            };
            initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *,
                                              D3D12_CPU_DESCRIPTOR_HANDLE,
                                              D3D12_GPU_DESCRIPTOR_HANDLE gpu)
            {
                Dx12RhiImpl *dx12 = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
                if (!dx12 || !dx12->GetCbvSrvUavHeap())
                    return;

                auto it = s_dx12ImGuiSlots.find(static_cast<uint64_t>(gpu.ptr));
                if (it == s_dx12ImGuiSlots.end())
                    return;

                dx12->GetCbvSrvUavHeap()->Free(it->second);
                s_dx12ImGuiSlots.erase(it);
            };

            ImGui_ImplDX12_Init(&initInfo);
        }
#endif
    }

    void Shutdown()
    {
#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
            ImGui_ImplDX12_Shutdown();
        else
            ImGui_ImplVulkan_Shutdown();
#else
        ImGui_ImplVulkan_Shutdown();
#endif
        ImGui_ImplSDL2_Shutdown();
    }

    void NewFrame()
    {
        ImGui_ImplSDL2_NewFrame();

        if (RHII.GetApi() == PE_GRAPHICS_API_VULKAN)
        {
            ImGui_ImplVulkan_NewFrame();
            return;
        }

#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
            ImGui_ImplDX12_NewFrame();
#endif
    }

    void CreateFontsTexture()
    {
        if (RHII.GetApi() == PE_GRAPHICS_API_VULKAN)
            ImGui_ImplVulkan_CreateFontsTexture();
    }

    void RenderDrawData(CommandBuffer *cmd)
    {
        if (RHII.GetApi() == PE_GRAPHICS_API_VULKAN)
        {
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), GetVulkanCommandBuffer(cmd));
            return;
        }

#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
            Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
            Dx12CommandBufferImpl *dx12Cmd = Dx12CommandBufferImpl::From(cmd);
            ID3D12DescriptorHeap *heap = rhi && rhi->GetCbvSrvUavHeap() ? rhi->GetCbvSrvUavHeap()->Get() : nullptr;
            PE_ERROR_IF(!heap || !dx12Cmd || !dx12Cmd->Get(), "GUIBackend::RenderDrawData: DX12 command state is not initialized");

            dx12Cmd->Get()->SetDescriptorHeaps(1, &heap);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dx12Cmd->Get());
            dx12Cmd->InvalidateShaderVisibleHeapBinding();
        }
#endif
    }

    void *RegisterImageTexture(Image *image)
    {
        if (!image || !image->GetSampler() || !image->GetSRV())
            return nullptr;

        if (RHII.GetApi() == PE_GRAPHICS_API_VULKAN)
        {
            VkSampler sampler = pe::GetVulkanSampler(image->GetSampler());
            VkImageView view = pe::GetVulkanImageView(image->GetSRV());
            if (!sampler || !view)
                return nullptr;

            return (void *)ImGui_ImplVulkan_AddTexture(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

#if defined(PE_WIN32)
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
            Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
            if (!rhi || !rhi->GetDevice() || !rhi->GetCbvSrvUavHeap())
                return nullptr;

            const Dx12ImageViewImpl *srv = Dx12ImageViewImpl::From(image->GetSRV());
            if (!srv)
                return nullptr;

            const uint32_t slot = rhi->GetCbvSrvUavHeap()->Allocate();
            rhi->GetDevice()->CopyDescriptorsSimple(1,
                                                    rhi->GetCbvSrvUavHeap()->GetCpuHandle(slot),
                                                    srv->GetCpuHandle(),
                                                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            const D3D12_GPU_DESCRIPTOR_HANDLE gpu = rhi->GetCbvSrvUavHeap()->GetGpuHandle(slot);
            void *textureID = reinterpret_cast<void *>(gpu.ptr);
            s_dx12ImGuiSlots[static_cast<uint64_t>(gpu.ptr)] = slot;
            return textureID;
        }
#endif

        return nullptr;
    }

    void ReleaseImageTexture(void *&textureID)
    {
        if (!textureID)
            return;

        if (RHII.GetApi() == PE_GRAPHICS_API_VULKAN)
        {
            ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)textureID);
        }
#if defined(PE_WIN32)
        else if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
            const uint64_t key = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(textureID));
            auto it = s_dx12ImGuiSlots.find(key);
            if (it != s_dx12ImGuiSlots.end())
            {
                Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
                if (rhi && rhi->GetCbvSrvUavHeap())
                    rhi->GetCbvSrvUavHeap()->Free(it->second);
                s_dx12ImGuiSlots.erase(it);
            }
        }
#endif

        textureID = nullptr;
    }
} // namespace pe::GUIBackend
