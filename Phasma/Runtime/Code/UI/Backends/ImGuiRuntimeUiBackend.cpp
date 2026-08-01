#include "UI/Backends/ImGuiRuntimeUiBackend.h"
#include "UI/Backends/ImGuiRuntimeUiStyle.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/RenderPass.h"
#include "API/Surface.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"
#include "API/Vulkan/VulkanDescriptorImpl.h"
#include "API/Vulkan/VulkanImageViewImpl.h"
#include "API/Vulkan/VulkanQueueImpl.h"
#include "API/Vulkan/VulkanRenderPassImpl.h"
#include "API/Vulkan/VulkanSamplerImpl.h"
#include "UI/RuntimeUiInputEvents.h"
#include "Runtime/RuntimeStartup.h"
#include "Script/Bindings/Input/InputState.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12CommandBufferImpl.h"
#include "API/DX12/Dx12DescriptorHeap.h"
#include "API/DX12/Dx12ImageViewImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12Translate.h"
#include "imgui_impl_dx12.h"
#endif
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

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
                    // Bake Greek (+ Latin) into the runtime UI atlas before the backend builds it.
                    {
                        const float fontSize = 16.0f;
                        const ImWchar *ranges = io.Fonts->GetGlyphRangesGreek();
                        ImFontConfig fontConfig;
                        fontConfig.RasterizerDensity = 4.0f;
                        auto addFont = [&](const char *relative) -> ImFont *
                        {
                            FileSystem file(Path::ResolveAsset(relative), std::ios::in | std::ios::binary);
                            if (!file.IsOpen() || file.Size() == 0)
                                return nullptr;
                            const std::vector<uint8_t> bytes = file.ReadAllBytes();
                            void *data = IM_ALLOC(bytes.size()); // atlas takes ownership
                            std::memcpy(data, bytes.data(), bytes.size());
                            return io.Fonts->AddFontFromMemoryTTF(data, static_cast<int>(bytes.size()), fontSize,
                                                                  &fontConfig, ranges);
                        };
                        ImFont *font = addFont("Fonts/RuntimeUi.ttf");
                        if (!font)
                            font = addFont("Fonts/DejaVuSans.ttf");
                        if (!font)
                            font = addFont("Fonts/Inter-Regular.ttf");
                        if (font)
                            io.FontDefault = font;
                    }

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
                ReleaseImageTextures();
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

                if (event.type == SDL_FINGERUP)
                    m_touchFingers.erase(event.tfinger.fingerId);

                if (!m_frameInfo.inputEnabled)
                    return false;

                if (event.type == SDL_FINGERDOWN)
                {
                    const bool secondary = !m_touchFingers.empty();
                    m_touchFingers.insert(event.tfinger.fingerId);
                    if (secondary)
                        m_pendingTouchClicks.push_back(MapFingerToSurface(event.tfinger.x, event.tfinger.y));
                }

                ScopedImGuiContext contextScope(m_context);
                SDL_Event mappedEvent = MapEventToSurface(event);
                SDL_Event &mutableEvent = mappedEvent;
                ImGui_ImplSDL2_ProcessEvent(&mutableEvent);

                ImGuiIO &io = ImGui::GetIO();
                if (IsRuntimeUiMouseInputEvent(mappedEvent))
                    return io.WantCaptureMouse;

                // Keyboard: swallow only while a text field is active. ImGui keeps
                // WantCaptureKeyboard latched while any window holds focus (e.g. after
                // a button click), which would permanently steal WASD from the host.
                if (IsRuntimeUiTextInputEvent(mappedEvent))
                    return io.WantTextInput;

                if (IsRuntimeUiKeyboardInputEvent(mappedEvent))
                    return io.WantTextInput;

                return false;
            }

            void BeginFrame(const RuntimeUiFrameInfo &frameInfo) override
            {
                if (!m_initialized || m_frameOpen)
                    return;

                ScopedImGuiContext contextScope(m_context);
                m_frameInfo = frameInfo;
                const float uiScale = frameInfo.uiScale > 0.0f ? frameInfo.uiScale : 1.0f;
                ImGuiIO &io = ImGui::GetIO();
                runtime_ui_imgui::ApplyContextSettings(io);
                runtime_ui_imgui::ApplyStyle(uiScale);
                io.FontGlobalScale = uiScale;
                ImGui_ImplSDL2_NewFrame();
                SyncMousePosition(io);
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
                    io.DisplaySize =
                        ImVec2(static_cast<float>(frameInfo.width), static_cast<float>(frameInfo.height));
                const float padding = runtime_ui_imgui::kViewportPadding * uiScale;
                m_nextScreenPos = ImVec2(SafeAreaMinX() + padding, SafeAreaMinY() + padding);
                ImGui::NewFrame();
                m_frameOpen = true;
                m_rendered = false;
            }

            bool BeginScreen(const RuntimeUiScreenDesc &screen) override
            {
                if (!m_frameOpen)
                    return false;

                ScopedImGuiContext contextScope(m_context);
                m_currentScreenId = screen.id;
                m_currentScreenOverlay = screen.overlay;
                if (screen.overlay)
                    return true;

                const float uiScale = m_frameInfo.uiScale > 0.0f ? m_frameInfo.uiScale : 1.0f;
                const float padding = runtime_ui_imgui::kViewportPadding * uiScale;
                const float safeWidth = SafeAreaWidth();
                float windowWidth = runtime_ui_imgui::kWindowWidth * uiScale;
                if (safeWidth > padding * 2.0f)
                    windowWidth = std::min(windowWidth, safeWidth - padding * 2.0f);
                if (screen.scrollable || screen.maxHeight > 0.0f)
                {
                    float maxWindowHeight = 0.0f;
                    const float safeHeight = SafeAreaHeight();
                    if (safeHeight > padding * 2.0f)
                    {
                        const float safeMaxY = SafeAreaMinY() + safeHeight - padding;
                        maxWindowHeight = std::max(64.0f * uiScale, safeMaxY - m_nextScreenPos.y);
                    }
                    if (screen.maxHeight > 0.0f)
                    {
                        maxWindowHeight = maxWindowHeight > 0.0f ? std::min(maxWindowHeight, screen.maxHeight)
                                                                 : screen.maxHeight;
                    }
                    if (maxWindowHeight > 0.0f)
                    {
                        ImGui::SetNextWindowSizeConstraints(ImVec2(windowWidth, 0.0f),
                                                            ImVec2(windowWidth, maxWindowHeight));
                    }
                }
                ImGui::SetNextWindowPos(m_nextScreenPos, ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(windowWidth, 0.0f), ImGuiCond_FirstUseEver);
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

            void DrawImage(const RuntimeUiImageDesc &image) override
            {
                if (!m_frameOpen || !image.image)
                    return;

                ScopedImGuiContext contextScope(m_context);
                void *textureID = GetImageTexture(image.image);
                if (!textureID)
                    return;

                const float width = image.width > 0.0f ? image.width : image.image->GetWidth_f();
                const float height = image.height > 0.0f ? image.height : image.image->GetHeight_f();
                if (width <= 0.0f || height <= 0.0f)
                    return;

                if (image.label && image.label[0] != '\0')
                    ImGui::TextUnformatted(image.label);
                ImGui::Image((ImTextureID)textureID, ImVec2(width, height));
            }

            RuntimeUiWidgetState Quad(const RuntimeUiQuadDesc &quad) override
            {
                RuntimeUiWidgetState state{};
                if (!m_frameOpen || !quad.visible)
                    return state;

                ScopedImGuiContext contextScope(m_context);
                const ImVec2 size = quad.fit
                                        ? MeasureFitSize(quad)
                                        : ImVec2(quad.width > 0.0f ? quad.width : 180.0f,
                                                 quad.height > 0.0f ? quad.height : 240.0f);
                if (size.x <= 0.0f || size.y <= 0.0f)
                    return state;

                const std::string id = m_currentScreenId + "." + (quad.id ? quad.id : "quad");
                if (m_currentScreenOverlay)
                {
                    // Decorative overlay quads (damage floats, bars, stick, FPS…) used to each
                    // open an ImGui window + BringWindowToDisplayFront. That dominated ATH combat
                    // CPU once dozens were live. Batch no-input quads onto the background draw
                    // list (still above the 3D scene; below interactive overlay windows).
                    // bring_to_front no-input quads (tooltips, toasts, boss bar) keep the
                    // windowed path — batching would bury them under interactive windows.
                    if (quad.noInput && !quad.bringToFront)
                        return QuadOverlayBatched(quad, size);
                    return QuadOverlay(id, quad, size);
                }

                ImGui::PushID(id.c_str());
                state = QuadInCurrentWindow(id, quad, size);
                ImGui::PopID();
                return state;
            }

            void EndScreen() override
            {
                if (m_frameOpen && !m_currentScreenOverlay)
                {
                    ScopedImGuiContext contextScope(m_context);
                    const float uiScale = m_frameInfo.uiScale > 0.0f ? m_frameInfo.uiScale : 1.0f;
                    const ImVec2 windowSize = ImGui::GetWindowSize();
                    ImGui::End();
                    m_nextScreenPos.y += windowSize.y + runtime_ui_imgui::kScreenGap * uiScale;
                }

                m_currentScreenOverlay = false;
                m_currentScreenId.clear();
            }

            void EndFrame() override
            {
                if (!m_frameOpen)
                    return;

                PE_PROFILE_SCOPE("RuntimeUI ImGui::Render");
                ScopedImGuiContext contextScope(m_context);
                // ImGui reads its own key state, so this debug bind bypasses the input.is_key_*
                // gate; without this it toggles while an ImGui text field is being typed into.
                if (!InputState::IsKeyboardCapturedByUi() && ImGui::IsKeyPressed(ImGuiKey_G, false))
                    m_showFrameGraph = !m_showFrameGraph;
                DrawFrameTimeOverlay();
                ImGui::Render();
                m_pendingTouchClicks.clear();
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
                // Only text entry captures the keyboard; focus alone must not steal
                // movement keys from the host (see ProcessEvent note).
                return io.WantTextInput;
            }

            float MouseWheel() const override
            {
                if (!m_initialized)
                    return 0.0f;
                ScopedImGuiContext contextScope(m_context);
                return ImGui::GetIO().MouseWheel;
            }

            void ResetInputState() override
            {
                m_activeDragWidget.clear();
                m_touchFingers.clear();
                m_pendingTouchClicks.clear();
                if (!m_context)
                    return;

                ScopedImGuiContext contextScope(m_context);
                ImGuiIO &io = ImGui::GetIO();
                io.ClearEventsQueue();
                io.ClearInputMouse();
                io.ClearInputKeys();
                io.WantCaptureMouse = false;
                io.WantCaptureKeyboard = false;
                io.WantTextInput = false;
                ImGui::SetNextFrameWantCaptureMouse(false);
                ImGui::SetNextFrameWantCaptureKeyboard(false);
                ImGui::ClearActiveID();
            }

            void Render(const RuntimeUiRenderContext &context) override
            {
                if (!m_initialized || !context.cmd || !context.renderTarget || !HasDrawData())
                    return;

                PE_PROFILE_SCOPE("RuntimeUI Vulkan Draw");
                ScopedImGuiContext contextScope(m_context);
                ImDrawData *drawData = ImGui::GetDrawData();
                if (!drawData || drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f)
                    return;
                drawData->FramebufferScale = ImVec2(context.renderTarget->GetWidth_f() / drawData->DisplaySize.x,
                                                    context.renderTarget->GetHeight_f() / drawData->DisplaySize.y);

                Attachment attachment{};
                attachment.image = context.renderTarget;
                attachment.loadOp = PE_LOAD_OP_LOAD;
                attachment.storeOp = PE_STORE_OP_STORE;

                context.cmd->BeginPass(1, &attachment, "RuntimeUI", true);
                if (m_api == PE_GRAPHICS_API_VULKAN)
                {
                    ImGui_ImplVulkan_RenderDrawData(drawData, GetVulkanCommandBuffer(context.cmd));
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
                    ImGui_ImplDX12_RenderDrawData(drawData, dx12Cmd->Get());
                    dx12Cmd->InvalidateShaderVisibleHeapBinding();
                }
#endif
                context.cmd->EndPass();
            }

        private:
            void DrawFrameTimeOverlay()
            {
                const float dt = static_cast<float>(FrameTimer::Instance().GetDelta());
                const float ms = dt * 1000.0f;
                if (ms > m_frameGraphPeakMs)
                    m_frameGraphPeakMs = ms;
                m_frameGraphSumMs += ms;
                ++m_frameGraphFrames;
                m_frameGraphAccum += dt;
                if (m_frameGraphAccum >= kFrameGraphRefreshSeconds)
                {
                    m_frameGraphAccum = 0.0f;
                    m_frameGraphDispMs = m_frameGraphFrames > 0
                                             ? m_frameGraphSumMs / static_cast<float>(m_frameGraphFrames)
                                             : m_frameGraphPeakMs;
                    m_frameGraphDispPeakMs = m_frameGraphPeakMs;
                    m_frameMs[m_frameMsHead] = m_frameGraphPeakMs;
                    m_frameMsHead = (m_frameMsHead + 1) % kFrameMsHistory;
                    if (m_frameMsCount < kFrameMsHistory)
                        ++m_frameMsCount;
                    m_frameGraphPeakMs = 0.0f;
                    m_frameGraphSumMs = 0.0f;
                    m_frameGraphFrames = 0;
                }

                if (!m_showFrameGraph)
                    return;

                float maxMs = 0.0f;
                for (int i = 0; i < m_frameMsCount; ++i)
                {
                    if (m_frameMs[i] > maxMs)
                        maxMs = m_frameMs[i];
                }
                const float dispMs = m_frameGraphDispMs;
                const float fps = dispMs > 0.0001f ? 1000.0f / dispMs : 0.0f;
                const float plotMax = maxMs * 1.25f > 2.0f ? maxMs * 1.25f : 2.0f;

                const ImGuiViewport *vp = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 10.0f, vp->WorkPos.y + 10.0f),
                                        ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowBgAlpha(0.55f);
                const ImGuiWindowFlags flags = ImGuiWindowFlags_NoNav |
                                               ImGuiWindowFlags_NoFocusOnAppearing |
                                               ImGuiWindowFlags_AlwaysAutoResize;
                if (ImGui::Begin("Frame Time (G)", nullptr, flags))
                {
                    ImGui::Text("avg %.2f ms   %.0f FPS", dispMs, fps);
                    ImGui::Text("max %.2f ms (spikes)", maxMs);
                    char overlay[48];
                    std::snprintf(overlay, sizeof(overlay), "%.1f ms peak", m_frameGraphDispPeakMs);
                    ImGui::PlotLines("##frame_ms", m_frameMs, kFrameMsHistory, m_frameMsHead,
                                     overlay, 0.0f, plotMax, ImVec2(240.0f, 60.0f));
                    DrawPresentModeSelector();
                }
                ImGui::End();
            }

            void DrawPresentModeSelector()
            {
                Surface *surface = RHII.GetSurface();
                if (!surface)
                    return;

                ImGui::Separator();
                const PePresentMode currentMode = surface->GetPresentMode();
                if (!ImGui::BeginCombo("Present Mode", RHII.PresentModeToString(currentMode)))
                    return;

                for (const PePresentMode mode : surface->GetSupportedPresentModes())
                {
                    const bool isSelected = (mode == currentMode);
                    if (ImGui::Selectable(RHII.PresentModeToString(mode), isSelected) && mode != currentMode)
                    {
                        Settings::Get<SceneSettings>().preferred_present_mode = mode;
                        std::string writeError;
                        if (!WriteEditorPresentMode({}, mode, &writeError))
                            PE_WARN("[RuntimeUI] Could not persist present mode: %s", writeError.c_str());
                        EventSystem::PushEvent(EventType::PresentMode);
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            void *GetImageTexture(Image *image)
            {
                auto it = m_imageTextureIds.find(image);
                if (it != m_imageTextureIds.end())
                    return it->second;

                void *textureID = RegisterImageTexture(image);
                if (textureID)
                    m_imageTextureIds.emplace(image, textureID);
                return textureID;
            }

            static ImU32 ToColor(const RuntimeUiColor &color)
            {
                return ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
            }

            static RuntimeUiColor LerpColor(const RuntimeUiColor &a, const RuntimeUiColor &b, float t)
            {
                t = std::clamp(t, 0.0f, 1.0f);
                return RuntimeUiColor{
                    a.r + (b.r - a.r) * t,
                    a.g + (b.g - a.g) * t,
                    a.b + (b.b - a.b) * t,
                    a.a + (b.a - a.a) * t};
            }

            static bool HasText(const char *text)
            {
                return text && text[0] != '\0';
            }

            static bool HasLineBreak(const char *text)
            {
                return HasText(text) && std::strchr(text, '\n') != nullptr;
            }

            static bool HasMultilineText(const RuntimeUiQuadDesc &quad)
            {
                return HasLineBreak(quad.label) || HasLineBreak(quad.title) ||
                       HasLineBreak(quad.subtitle) || HasLineBreak(quad.body) ||
                       HasLineBreak(quad.footer);
            }

            static constexpr float kLineGap = 3.0f;

            // RuntimeUi cancels DPI into quad.fontScale (authored / uiScale) so glyph
            // size stays resolution-stable. Pad must undo that cancel: otherwise phones
            // get a smaller inset, a wider wrap box, and different line breaks than PC.
            static float TextPadPx(float cancelledFontScale)
            {
                const float scale = cancelledFontScale > 0.0f ? cancelledFontScale : 1.0f;
                const float uiScale = std::max(1.0f, ImGui::GetIO().FontGlobalScale);
                return 10.0f * scale * uiScale;
            }

            static float LineWidth(ImFont *font, float fontSize, const std::string &s)
            {
                return font ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, s.c_str()).x
                            : ImGui::CalcTextSize(s.c_str()).x;
            }

            static RuntimeUiTextAlignH ResolveH(RuntimeUiTextAlignH a, RuntimeUiTextAlignH def)
            {
                return a == RuntimeUiTextAlignH::Default ? def : a;
            }

            static RuntimeUiTextAlignV ResolveV(RuntimeUiTextAlignV a, RuntimeUiTextAlignV def)
            {
                return a == RuntimeUiTextAlignV::Default ? def : a;
            }

            // Word-wrap `text` to `maxWidth`, honoring explicit '\n', up to `maxLines`.
            static std::vector<std::string> WrapTextLines(ImFont *font, float fontSize, const char *text,
                                                          float maxWidth, int maxLines)
            {
                std::vector<std::string> out;
                if (!HasText(text) || maxWidth <= 0.0f || maxLines <= 0)
                    return out;

                std::string line;
                std::string word;

                auto flush = [&]()
                {
                    if (line.empty() || static_cast<int>(out.size()) >= maxLines)
                        return;
                    out.push_back(line);
                    line.clear();
                };

                auto pushWord = [&](const std::string &next)
                {
                    if (next.empty() || static_cast<int>(out.size()) >= maxLines)
                        return;
                    const std::string candidate = line.empty() ? next : line + " " + next;
                    if (LineWidth(font, fontSize, candidate) <= maxWidth)
                    {
                        line = candidate;
                        return;
                    }
                    flush();
                    if (static_cast<int>(out.size()) < maxLines)
                        line = next;
                };

                const char *cursor = text;
                while (*cursor && static_cast<int>(out.size()) < maxLines)
                {
                    const char c = *cursor++;
                    if (c == '\n')
                    {
                        pushWord(word);
                        word.clear();
                        flush();
                    }
                    else if (std::isspace(static_cast<unsigned char>(c)))
                    {
                        pushWord(word);
                        word.clear();
                    }
                    else
                    {
                        word.push_back(c);
                    }
                }
                pushWord(word);
                flush();
                return out;
            }

            // Draw pre-wrapped lines starting at startY, each H-aligned within
            // [originX, originX+wrapWidth], plus a pixel offX nudge.
            static void DrawTextLines(ImDrawList *drawList, ImFont *font, float fontSize,
                                      const std::vector<std::string> &lines, float originX, float wrapWidth,
                                      float startY, ImU32 color, RuntimeUiTextAlignH alignH, float offX)
            {
                const float lineHeight = fontSize + kLineGap;
                float y = startY;
                for (const std::string &ln : lines)
                {
                    float x = originX;
                    if (alignH == RuntimeUiTextAlignH::Center || alignH == RuntimeUiTextAlignH::Right)
                    {
                        const float lw = LineWidth(font, fontSize, ln);
                        x += (alignH == RuntimeUiTextAlignH::Center) ? (wrapWidth - lw) * 0.5f : (wrapWidth - lw);
                    }
                    drawList->AddText(font, fontSize, ImVec2(x + offX, y), color, ln.c_str());
                    y += lineHeight;
                }
            }

            // Flow-style wrapped text from a fixed top (pos.y); H-aligned within maxWidth.
            // Returns the drawn block height. (Vertical placement is the caller's via pos.y.)
            static float DrawWrappedText(ImDrawList *drawList, ImFont *font, float fontSize, const char *text,
                                         ImVec2 pos, float maxWidth, ImU32 color, int maxLines,
                                         RuntimeUiTextAlignH alignH = RuntimeUiTextAlignH::Left, float offX = 0.0f)
            {
                if (!drawList || !HasText(text) || maxWidth <= 0.0f || maxLines <= 0)
                    return 0.0f;
                const std::vector<std::string> lines = WrapTextLines(font, fontSize, text, maxWidth, maxLines);
                DrawTextLines(drawList, font, fontSize, lines, pos.x, maxWidth, pos.y, color, alignH, offX);
                return static_cast<float>(lines.size()) * (fontSize + kLineGap);
            }

            struct TextBlockMetrics
            {
                float width = 0.0f;
                float height = 0.0f;
            };

            static TextBlockMetrics MeasureExplicitText(ImFont *font, float fontSize, const char *text)
            {
                TextBlockMetrics result{};
                if (!HasText(text))
                    return result;

                const char *lineStart = text;
                for (const char *cursor = text;; ++cursor)
                {
                    if (*cursor != '\n' && *cursor != '\0')
                        continue;
                    result.width = std::max(result.width,
                                            LineWidth(font, fontSize, std::string(lineStart, cursor)));
                    result.height += fontSize;
                    if (*cursor == '\0')
                        break;
                    lineStart = cursor + 1;
                }
                return result;
            }

            static TextBlockMetrics MeasureTextBlock(ImFont *font, float fontSize, float gapScale,
                                                     const RuntimeUiQuadDesc &quad, float maxWidth,
                                                     float bodyScale, int maxBodyLines)
            {
                TextBlockMetrics result{};
                auto addLine = [&](const char *value, float size, float gap)
                {
                    if (!HasText(value))
                        return;
                    const TextBlockMetrics line = MeasureExplicitText(font, size, value);
                    result.width = std::max(result.width, line.width);
                    result.height += line.height + gap * gapScale;
                };

                addLine(quad.label, fontSize * 0.83f, 2.0f);
                addLine(quad.title, fontSize * 1.08f, 6.0f);
                addLine(quad.subtitle, fontSize * 0.86f, 5.0f);
                if (HasText(quad.body))
                {
                    const float bodyFontSize = fontSize * bodyScale;
                    const std::vector<std::string> lines =
                        WrapTextLines(font, bodyFontSize, quad.body, maxWidth, maxBodyLines);
                    for (const std::string &line : lines)
                        result.width = std::max(result.width, LineWidth(font, bodyFontSize, line));
                    result.height += static_cast<float>(lines.size()) * (bodyFontSize + kLineGap);
                }
                addLine(quad.footer, fontSize * 0.82f, 0.0f);
                return result;
            }

            // Wrapped text aligned in BOTH axes within the box [boxMin, boxMin+boxSize]
            // (inset by pad), plus a pixel (offX,offY) nudge. Returns block height.
            static float DrawAlignedText(ImDrawList *drawList, ImFont *font, float fontSize, const char *text,
                                         ImVec2 boxMin, ImVec2 boxSize, float pad, ImU32 color,
                                         RuntimeUiTextAlignH alignH, RuntimeUiTextAlignV alignV,
                                         float offX, float offY, int maxLines)
            {
                if (!drawList || !HasText(text))
                    return 0.0f;
                const float wrapWidth = std::max(1.0f, boxSize.x - pad * 2.0f);
                const std::vector<std::string> lines = WrapTextLines(font, fontSize, text, wrapWidth, maxLines);
                if (lines.empty())
                    return 0.0f;
                const float lineHeight = fontSize + kLineGap;
                const float totalH = static_cast<float>(lines.size()) * lineHeight;
                float startY = boxMin.y + pad; // Top / Default
                if (alignV == RuntimeUiTextAlignV::Middle)
                    startY = boxMin.y + (boxSize.y - totalH) * 0.5f;
                else if (alignV == RuntimeUiTextAlignV::Bottom)
                    startY = boxMin.y + boxSize.y - pad - totalH;
                DrawTextLines(drawList, font, fontSize, lines, boxMin.x + pad, wrapWidth, startY + offY, color, alignH, offX);
                return totalH;
            }

            // Aligned X for a single (non-wrapped) line within [boxLeft, boxLeft+boxWidth].
            static float AlignLineX(ImFont *font, float fontSize, const char *s, float boxLeft, float boxWidth,
                                    float pad, RuntimeUiTextAlignH alignH, float offX)
            {
                float x = boxLeft + pad;
                if (alignH == RuntimeUiTextAlignH::Center || alignH == RuntimeUiTextAlignH::Right)
                {
                    const float wrapW = std::max(1.0f, boxWidth - pad * 2.0f);
                    const float lw = font ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, s).x : ImGui::CalcTextSize(s).x;
                    x += (alignH == RuntimeUiTextAlignH::Center) ? (wrapW - lw) * 0.5f : (wrapW - lw);
                }
                return x + offX;
            }

            // Tightly bound a Text-style quad to its content: max line width + pad
            // for X, line count * line height + pad for Y. Mirrors the metrics used
            // by DrawAlignedText so the box snaps exactly around the drawn text.
            static ImVec2 MeasureFitSize(const RuntimeUiQuadDesc &quad)
            {
                const char *content = HasText(quad.body) ? quad.body
                                                         : (HasText(quad.title) ? quad.title : quad.label);
                // Match DrawQuadVisual: pad is DPI-stable; glyph size uses both scales.
                const float fontScale = quad.fontScale > 0.0f ? quad.fontScale : 1.0f;
                const float textScale = fontScale * (quad.textScale > 0.0f ? quad.textScale : 1.0f);
                const float pad = TextPadPx(fontScale);
                ImFont *font = ImGui::GetFont();
                const float fontSize = ImGui::GetFontSize() * textScale;
                const float lineHeight = fontSize + kLineGap;

                float maxWidth = 0.0f;
                int lineCount = 0;
                if (HasText(content))
                {
                    const char *lineStart = content;
                    for (const char *c = content;; ++c)
                    {
                        if (*c == '\n' || *c == '\0')
                        {
                            maxWidth = std::max(maxWidth, LineWidth(font, fontSize, std::string(lineStart, c)));
                            ++lineCount;
                            if (*c == '\0')
                                break;
                            lineStart = c + 1;
                        }
                    }
                }
                if (lineCount <= 0)
                    lineCount = 1;

                return ImVec2(maxWidth + pad * 2.0f, static_cast<float>(lineCount) * lineHeight + pad * 2.0f);
            }

            RuntimeUiWidgetState QuadOverlay(const std::string &id, const RuntimeUiQuadDesc &quad, ImVec2 size)
            {
                ImGuiWindowFlags flags =
                    ImGuiWindowFlags_NoDecoration |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoFocusOnAppearing |
                    ImGuiWindowFlags_NoBringToFrontOnFocus |
                    ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse |
                    ImGuiWindowFlags_NoBackground;
                // A non-interactive quad (e.g. the full-screen menu background) must
                // never capture a click or become the active/raised window —
                // otherwise clicking empty space raises that opaque backdrop over the
                // whole UI ("black screen" on any empty-space click). NoInputs makes
                // the quad transparent to the mouse so clicks fall through it.
                if (quad.noInput)
                    flags |= ImGuiWindowFlags_NoInputs;

                ImGui::SetNextWindowPos(ImVec2(quad.x, quad.y), ImGuiCond_Always);
                ImGui::SetNextWindowSize(size, ImGuiCond_Always);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
                RuntimeUiWidgetState state{};
                if (ImGui::Begin(("##runtime-ui-quad-" + id).c_str(), nullptr, flags))
                {
                    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
                    ImGui::PushID(id.c_str());
                    state = QuadInCurrentWindow(id, quad, size);
                    ImGui::PopID();
                }
                ImGui::End();
                ImGui::PopStyleVar(3);
                return state;
            }

            // no_input overlay: draw only (no window / no hit target).
            RuntimeUiWidgetState QuadOverlayBatched(const RuntimeUiQuadDesc &quad, ImVec2 size)
            {
                RuntimeUiWidgetState state{};
                const ImVec2 pos(quad.x, quad.y);
                const ImVec2 max(pos.x + size.x, pos.y + size.y);
                DrawQuadVisual(quad, pos, max, size, state, ImGui::GetBackgroundDrawList());
                return state;
            }

            RuntimeUiWidgetState QuadInCurrentWindow(const std::string &id, const RuntimeUiQuadDesc &quad, ImVec2 size)
            {
                RuntimeUiWidgetState state{};
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const ImVec2 max(pos.x + size.x, pos.y + size.y);
                ImGui::InvisibleButton("hit", size);

                ImGuiIO &io = ImGui::GetIO();
                const bool itemActive = ImGui::IsItemActive();
                const bool itemClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                const bool mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                const bool mouseDragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f);
                if (quad.draggable && itemClicked)
                    m_activeDragWidget = id;

                state.hovered = ImGui::IsItemHovered();
                state.active = itemActive;
                state.clicked = itemClicked;
                state.rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
                state.down = itemActive && ImGui::IsMouseDown(ImGuiMouseButton_Left);
                state.dragStarted = quad.draggable && itemClicked;
                state.dragging = quad.draggable && m_activeDragWidget == id && !mouseReleased &&
                                 (mouseDragging || state.down);
                state.dragReleased = quad.draggable && m_activeDragWidget == id && mouseReleased;
                state.mouseX = io.MousePos.x;
                state.mouseY = io.MousePos.y;
                const ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
                state.dragDeltaX = dragDelta.x;
                state.dragDeltaY = dragDelta.y;
                if (state.dragReleased)
                    m_activeDragWidget.clear();

                if (!quad.noInput)
                {
                    for (auto it = m_pendingTouchClicks.begin(); it != m_pendingTouchClicks.end();)
                    {
                        if (it->x >= pos.x && it->x <= max.x && it->y >= pos.y && it->y <= max.y)
                        {
                            state.clicked = true;
                            state.mouseX = it->x;
                            state.mouseY = it->y;
                            it = m_pendingTouchClicks.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                    }
                }

                ImDrawList *drawList = ImGui::GetWindowDrawList();
                drawList->PushClipRectFullScreen();
                DrawQuadVisual(quad, pos, max, size, state, drawList);
                drawList->PopClipRect();
                return state;
            }

            void DrawQuadVisual(const RuntimeUiQuadDesc &quad,
                                ImVec2 pos,
                                ImVec2 max,
                                ImVec2 size,
                                const RuntimeUiWidgetState &state,
                                ImDrawList *drawList)
            {
                if (!drawList)
                    drawList = ImGui::GetWindowDrawList();
                const float rounding = 7.0f;
                ImU32 fill = ToColor(quad.fillColor);
                ImU32 border = ToColor(quad.borderColor);
                ImU32 accent = ToColor(quad.accentColor);
                ImU32 text = ToColor(quad.textColor);
                if (quad.selected || state.dragging)
                    border = accent;

                const float scale = quad.fontScale > 0.0f ? quad.fontScale : 1.0f;
                const float pad = TextPadPx(scale);
                ImFont *font = ImGui::GetFont();
                const float textScale = scale * (quad.textScale > 0.0f ? quad.textScale : 1.0f);
                const float fontSize = ImGui::GetFontSize() * textScale;
                const float textInsetRight = std::clamp(quad.textInsetRight, 0.0f, std::max(0.0f, size.x - pad));
                const float textBoxWidth = std::max(1.0f, size.x - textInsetRight);
                const float textWidth = std::max(1.0f, textBoxWidth - pad * 2.0f);
                const bool themed = quad.backgroundImage != nullptr;
                // Theme plates must stay opaque. The 0.90 default tint lets the
                // underlay fill muddy button_surface — idle looks wrong until hover
                // (or focus loss) changes the composite.
                auto plateImageTint = [&]() -> RuntimeUiColor
                {
                    RuntimeUiColor tint = quad.backgroundImageTint;
                    if (themed && tint.a < 0.999f)
                        tint.a = 1.0f;
                    return tint;
                };
                auto drawSurface = [&](ImU32 surfaceFill, bool accentWash)
                {
                    drawList->AddRectFilled(pos, max, surfaceFill, rounding);
                    if (quad.backgroundImage)
                    {
                        if (void *textureID = GetImageTexture(quad.backgroundImage))
                        {
                            drawList->AddImageRounded((ImTextureID)textureID, pos, max, ImVec2(0.0f, 0.0f),
                                                      ImVec2(1.0f, 1.0f), ToColor(plateImageTint()), rounding);
                        }
                    }
                    if (accentWash && themed && (quad.selected || state.dragging || state.hovered))
                    {
                        RuntimeUiColor glow = quad.accentColor;
                        glow.a *= quad.selected || state.dragging ? 0.22f : 0.08f;
                        drawList->AddRectFilled(pos, max, ToColor(glow), rounding);
                    }
                };
                auto drawBorder = [&]()
                {
                    if (!themed && (quad.borderColor.a > 0.0f || quad.selected || state.dragging))
                        drawList->AddRect(pos, max, border, rounding, 0,
                                          quad.selected || state.dragging ? 3.0f : 1.5f);
                };

                if (quad.visualStyle == RuntimeUiQuadVisualStyle::Image)
                {
                    if (quad.fillColor.a > 0.0f)
                        drawSurface(fill, true);
                    if (quad.image)
                    {
                        if (void *textureID = GetImageTexture(quad.image))
                        {
                            // Transparent-fill HUD images (bars, ink icons) must stay
                            // sharp — the themed 7px plate rounding turns thin fills
                            // into pills. Plated image cards keep the rounded path.
                            if (quad.fillColor.a <= 0.0f && !themed)
                            {
                                drawList->AddImage((ImTextureID)textureID, pos, max, ImVec2(0.0f, 0.0f),
                                                   ImVec2(1.0f, 1.0f), ToColor(quad.imageTint));
                            }
                            else
                            {
                                drawList->AddImageRounded((ImTextureID)textureID, pos, max, ImVec2(0.0f, 0.0f),
                                                          ImVec2(1.0f, 1.0f), ToColor(quad.imageTint), rounding);
                            }
                        }
                    }
                    else if (quad.accentColor.a > 0.0f)
                    {
                        drawList->AddRectFilled(ImVec2(pos.x + pad, pos.y + pad),
                                                ImVec2(max.x - pad, max.y - pad),
                                                accent,
                                                std::max(0.0f, rounding - 2.0f));
                    }
                    drawBorder();
                    return;
                }

                if (quad.visualStyle == RuntimeUiQuadVisualStyle::Button)
                {
                    // Same idle/hover fill lerp as unthemed — this is the calculation
                    // that makes the plate read correctly (hover was the only path that
                    // still applied it after the themed-tint experiment).
                    RuntimeUiColor buttonFill = state.down ? quad.accentColor
                                                           : (state.hovered ? LerpColor(quad.fillColor, quad.accentColor, 0.35f)
                                                                            : quad.fillColor);
                    drawSurface(ToColor(buttonFill), false);
                    // Themed buttons have no border to thicken (drawBorder is unthemed
                    // only), so selected/dragging reads as an accent wash instead.
                    const bool activeWash = themed && (quad.selected || state.dragging);
                    if (state.hovered || activeWash)
                    {
                        RuntimeUiColor wash = quad.accentColor;
                        // Themed: restrained wash so wood/metal grain stays visible.
                        wash.a *= state.down || activeWash ? (themed ? 0.18f : 0.34f) : (themed ? 0.08f : 0.16f);
                        drawList->AddRectFilled(pos, max, ToColor(wash), rounding);
                    }
                    drawBorder();

                    const char *caption = HasText(quad.title) ? quad.title : (HasText(quad.label) ? quad.label : quad.body);
                    if (!HasText(caption))
                        caption = "Button";

                    const ImVec2 captionSize = font ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, caption)
                                                    : ImGui::CalcTextSize(caption);
                    const RuntimeUiTextAlignH bah = ResolveH(quad.textAlignH, RuntimeUiTextAlignH::Center);
                    const RuntimeUiTextAlignV bav = ResolveV(quad.textAlignV, RuntimeUiTextAlignV::Middle);
                    float btx = pos.x + (textBoxWidth - captionSize.x) * 0.5f;
                    if (bah == RuntimeUiTextAlignH::Left)
                        btx = pos.x + pad;
                    else if (bah == RuntimeUiTextAlignH::Right)
                        btx = pos.x + textBoxWidth - pad - captionSize.x;
                    float bty = pos.y + (size.y - captionSize.y) * 0.5f;
                    if (bav == RuntimeUiTextAlignV::Top)
                        bty = pos.y + pad;
                    else if (bav == RuntimeUiTextAlignV::Bottom)
                        bty = max.y - pad - captionSize.y;
                    ImVec2 textPos(std::max(pos.x + pad, btx) + quad.textOffsetX, bty + quad.textOffsetY);
                    drawList->AddText(font, fontSize, textPos, text, caption);
                    return;
                }

                if (quad.visualStyle == RuntimeUiQuadVisualStyle::Text)
                {
                    if (quad.fillColor.a > 0.0f)
                        drawSurface(fill, true);
                    drawBorder();

                    const char *content = HasText(quad.body) ? quad.body : (HasText(quad.title) ? quad.title : quad.label);
                    const bool multiline = HasLineBreak(content);
                    // 48: skill/inventory tips wrap to many short lines; 12 clipped them.
                    DrawAlignedText(drawList, font, fontSize, content, pos,
                                    ImVec2(textBoxWidth, size.y), pad, text,
                                    ResolveH(quad.textAlignH, multiline ? RuntimeUiTextAlignH::Left
                                                                        : RuntimeUiTextAlignH::Center),
                                    ResolveV(quad.textAlignV, multiline ? RuntimeUiTextAlignV::Top
                                                                        : RuntimeUiTextAlignV::Middle),
                                    quad.textOffsetX, quad.textOffsetY, 48);
                    return;
                }

                if (quad.visualStyle == RuntimeUiQuadVisualStyle::Panel)
                {
                    drawSurface(fill, true);
                    drawBorder();
                    const bool multiline = HasMultilineText(quad);
                    const RuntimeUiTextAlignH ah = ResolveH(
                        quad.textAlignH, multiline ? RuntimeUiTextAlignH::Left : RuntimeUiTextAlignH::Center);
                    const RuntimeUiTextAlignV av = ResolveV(
                        quad.textAlignV, multiline ? RuntimeUiTextAlignV::Top : RuntimeUiTextAlignV::Middle);
                    const float ox = quad.textOffsetX;
                    const float layoutFontSize = fontSize;
                    const float layoutScale = textScale;
                    const TextBlockMetrics block =
                        MeasureTextBlock(font, layoutFontSize, layoutScale, quad, textWidth, 1.0f, 14);
                    const float totalHeight = block.height;

                    float y = pos.y + pad;
                    if (av == RuntimeUiTextAlignV::Middle)
                        y = std::max(y, pos.y + (size.y - totalHeight) * 0.5f);
                    else if (av == RuntimeUiTextAlignV::Bottom)
                        y = std::max(y, max.y - pad - totalHeight);
                    y += quad.textOffsetY;
                    if (HasText(quad.label))
                    {
                        drawList->AddText(font, layoutFontSize * 0.83f,
                                          ImVec2(AlignLineX(font, layoutFontSize * 0.83f, quad.label,
                                                            pos.x, textBoxWidth, pad, ah, ox),
                                                 y),
                                          accent, quad.label);
                        y += layoutFontSize * 0.83f + 2.0f * layoutScale;
                    }
                    if (HasText(quad.title))
                    {
                        drawList->AddText(font, layoutFontSize * 1.08f,
                                          ImVec2(AlignLineX(font, layoutFontSize * 1.08f, quad.title,
                                                            pos.x, textBoxWidth, pad, ah, ox),
                                                 y),
                                          text, quad.title);
                        y += layoutFontSize * 1.08f + 6.0f * layoutScale;
                    }
                    if (HasText(quad.subtitle))
                    {
                        drawList->AddText(font, layoutFontSize * 0.86f,
                                          ImVec2(AlignLineX(font, layoutFontSize * 0.86f, quad.subtitle,
                                                            pos.x, textBoxWidth, pad, ah, ox),
                                                 y),
                                          accent, quad.subtitle);
                        y += layoutFontSize * 0.86f + 5.0f * layoutScale;
                    }
                    if (HasText(quad.body))
                        y += DrawWrappedText(drawList, font, layoutFontSize, quad.body, ImVec2(pos.x + pad, y),
                                             textWidth, text, 14, ah, ox);
                    if (HasText(quad.footer))
                    {
                        if (!themed)
                            drawList->AddLine(ImVec2(pos.x + pad, y - 2.0f * layoutScale),
                                              ImVec2(pos.x + textBoxWidth - pad, y - 2.0f * layoutScale), border, 1.0f);
                        drawList->AddText(font, layoutFontSize * 0.82f,
                                          ImVec2(AlignLineX(font, layoutFontSize * 0.82f, quad.footer,
                                                            pos.x, textBoxWidth, pad, ah, ox),
                                                 y),
                                          text, quad.footer);
                    }
                    return;
                }

                drawSurface(fill, true);

                const bool textless = !HasText(quad.label) && !HasText(quad.title) && !HasText(quad.subtitle) &&
                                      !HasText(quad.body) && !HasText(quad.footer);
                if (textless)
                {
                    if (quad.image)
                    {
                        if (void *textureID = GetImageTexture(quad.image))
                        {
                            drawList->AddImageRounded((ImTextureID)textureID, pos, max, ImVec2(0.0f, 0.0f),
                                                      ImVec2(1.0f, 1.0f), ToColor(quad.imageTint), rounding);
                        }
                    }
                    drawBorder();
                    return;
                }

                drawBorder();

                float textMinY = pos.y + pad;
                if (quad.image)
                {
                    const float artHeight = std::clamp(size.y * 0.34f, 42.0f * scale, 82.0f * scale);
                    const ImVec2 artMin(pos.x + pad, pos.y + pad);
                    const ImVec2 artMax(max.x - pad, pos.y + pad + artHeight);
                    if (!themed)
                        drawList->AddRectFilled(artMin, artMax, accent, 5.0f);
                    if (void *textureID = GetImageTexture(quad.image))
                    {
                        drawList->AddImage((ImTextureID)textureID,
                                           artMin,
                                           artMax,
                                           ImVec2(0.0f, 0.0f),
                                           ImVec2(1.0f, 1.0f),
                                           ToColor(quad.imageTint));
                    }
                    textMinY = artMax.y + 7.0f * scale;
                }

                const bool multiline = HasMultilineText(quad);
                const RuntimeUiTextAlignH ah = ResolveH(
                    quad.textAlignH, multiline ? RuntimeUiTextAlignH::Left : RuntimeUiTextAlignH::Center);
                const RuntimeUiTextAlignV av = ResolveV(
                    quad.textAlignV, multiline ? RuntimeUiTextAlignV::Top : RuntimeUiTextAlignV::Middle);
                const float ox = quad.textOffsetX;
                const float textAreaHeight = std::max(0.0f, max.y - pad - textMinY);
                const float layoutFontSize = fontSize;
                const float layoutScale = textScale;
                const float bodyFontSize = layoutFontSize * 0.88f;
                const TextBlockMetrics block =
                    MeasureTextBlock(font, layoutFontSize, layoutScale, quad, textWidth, 0.88f, 14);
                const float totalHeight = block.height;
                float y = textMinY;
                if (av == RuntimeUiTextAlignV::Middle)
                    y = std::max(y, textMinY + (textAreaHeight - totalHeight) * 0.5f);
                else if (av == RuntimeUiTextAlignV::Bottom)
                    y = std::max(y, max.y - pad - totalHeight);
                y += quad.textOffsetY;

                if (HasText(quad.label))
                {
                    drawList->AddText(font, layoutFontSize * 0.83f,
                                      ImVec2(AlignLineX(font, layoutFontSize * 0.83f, quad.label,
                                                        pos.x, textBoxWidth, pad, ah, ox),
                                             y),
                                      text, quad.label);
                    y += layoutFontSize * 0.83f + 2.0f * layoutScale;
                }
                if (HasText(quad.title))
                {
                    drawList->AddText(font, layoutFontSize * 1.08f,
                                      ImVec2(AlignLineX(font, layoutFontSize * 1.08f, quad.title,
                                                        pos.x, textBoxWidth, pad, ah, ox),
                                             y),
                                      text, quad.title);
                    y += layoutFontSize * 1.08f + 6.0f * layoutScale;
                }
                if (HasText(quad.subtitle))
                {
                    drawList->AddText(font, layoutFontSize * 0.86f,
                                      ImVec2(AlignLineX(font, layoutFontSize * 0.86f, quad.subtitle,
                                                        pos.x, textBoxWidth, pad, ah, ox),
                                             y),
                                      accent, quad.subtitle);
                    y += layoutFontSize * 0.86f + 5.0f * layoutScale;
                }
                if (HasText(quad.body))
                    y += DrawWrappedText(drawList, font, bodyFontSize, quad.body,
                                         ImVec2(pos.x + pad, y), textWidth, text, 14, ah, ox);
                if (HasText(quad.footer))
                {
                    if (!themed)
                        drawList->AddLine(ImVec2(pos.x + pad, y - 2.0f * layoutScale),
                                          ImVec2(pos.x + textBoxWidth - pad, y - 2.0f * layoutScale), border, 1.0f);
                    drawList->AddText(font, layoutFontSize * 0.82f,
                                      ImVec2(AlignLineX(font, layoutFontSize * 0.82f, quad.footer,
                                                        pos.x, textBoxWidth, pad, ah, ox),
                                             y),
                                      text, quad.footer);
                }
            }

            void *RegisterImageTexture(Image *image)
            {
                if (!image || !image->GetSampler() || !image->GetSRV())
                    return nullptr;

                if (m_api == PE_GRAPHICS_API_VULKAN)
                {
                    VkSampler sampler = pe::GetVulkanSampler(image->GetSampler());
                    VkImageView view = pe::GetVulkanImageView(image->GetSRV());
                    if (!sampler || !view)
                        return nullptr;

                    return (void *)ImGui_ImplVulkan_AddTexture(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }

#if defined(PE_WIN32)
                if (m_api == PE_GRAPHICS_API_DX12)
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
                    m_dx12ImGuiSlots[static_cast<uint64_t>(gpu.ptr)] = slot;
                    return reinterpret_cast<void *>(gpu.ptr);
                }
#endif

                return nullptr;
            }

            void ReleaseImageTexture(void *&textureID)
            {
                if (!textureID)
                    return;

                if (m_api == PE_GRAPHICS_API_VULKAN)
                {
                    ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)textureID);
                }
