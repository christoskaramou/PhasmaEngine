#include "UI/Backends/ImGuiRuntimeUiBackend.h"
#include "UI/Backends/ImGuiRuntimeUiStyle.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/RenderPass.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"
#include "API/Vulkan/VulkanDescriptorImpl.h"
#include "API/Vulkan/VulkanQueueImpl.h"
#include "API/Vulkan/VulkanRenderPassImpl.h"
#include "UI/RuntimeUiInputEvents.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12CommandBufferImpl.h"
#include "API/DX12/Dx12DescriptorHeap.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12Translate.h"
#include "imgui_impl_dx12.h"
#endif
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include <cstdint>
#include <unordered_map>

namespace pe
{
    namespace
    {
        class ScopedImGuiContext
        {
        public:
            explicit ScopedImGuiContext(ImGuiContext *context) : m_previous(ImGui::GetCurrentContext())
            {
                ImGui::SetCurrentContext(context);
            }

            ~ScopedImGuiContext()
            {
                ImGui::SetCurrentContext(m_previous);
            }

        private:
            ImGuiContext *m_previous = nullptr;
        };

        class ImGuiRuntimeUiBackend final : public IRuntimeUiBackend
        {
        public:
            ~ImGuiRuntimeUiBackend() override
            {
                Shutdown();
            }

            const char *GetName() const override { return "Dear ImGui"; }

            bool IsSupported() const override
            {
                const PeGraphicsApi api = RHII.GetApi();
#if defined(PE_WIN32)
                return api == PE_GRAPHICS_API_VULKAN || api == PE_GRAPHICS_API_DX12;
#else
                return api == PE_GRAPHICS_API_VULKAN;
#endif
            }

            bool Init(const RuntimeUiBackendInitInfo &initInfo) override
            {
                if (m_initialized)
                    return true;
                if (!IsSupported())
                    return false;

                {
                    m_context = ImGui::CreateContext();
                    ScopedImGuiContext contextScope(m_context);

                    ImGuiIO &io = ImGui::GetIO();
                    runtime_ui_imgui::ApplyContextSettings(io);
                    runtime_ui_imgui::ApplyStyle();

                    if (RHII.GetApi() == PE_GRAPHICS_API_VULKAN)
                    {
                        if (!InitVulkan(initInfo.renderTarget))
                        {
                            Shutdown();
                            return false;
                        }
                    }
#if defined(PE_WIN32)
                    else if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
                    {
                        if (!InitDx12(initInfo.renderTarget))
                        {
                            Shutdown();
                            return false;
                        }
                    }
#endif
                    else
                    {
                        Shutdown();
                        return false;
                    }

                    CreateFontsTexture();
                }

                RHII.GetMainQueue()->WaitIdle();
                m_initialized = true;
                return true;
            }

            void Shutdown() override
            {
                if (!m_context)
                    return;

                ImGuiContext *previousContext = ImGui::GetCurrentContext();
                ImGui::SetCurrentContext(m_context);
                if (m_rendererInitialized)
                {
#if defined(PE_WIN32)
                    if (m_api == PE_GRAPHICS_API_DX12)
                        ImGui_ImplDX12_Shutdown();
                    else
                        ImGui_ImplVulkan_Shutdown();
#else
                    ImGui_ImplVulkan_Shutdown();
#endif
                    m_rendererInitialized = false;
                }
                if (m_platformInitialized)
                {
                    ImGui_ImplSDL2_Shutdown();
                    m_platformInitialized = false;
                }
                ImGuiContext *destroyedContext = m_context;
                ImGui::DestroyContext(destroyedContext);
                m_context = nullptr;
                ImGui::SetCurrentContext(previousContext == destroyedContext ? nullptr : previousContext);
                m_initialized = false;
                m_frameOpen = false;
                m_rendered = false;
#if defined(PE_WIN32)
                m_dx12ImGuiSlots.clear();
#endif
            }