#if defined(PE_WIN32)
                else if (m_api == PE_GRAPHICS_API_DX12)
                {
                    const uint64_t key = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(textureID));
                    auto it = m_dx12ImGuiSlots.find(key);
                    if (it != m_dx12ImGuiSlots.end())
                    {
                        Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
                        if (rhi && rhi->GetCbvSrvUavHeap())
                            rhi->GetCbvSrvUavHeap()->Free(it->second);
                        m_dx12ImGuiSlots.erase(it);
                    }
                }
#endif

                textureID = nullptr;
            }

            void ReleaseImageTextures()
            {
                for (auto &entry : m_imageTextureIds)
                    ReleaseImageTexture(entry.second);
                m_imageTextureIds.clear();
            }

            bool HasInputRect() const
            {
                return m_frameInfo.inputRectValid &&
                       m_frameInfo.inputRectWidth > 0.0f &&
                       m_frameInfo.inputRectHeight > 0.0f &&
                       m_frameInfo.width > 0 &&
                       m_frameInfo.height > 0;
            }

            bool HasSafeArea() const
            {
                return m_frameInfo.safeAreaValid &&
                       m_frameInfo.safeAreaWidth > 0.0f &&
                       m_frameInfo.safeAreaHeight > 0.0f;
            }

            float SafeAreaMinX() const
            {
                return HasSafeArea() ? m_frameInfo.safeAreaMinX : 0.0f;
            }

            float SafeAreaMinY() const
            {
                return HasSafeArea() ? m_frameInfo.safeAreaMinY : 0.0f;
            }

            float SafeAreaWidth() const
            {
                if (HasSafeArea())
                    return m_frameInfo.safeAreaWidth;
                return m_frameInfo.width > 0 ? static_cast<float>(m_frameInfo.width) : 0.0f;
            }

            float SafeAreaHeight() const
            {
                if (HasSafeArea())
                    return m_frameInfo.safeAreaHeight;
                return m_frameInfo.height > 0 ? static_cast<float>(m_frameInfo.height) : 0.0f;
            }

            float MapXToSurface(float x) const
            {
                return (x - m_frameInfo.inputRectMinX) *
                       static_cast<float>(m_frameInfo.width) /
                       m_frameInfo.inputRectWidth;
            }

            float MapYToSurface(float y) const
            {
                return (y - m_frameInfo.inputRectMinY) *
                       static_cast<float>(m_frameInfo.height) /
                       m_frameInfo.inputRectHeight;
            }

            ImVec2 MapFingerToSurface(float x, float y) const
            {
                if (HasInputRect())
                {
                    int windowWidth = 0;
                    int windowHeight = 0;
                    SDL_GetWindowSize(RHII.GetWindow(), &windowWidth, &windowHeight);
                    return ImVec2(MapXToSurface(x * static_cast<float>(windowWidth)),
                                  MapYToSurface(y * static_cast<float>(windowHeight)));
                }

                const float width = m_frameInfo.width > 0 ? static_cast<float>(m_frameInfo.width)
                                                          : static_cast<float>(RHII.GetWidth());
                const float height = m_frameInfo.height > 0 ? static_cast<float>(m_frameInfo.height)
                                                            : static_cast<float>(RHII.GetHeight());
                return ImVec2(x * width, y * height);
            }

            SDL_Event MapEventToSurface(const SDL_Event &event) const
            {
                if (!HasInputRect())
                    return event;

                SDL_Event mapped = event;
                if (mapped.type == SDL_MOUSEMOTION)
                {
                    mapped.motion.x = static_cast<int>(MapXToSurface(static_cast<float>(event.motion.x)));
                    mapped.motion.y = static_cast<int>(MapYToSurface(static_cast<float>(event.motion.y)));
                }
                else if (mapped.type == SDL_MOUSEBUTTONDOWN || mapped.type == SDL_MOUSEBUTTONUP)
                {
                    mapped.button.x = static_cast<int>(MapXToSurface(static_cast<float>(event.button.x)));
                    mapped.button.y = static_cast<int>(MapYToSurface(static_cast<float>(event.button.y)));
                }
                // SDL mouse-wheel events have no cursor coordinates; ImGui applies them at its cached mouse position.

                return mapped;
            }

            void SyncMousePosition(ImGuiIO &io) const
            {
                // ImGui_ImplSDL2_NewFrame (UpdateMouseData) re-injects the raw
                // window-relative mouse position from global state whenever no button
                // is held, bypassing MapEventToSurface. Queue the surface-mapped
                // position after it so this context never sees editor-window
                // coordinates — otherwise hover/click land on the wrong widgets when
                // the Viewport image is offset or letterboxed.
                if (!m_frameInfo.inputEnabled)
                {
                    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                    return;
                }

                if (!HasInputRect())
                    return;

                int mouseX = 0;
                int mouseY = 0;
                SDL_GetMouseState(&mouseX, &mouseY);
                io.AddMousePosEvent(MapXToSurface(static_cast<float>(mouseX)),
                                    MapYToSurface(static_cast<float>(mouseY)));
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
            std::string m_currentScreenId;
            std::string m_activeDragWidget;
            std::unordered_set<SDL_FingerID> m_touchFingers;
            std::vector<ImVec2> m_pendingTouchClicks;
            PeGraphicsApi m_api = PE_GRAPHICS_API_VULKAN;
            bool m_initialized = false;
            bool m_platformInitialized = false;
            bool m_rendererInitialized = false;
            bool m_frameOpen = false;
            bool m_rendered = false;
            bool m_currentScreenOverlay = false;
            std::unordered_map<Image *, void *> m_imageTextureIds;

            static constexpr int kFrameMsHistory = 180;
            static constexpr float kFrameGraphRefreshSeconds = 0.25f;
            float m_frameMs[kFrameMsHistory] = {}; // per-interval PEAK ms (graph points)
            int m_frameMsHead = 0;
            int m_frameMsCount = 0;
            float m_frameGraphAccum = 0.0f;      // time toward the next committed sample
            float m_frameGraphPeakMs = 0.0f;     // worst frame within the current interval
            float m_frameGraphSumMs = 0.0f;      // summed frame ms within the current interval
            int m_frameGraphFrames = 0;          // frames counted within the current interval
            float m_frameGraphDispMs = 0.0f;     // committed interval AVG (headline; matches FPS HUD)
            float m_frameGraphDispPeakMs = 0.0f; // committed interval PEAK (newest graph point)
            bool m_showFrameGraph = false;
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