            bool ProcessEvent(const SDL_Event &event) override
            {
                if (!m_initialized || !m_context)
                    return false;

                if (!m_frameInfo.inputEnabled)
                    return false;

                ScopedImGuiContext contextScope(m_context);
                SDL_Event mappedEvent = MapEventToSurface(event);
                SDL_Event &mutableEvent = mappedEvent;
                ImGui_ImplSDL2_ProcessEvent(&mutableEvent);

                ImGuiIO &io = ImGui::GetIO();
                if (IsRuntimeUiMouseInputEvent(mappedEvent))
                    return io.WantCaptureMouse;

                if (IsRuntimeUiTextInputEvent(mappedEvent))
                    return io.WantTextInput || io.WantCaptureKeyboard;

                if (IsRuntimeUiKeyboardInputEvent(mappedEvent))
                    return io.WantCaptureKeyboard;

                return false;
            }

            void BeginFrame(const RuntimeUiFrameInfo &frameInfo) override
            {
                if (!m_initialized || m_frameOpen)
                    return;

                ScopedImGuiContext contextScope(m_context);
                m_frameInfo = frameInfo;
                runtime_ui_imgui::ApplyContextSettings(ImGui::GetIO());
                runtime_ui_imgui::ApplyStyle();
                ImGui_ImplSDL2_NewFrame();
                if (m_api == PE_GRAPHICS_API_VULKAN)
                {
                    ImGui_ImplVulkan_NewFrame();
                }
#if defined(PE_WIN32)
                else if (m_api == PE_GRAPHICS_API_DX12)
                {
                    ImGui_ImplDX12_NewFrame();
                }
#endif
                if (frameInfo.width > 0 && frameInfo.height > 0)
                    ImGui::GetIO().DisplaySize =
                        ImVec2(static_cast<float>(frameInfo.width), static_cast<float>(frameInfo.height));
                m_nextScreenPos = ImVec2(runtime_ui_imgui::kViewportPadding, runtime_ui_imgui::kViewportPadding);
                ImGui::NewFrame();
                m_frameOpen = true;
                m_rendered = false;
            }

            bool BeginScreen(const RuntimeUiScreenDesc &screen) override
            {
                if (!m_frameOpen)
                    return false;

                ScopedImGuiContext contextScope(m_context);
                ImGui::SetNextWindowPos(m_nextScreenPos, ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(runtime_ui_imgui::kWindowWidth, 0.0f), ImGuiCond_FirstUseEver);
                const std::string windowId = screen.title + "##" + screen.id;
                return ImGui::Begin(windowId.c_str(), nullptr, runtime_ui_imgui::kPlayerWindowFlags);
            }

            void Text(const char *label, const char *value) override
            {
                if (!m_frameOpen)
                    return;
                ScopedImGuiContext contextScope(m_context);
                ImGui::Text("%s: %s", label ? label : "", value ? value : "");
            }

            void Number(const char *label, double value) override
            {
                if (!m_frameOpen)
                    return;
                ScopedImGuiContext contextScope(m_context);
                ImGui::Text("%s: %.3f", label ? label : "", value);
            }

            bool Checkbox(const char *label, bool &value) override
            {
                if (!m_frameOpen)
                    return false;
                ScopedImGuiContext contextScope(m_context);
                return ImGui::Checkbox(label ? label : "", &value);
            }

            bool Button(const char *label) override
            {
                if (!m_frameOpen)
                    return false;
                ScopedImGuiContext contextScope(m_context);
                return ImGui::Button(label ? label : "");
            }

            void EndScreen() override
            {
                if (m_frameOpen)
                {
                    ScopedImGuiContext contextScope(m_context);
                    const ImVec2 windowSize = ImGui::GetWindowSize();
                    ImGui::End();
                    m_nextScreenPos.y += windowSize.y + runtime_ui_imgui::kScreenGap;
                }
            }

            void EndFrame() override
            {
                if (!m_frameOpen)
                    return;

                ScopedImGuiContext contextScope(m_context);
                ImGui::Render();
                m_frameOpen = false;
                m_rendered = true;
            }

            bool HasDrawData() const override
            {
                if (!m_rendered || !m_context)
                    return false;

                ScopedImGuiContext contextScope(m_context);
                ImDrawData *drawData = ImGui::GetDrawData();
                return drawData && drawData->TotalVtxCount > 0;
            }

            bool WantsMouseCapture() const override
            {
                if (!m_initialized || !m_context)
                    return false;

                if (!m_frameInfo.inputEnabled)
                    return false;

                ScopedImGuiContext contextScope(m_context);
                return ImGui::GetIO().WantCaptureMouse;
            }

            bool WantsKeyboardCapture() const override
            {
                if (!m_initialized || !m_context)
                    return false;

                if (!m_frameInfo.inputEnabled)
                    return false;

                ScopedImGuiContext contextScope(m_context);
                const ImGuiIO &io = ImGui::GetIO();
                return io.WantCaptureKeyboard || io.WantTextInput;
            }

            void Render(const RuntimeUiRenderContext &context) override
            {
                if (!m_initialized || !context.cmd || !context.renderTarget || !HasDrawData())
                    return;

                ScopedImGuiContext contextScope(m_context);

                Attachment attachment{};
                attachment.image = context.renderTarget;
                attachment.loadOp = PE_LOAD_OP_LOAD;
                attachment.storeOp = PE_STORE_OP_STORE;

                context.cmd->BeginPass(1, &attachment, "RuntimeUI", true);
                if (m_api == PE_GRAPHICS_API_VULKAN)
                {
                    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), GetVulkanCommandBuffer(context.cmd));
                }
#if defined(PE_WIN32)
                else if (m_api == PE_GRAPHICS_API_DX12)
                {
                    Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
                    Dx12CommandBufferImpl *dx12Cmd = Dx12CommandBufferImpl::From(context.cmd);
                    ID3D12DescriptorHeap *heap = rhi && rhi->GetCbvSrvUavHeap() ? rhi->GetCbvSrvUavHeap()->Get() : nullptr;
                    PE_ERROR_IF(!heap || !dx12Cmd || !dx12Cmd->Get(),
                                "RuntimeUI: DX12 command state is not initialized");

                    dx12Cmd->Get()->SetDescriptorHeaps(1, &heap);
                    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dx12Cmd->Get());
                    dx12Cmd->InvalidateShaderVisibleHeapBinding();
                }
#endif
                context.cmd->EndPass();
            }

        private:
            bool HasInputRect() const
            {
                return m_frameInfo.inputRectValid &&
                       m_frameInfo.inputRectWidth > 0.0f &&
                       m_frameInfo.inputRectHeight > 0.0f &&
                       m_frameInfo.width > 0 &&
                       m_frameInfo.height > 0;
            }

            SDL_Event MapEventToSurface(const SDL_Event &event) const
            {
                if (!HasInputRect())
                    return event;

                SDL_Event mapped = event;
                const auto mapX = [this](float x)
                {
                    return (x - m_frameInfo.inputRectMinX) *
                           static_cast<float>(m_frameInfo.width) /
                           m_frameInfo.inputRectWidth;
                };
                const auto mapY = [this](float y)
                {
                    return (y - m_frameInfo.inputRectMinY) *
                           static_cast<float>(m_frameInfo.height) /
                           m_frameInfo.inputRectHeight;
                };

                if (mapped.type == SDL_MOUSEMOTION)
                {
                    mapped.motion.x = static_cast<int>(mapX(static_cast<float>(event.motion.x)));
                    mapped.motion.y = static_cast<int>(mapY(static_cast<float>(event.motion.y)));
                }
                else if (mapped.type == SDL_MOUSEBUTTONDOWN || mapped.type == SDL_MOUSEBUTTONUP)
                {
                    mapped.button.x = static_cast<int>(mapX(static_cast<float>(event.button.x)));
                    mapped.button.y = static_cast<int>(mapY(static_cast<float>(event.button.y)));
                }
                // SDL mouse-wheel events have no cursor coordinates; ImGui applies them at its cached mouse position.

                return mapped;
            }

            bool InitVulkan(Image *renderTarget)
            {
                if (!renderTarget)
                    return false;

                ImGui_ImplSDL2_InitForVulkan(RHII.GetWindow());
                m_platformInitialized = true;

                Queue *queue = RHII.GetMainQueue();
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

                Attachment attachment{};
                attachment.image = renderTarget;
                attachment.loadOp = PE_LOAD_OP_LOAD;
                attachment.storeOp = PE_STORE_OP_STORE;
                RenderPass *renderPass = CommandBuffer::GetRenderPass(1, &attachment);
                initInfo.UseDynamicRendering = false;
                initInfo.RenderPass = pe::GetVulkanRenderPass(renderPass);

                ImGui_ImplVulkan_Init(&initInfo);
                m_rendererInitialized = true;
                m_api = PE_GRAPHICS_API_VULKAN;
                return true;
            }

#if defined(PE_WIN32)
            bool InitDx12(Image *renderTarget)
            {
                Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
                if (!rhi || !rhi->GetDevice() || !rhi->GetGraphicsQueue() || !rhi->GetCbvSrvUavHeap() || !renderTarget)
                    return false;

                ImGui_ImplSDL2_InitForD3D(RHII.GetWindow());
                m_platformInitialized = true;

                ImGui_ImplDX12_InitInfo initInfo{};
                initInfo.Device = rhi->GetDevice();
                initInfo.CommandQueue = rhi->GetGraphicsQueue();
                initInfo.NumFramesInFlight = static_cast<int>(RHII.GetSwapchainImageCount());
                initInfo.RTVFormat = pe_dx12::Format(renderTarget->GetFormat());
                initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
                initInfo.SrvDescriptorHeap = rhi->GetCbvSrvUavHeap()->Get();
                initInfo.UserData = this;
                initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *info,
                                                   D3D12_CPU_DESCRIPTOR_HANDLE *outCpu,
                                                   D3D12_GPU_DESCRIPTOR_HANDLE *outGpu)
                {
                    auto *self = static_cast<ImGuiRuntimeUiBackend *>(info ? info->UserData : nullptr);
                    Dx12RhiImpl *dx12 = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
                    PE_ERROR_IF(!self || !dx12 || !dx12->GetCbvSrvUavHeap(),
                                "RuntimeUI DX12 descriptor allocation requires an initialized heap");

                    uint32_t slot = dx12->GetCbvSrvUavHeap()->Allocate();
                    *outCpu = dx12->GetCbvSrvUavHeap()->GetCpuHandle(slot);
                    *outGpu = dx12->GetCbvSrvUavHeap()->GetGpuHandle(slot);
                    self->m_dx12ImGuiSlots.emplace(static_cast<uint64_t>(outGpu->ptr), slot);
                };
                initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *info,
                                                  D3D12_CPU_DESCRIPTOR_HANDLE,
                                                  D3D12_GPU_DESCRIPTOR_HANDLE gpu)
                {
                    auto *self = static_cast<ImGuiRuntimeUiBackend *>(info ? info->UserData : nullptr);
                    Dx12RhiImpl *dx12 = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
                    if (!self || !dx12 || !dx12->GetCbvSrvUavHeap())
                        return;

                    auto it = self->m_dx12ImGuiSlots.find(static_cast<uint64_t>(gpu.ptr));
                    if (it == self->m_dx12ImGuiSlots.end())
                        return;

                    dx12->GetCbvSrvUavHeap()->Free(it->second);
                    self->m_dx12ImGuiSlots.erase(it);
                };

                ImGui_ImplDX12_Init(&initInfo);
                m_rendererInitialized = true;
                m_api = PE_GRAPHICS_API_DX12;
                return true;
            }
#endif

            void CreateFontsTexture()
            {
                if (m_api == PE_GRAPHICS_API_VULKAN)
                    ImGui_ImplVulkan_CreateFontsTexture();
            }

            ImGuiContext *m_context = nullptr;
            RuntimeUiFrameInfo m_frameInfo{};
            ImVec2 m_nextScreenPos = ImVec2(runtime_ui_imgui::kViewportPadding, runtime_ui_imgui::kViewportPadding);
            PeGraphicsApi m_api = PE_GRAPHICS_API_VULKAN;
            bool m_initialized = false;
            bool m_platformInitialized = false;
            bool m_rendererInitialized = false;
            bool m_frameOpen = false;
            bool m_rendered = false;
#if defined(PE_WIN32)
            std::unordered_map<uint64_t, uint32_t> m_dx12ImGuiSlots;
#endif
        };
    } // namespace

    std::unique_ptr<IRuntimeUiBackend> CreateImGuiRuntimeUiBackend()
    {
        return std::make_unique<ImGuiRuntimeUiBackend>();
    }
} // namespace pe
