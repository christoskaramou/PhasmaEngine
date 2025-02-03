#include "GUI.h"
#include "TinyFileDialogs/tinyfiledialogs.h"
#include "Renderer/Vertex.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Scene/Model.h"
#include "Systems/CameraSystem.h"
#include "Window/Window.h"
#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Image.h"
#include "Renderer/Renderer.h"
#include "Renderer/Queue.h"
#include "Renderer/RenderPasses/LightPass.h"
#include "Renderer/RenderPasses/AabbsPass.h"
#include "Renderer/RenderPasses/SuperResolutionPass.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_impl_sdl.h"
#include "imgui/imgui_internal.h"
#include "Core/Timer.h"

namespace pe
{
    GUI::GUI()
    {
    }

    GUI::~GUI()
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }

    // https://github.com/ocornut/imgui/issues/1901#issuecomment-444929973
    void LoadingIndicatorCircle(const char *label, const float indicator_radius,
                                const ImVec4 &main_color, const ImVec4 &backdrop_color,
                                const int circle_count, const float speed)
    {
        ImGuiWindow *window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return;

        ImGuiContext &g = *GImGui;
        const ImGuiID id = window->GetID(label);

        const ImVec2 pos = window->DC.CursorPos;
        const float circle_radius = indicator_radius / 10.f;
        const ImRect bb(pos, ImVec2(pos.x + indicator_radius * 2.0f, pos.y + indicator_radius * 2.0f));
        ImGui::ItemSize(bb, ImGui::GetStyle().FramePadding.y);
        if (!ImGui::ItemAdd(bb, id))
            return;

        const float t = (float)g.Time;
        const auto degree_offset = 2.0f * IM_PI / circle_count;
        for (int i = 0; i < circle_count; ++i)
        {
            const auto x = indicator_radius * std::sin(degree_offset * i) * 0.9f;
            const auto y = indicator_radius * std::cos(degree_offset * i) * 0.9f;
            const auto growth = std::max(0.0f, std::sin(t * speed - i * degree_offset));
            ImVec4 color;
            color.x = main_color.x * growth + backdrop_color.x * (1.0f - growth);
            color.y = main_color.y * growth + backdrop_color.y * (1.0f - growth);
            color.z = main_color.z * growth + backdrop_color.z * (1.0f - growth);
            color.w = 1.0f;
            window->DrawList->AddCircleFilled(ImVec2(pos.x + indicator_radius + x,
                                                     pos.y + indicator_radius - y),
                                              circle_radius + growth * circle_radius,
                                              ImGui::GetColorU32(color));
        }
    }

    static std::atomic_bool s_modelLoading = false;

    void GUI::async_fileDialog_ImGuiMenuItem(const char *menuLabel, const char *dialogTitle,
                                             const std::vector<const char *> &filter)
    {
        if (ImGui::MenuItem(menuLabel))
        {
            if (s_modelLoading)
                return;

            auto lambda = [dialogTitle, filter]()
            {
                const char *result = tinyfd_openFileDialog(dialogTitle, "", static_cast<int>(filter.size()), filter.data(), "", 0);
                if (result)
                {
                    auto loadAsync = [result]()
                    {
                        s_modelLoading = true;

                        std::filesystem::path path(result);
                        ModelGltf::Load(path);

                        s_modelLoading = false;
                    };
                    e_GUI_ThreadPool.Enqueue(loadAsync);
                }
            };

            e_GUI_ThreadPool.Enqueue(lambda);
        }
    }

    void GUI::async_messageBox_ImGuiMenuItem(const char *menuLabel, const char *messageBoxTitle, const char *message)
    {
        if (ImGui::MenuItem(menuLabel))
        {
            auto lambda = [messageBoxTitle, message]()
            {
                int result = tinyfd_messageBox(messageBoxTitle, message, "yesno", "warning", 0);
                if (result == 1)
                {
                    EventSystem::PushEvent(EventQuit);
                }
            };
            e_GUI_ThreadPool.Enqueue(lambda);
        }
    }

    static bool metrics_open = true;
    static bool properties_open = true;
    static bool shaders_open = false;
    static bool models_open = false;
    static bool scripts_open = false;

    void GUI::Menu()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                static std::vector<const char *> filter{"*.gltf", "*.glb"};
                async_fileDialog_ImGuiMenuItem("Load...", "Choose Model", filter);
                async_messageBox_ImGuiMenuItem("Exit", "Exit", "Are you sure you want to exit?");
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Windows"))
            {
                ImGui::Checkbox("Metrics", &metrics_open);
                ImGui::Checkbox("Properties", &properties_open);
                ImGui::Checkbox("Shaders", &shaders_open);
                ImGui::Checkbox("Models", &models_open);
                ImGui::Checkbox("Scripts", &scripts_open);

                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void GUI::Loading()
    {
        static const ImVec4 color{0, 232.f / 256, 224.f / 256, 1.f};
        static const ImVec4 bdcolor{0.f, 168.f / 256, 162.f / 256, 1.f};
        static const float radius = 25.f;
        static const int flags = ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_NoBackground;

        auto &gSettings = Settings::Get<GlobalSettings>();
        if (s_modelLoading)
        {
            int x, y;
            const float topBarHeight = 16.f;
            SDL_GetWindowPosition(RHII.GetWindow(), &x, &y);
            ImVec2 size(-0.001f, 25.f);
            ImGui::SetNextWindowPos(ImVec2(x + WIDTH_f / 2 - 200.f, y + topBarHeight + HEIGHT_f / 2 - size.y / 2));
            ImGui::SetNextWindowSize(ImVec2(400.f, 25.f));
            ImGui::Begin("Model Loading", &metrics_open, flags);
            // LoadingIndicatorCircle("Loading", radius, color, bdcolor, 10, 4.5f);
            const float progress = gSettings.loading_current / static_cast<float>(gSettings.loading_total);
            ImGui::ProgressBar(progress, size);
            ImGui::End();
        }
    }

    void SetTextColorTemp(float time, float maxTime)
    {
        const ImVec4 startColor = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); // White
        const ImVec4 endColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);   // Red

        time = std::clamp(time / maxTime, 0.0f, 1.0f);

        ImVec4 resultColor(
            startColor.x + time * (endColor.x - startColor.x), // Red
            startColor.y + time * (endColor.y - startColor.y), // Green
            startColor.z + time * (endColor.z - startColor.z), // Blue
            1.0f                                               // Alpha
        );

        ImGui::GetStyle().Colors[ImGuiCol_Text] = resultColor;
    }

    void ResetTextColor()
    {
        ImGui::GetStyle().Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    }

#if PE_DEBUG_MODE
    float GUI::GetQueueTotalTime(Queue *queue)
    {
        float total = 0.f;
        uint32_t frame = RHII.GetFrameIndex();
        for (auto *cmd : queue->m_frameCmdsSubmitted[frame])
        {
            for (uint32_t i = 0; i < cmd->m_gpuTimerInfosCount; i++)
            {
                auto &timeInfo = cmd->m_gpuTimerInfos[i];

                // Top level timers are in CommandBuffer::Begin
                if (timeInfo.depth == 0)
                    total += timeInfo.timer->GetTime();
            }
        }

        return total;
    }

    void GUI::ShowQueueGpuTimings(Queue *queue, float maxTime)
    {
        uint32_t frame = RHII.GetFrameIndex();
        for (auto *cmd : queue->m_frameCmdsSubmitted[frame])
        {
            for (uint32_t i = 0; i < cmd->m_gpuTimerInfosCount; i++)
            {
                auto &timeInfo = cmd->m_gpuTimerInfos[i];

                // Top level timers are in CommandBuffer::Begin
                if (timeInfo.depth == 0)
                    continue;

                if (timeInfo.depth > 0)
                    ImGui::Indent(timeInfo.depth * 16.0f);

                float time = timeInfo.timer->GetTime();
                SetTextColorTemp(time, maxTime);
                ImGui::Text("%s: %.3f ms", timeInfo.name.c_str(), time);

                if (timeInfo.depth > 0)
                    ImGui::Unindent(timeInfo.depth * 16.0f);
            }
        }
    }
#endif

    void GUI::Metrics()
    {
        if (!metrics_open)
            return;

        auto &gSettings = Settings::Get<GlobalSettings>();
        static std::deque<float> fpsDeque(100, 0.0f);
        static std::vector<float> fpsVector(100, 0.0f);
        static float highestValue = 100.0f;
        static Timer delay;
        float framerate = ImGui::GetIO().Framerate;
        if (delay.Count() > 0.2)
        {
            delay.Start();
            fpsDeque.pop_front();          // Remove the oldest value
            fpsDeque.push_back(framerate); // Add the new value at the end
            highestValue = max(highestValue, framerate + 60.0f);
            std::copy(fpsDeque.begin(), fpsDeque.end(), fpsVector.begin());
        }

        ImGui::Begin("Metrics", &metrics_open);
        ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Average %.3f ms (%.1f FPS)", 1000.0f / framerate, framerate);
        ImGui::PlotLines("##FrameTimes", fpsVector.data(), static_cast<int>(fpsVector.size()), 0, NULL, 0.0f, highestValue, ImVec2(0, 80));
        ImGui::Checkbox("Unlock FPS", &gSettings.unlock_fps);
        if (!gSettings.unlock_fps)
        {
            ImGui::DragInt("FPS", &gSettings.target_fps, 0.1f);
            gSettings.target_fps = max(gSettings.target_fps, 10);
        }
        ImGui::Separator();
        ImGui::Separator();

        FrameTimer &frameTimer = FrameTimer::Instance();
        ImGui::Text("CPU Total: %.3f ms", static_cast<float>(MILLI(frameTimer.GetCpuTotal())));
        ImGui::Indent(16.0f);
        SetTextColorTemp(static_cast<float>(frameTimer.GetUpdatesStamp()), static_cast<float>(frameTimer.GetCpuTotal()));
        ImGui::Text("CPU Updates: %.3f ms", static_cast<float>(MILLI(frameTimer.GetUpdatesStamp())));
        SetTextColorTemp(static_cast<float>(frameTimer.GetCpuTotal() - frameTimer.GetUpdatesStamp()), static_cast<float>(frameTimer.GetCpuTotal()));
        ImGui::Text("CPU Draw: %.3f ms", static_cast<float>(MILLI(frameTimer.GetCpuTotal() - frameTimer.GetUpdatesStamp())));
        ResetTextColor();
        ImGui::Unindent(16.0f);

#if PE_DEBUG_MODE
        std::unordered_set<Queue *> queues{
            RHII.GetRenderQueue(),
            RHII.GetTransferQueue(),
            RHII.GetComputeQueue(),
            RHII.GetPresentQueue()};

        float gpuTotal = 0.f;
        for (auto *queue : queues)
            gpuTotal += GetQueueTotalTime(queue);

        ImGui::Text("GPU Total: %.3f ms", gpuTotal);
        ImGui::Indent(16.0f);
        for (auto *queue : queues)
        {
            std::string name;
            if (queue == RHII.GetRenderQueue())
                name = "Render Queue";
            else if (queue == RHII.GetTransferQueue())
                name = "Transfer Queue";
            else if (queue == RHII.GetComputeQueue())
                name = "Compute Queue";
            else if (queue == RHII.GetPresentQueue())
                name = "Present Queue";

            float time = GetQueueTotalTime(queue);
            SetTextColorTemp(time, gpuTotal);
            ImGui::Text("%s: %.3f ms", name.c_str(), time);
            ShowQueueGpuTimings(queue, gpuTotal);
        }
        ImGui::Unindent(16.0f);
        ImGui::GetStyle().Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);

        uint32_t frame = RHII.GetFrameIndex();
        for (auto *queue : queues)
        {
            for (auto *cmd : queue->m_frameCmdsSubmitted[frame])
                cmd->m_gpuTimerInfosCount = 0;

            queue->m_frameCmdsSubmitted[frame].clear();
        }
#endif
        ImGui::End();
    }

    void GUI::Shaders()
    {
        if (!shaders_open)
            return;

        auto &gSettings = Settings::Get<GlobalSettings>();
        ImGui::Begin("Shaders Folder", &shaders_open);
        if (ImGui::Button("Compile Shaders"))
        {
            EventSystem::PushEvent(EventCompileShaders);
        }
        for (uint32_t i = 0; i < gSettings.shader_list.size(); i++)
        {
            std::string name = gSettings.shader_list[i] + "##" + std::to_string(i);
            if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
            {
                if (ImGui::IsMouseDoubleClicked(0))
                {
                    std::string s = Path::Assets + "Shaders\\" + gSettings.shader_list[i];
                    auto lambda = [s]()
                    { system(s.c_str()); };
                    e_GUI_ThreadPool.Enqueue(lambda);
                }
            }
        }
        ImGui::End();
    }

    void GUI::Properties()
    {
        if (!properties_open)
            return;

        static bool initialized = false;
        if (!initialized)
        {
            ImGui::SetNextWindowPos(ImVec2(WIDTH_f - 200.f, 100.f));
            ImGui::SetNextWindowSize(ImVec2(200.f, HEIGHT_f - 150.f));
            initialized = true;
        }

        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &srSettings = Settings::Get<SRSettings>();

        static float rtScale = gSettings.render_scale;
        ImGui::Begin("Global Properties", &properties_open);
        ImGui::Text("Resolution: %d x %d", static_cast<int>(WIDTH_f * gSettings.render_scale), static_cast<int>(HEIGHT_f * gSettings.render_scale));
        ImGui::DragFloat("Quality", &rtScale, 0.01f, 0.05f, 1.0f);
        if (ImGui::Button("Apply"))
        {
            gSettings.render_scale = clamp(rtScale, 0.1f, 4.0f);
            RHII.WaitDeviceIdle();
            EventSystem::PushEvent(EventResize);
        }
        ImGui::Separator();

        ImGui::Checkbox("IBL", &gSettings.IBL);
        if (gSettings.IBL)
        {
            ImGui::Indent(16.0f);
            ImGui::DragFloat("IBL Intensity", &gSettings.IBL_intensity, 0.01f, 0.0f, 10.0f);
            ImGui::Unindent(16.0f);
        }
        ImGui::Checkbox("SSR", &gSettings.ssr);
        ImGui::Checkbox("SSAO", &gSettings.ssao);
        ImGui::Checkbox("Depth of Field", &gSettings.dof);
        if (gSettings.dof)
        {
            ImGui::Indent(16.0f);
            ImGui::DragFloat("Scale##DOF", &gSettings.dof_focus_scale, 0.05f, 0.5f);
            ImGui::DragFloat("Range##DOF", &gSettings.dof_blur_range, 0.05f, 0.5f);
            ImGui::Unindent(16.0f);
            ImGui::Separator();
        }
        ImGui::Checkbox("Motion Blur", &gSettings.motion_blur);
        if (gSettings.motion_blur)
        {
            ImGui::Indent(16.0f);
            ImGui::DragFloat("Strength#mb", &gSettings.motion_blur_strength, 0.05f, 0.2f);
            ImGui::DragInt("Samples#mb", &gSettings.motion_blur_samples, 0.1f, 8, 64);
            ImGui::Unindent(16.0f);
            ImGui::Separator();
        }
        ImGui::Checkbox("Tone Mapping", &gSettings.tonemapping);
#if 0
        ImGui::Checkbox("FSR2", &srSettings.enable);
#else
        // TODO: FSR2 is out of sync and out of date
        ImGui::Checkbox("FSR2 (broken)", &srSettings.enable);
        srSettings.enable = false;
#endif
        if (srSettings.enable)
        {
            ImGui::Indent(16.0f);
            ImGui::InputFloat2("Motion", (float *)&srSettings.motionScale);
            ImGui::InputFloat2("Proj", (float *)&srSettings.projScale);
            ImGui::Unindent(16.0f);
            ImGui::Separator();
        }

        ImGui::Checkbox("FXAA", &gSettings.fxaa);

        ImGui::Checkbox("Bloom", &gSettings.bloom);
        if (gSettings.bloom)
        {
            ImGui::Indent(16.0f);
            ImGui::SliderFloat("Strength", &gSettings.bloom_strength, 0.01f, 10.f);
            ImGui::SliderFloat("Range", &gSettings.bloom_range, 0.1f, 20.f);
            ImGui::Unindent(16.0f);
            ImGui::Separator();
        }

        if (ImGui::Checkbox("Sun Light", &gSettings.shadows))
        {
            // Update light pass descriptor sets for the skybox change
            LightOpaquePass &lightOpaquePass = *GetGlobalComponent<LightOpaquePass>();
            lightOpaquePass.UpdateDescriptorSets();
            LightTransparentPass &lightTransparentPass = *GetGlobalComponent<LightTransparentPass>();
            lightTransparentPass.UpdateDescriptorSets();
        }
        if (gSettings.shadows)
        {
            ImGui::Indent(16.0f);
            ImGui::SliderFloat("Intst", &gSettings.sun_intensity, 0.1f, 50.f);
            ImGui::DragFloat3("Dir", gSettings.sun_direction.data(), 0.01f, -1.f, 1.f);
            ImGui::DragFloat("Slope", &gSettings.depth_bias[2], 0.15f, 0.5f);
            ImGui::Separator();
            {
                vec3 direction = normalize(make_vec3(&gSettings.sun_direction[0]));
                gSettings.sun_direction[0] = direction.x;
                gSettings.sun_direction[1] = direction.y;
                gSettings.sun_direction[2] = direction.z;
            }
            ImGui::Unindent(16.0f);
        }

        ImGui::DragFloat("CamSpeed", &gSettings.camera_speed, 0.1f, 1.f);
        ImGui::DragFloat("TimeScale", &gSettings.time_scale, 0.05f, 0.2f);
        ImGui::Separator();
        if (ImGui::Button("Randomize Lights"))
            gSettings.randomize_lights = true;
        ImGui::SliderFloat("Light Intst", &gSettings.lights_intensity, 0.01f, 30.f);
        ImGui::SliderFloat("Light Rng", &gSettings.lights_range, 0.1f, 30.f);
        ImGui::Checkbox("FreezeCamCull", &gSettings.freeze_frustum_culling);
        ImGui::Checkbox("Draw AABBs", &gSettings.draw_aabbs);
        if (gSettings.draw_aabbs)
        {
            bool depthAware = gSettings.aabbs_depth_aware;
            ImGui::Indent(16.0f);
            ImGui::Checkbox("Depth Aware", &gSettings.aabbs_depth_aware);
            ImGui::Unindent(16.0f);
        }
        ImGui::DragInt("Culls Per Task", &gSettings.culls_per_task, 1.f, 1, 100);

        ImGui::End();
    }

    void GUI::Init()
    {
        //                   counter clock wise
        // x, y, z coords orientation   // u, v coords orientation
        //          |  /|               // (0,0)-------------> u
        //          | /  +z             //     |
        //          |/                  //     |
        //  --------|-------->          //     |
        //         /|       +x          //     |
        //        /	|                   //     |
        //       /  |/ +y               //     |/ v

        auto &gSettings = Settings::Get<GlobalSettings>();
        std::string directory = Path::Assets + "Scripts";
        for (auto &file : std::filesystem::recursive_directory_iterator(directory))
        {
            auto pathStr = file.path().string();
            if (pathStr.ends_with(".cs"))
                gSettings.file_list.push_back(pathStr.erase(0, pathStr.find(directory) + directory.size() + 1));
        }

        directory = Path::Assets + "Shaders";
        for (auto &file : std::filesystem::recursive_directory_iterator(directory))
        {
            auto pathStr = file.path().string();
            if (pathStr.ends_with(".vert") || pathStr.ends_with(".frag") || pathStr.ends_with(".comp") ||
                pathStr.ends_with(".glsl"))
            {
                gSettings.shader_list.push_back(pathStr.erase(0, pathStr.find(directory) + directory.size() + 1));
            }
        }

        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;          // Enable docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;        // Enable multiple viewports
        io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports; // Backend Renderer supports multiple viewports.

        ImGui::StyleColorsClassic();
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.3f);

        ImGui_ImplSDL2_InitForVulkan(RHII.GetWindow());

        Queue *queue = Queue::Get(QueueType::GraphicsBit | QueueType::TransferBit, 1);
        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.Instance = RHII.GetInstance();
        initInfo.PhysicalDevice = RHII.GetGpu();
        initInfo.Device = RHII.GetDevice();
        initInfo.QueueFamily = queue->GetFamilyId();
        initInfo.Queue = queue->ApiHandle();
        initInfo.PipelineCache = nullptr;
        initInfo.DescriptorPool = RHII.GetDescriptorPool()->ApiHandle();
        initInfo.Subpass = 0;
        initInfo.MinImageCount = RHII.GetSwapchain()->GetImageCount();
        initInfo.ImageCount = RHII.GetSwapchain()->GetImageCount();
        initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.Allocator = nullptr;
        initInfo.CheckVkResultFn = nullptr;

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        m_attachment = std::make_unique<Attachment>();
        m_attachment->image = renderer->GetDisplayRT();
        m_attachment->loadOp = AttachmentLoadOp::Load;
        RenderPass *renderPass = CommandBuffer::GetRenderPass(1, m_attachment.get());
        ImGui_ImplVulkan_Init(&initInfo, renderPass->ApiHandle());

        CommandBuffer *cmd = CommandBuffer::GetFree(queue);
        cmd->Begin();
        ImGui_ImplVulkan_CreateFontsTexture(cmd->ApiHandle());
        cmd->End();
        queue->Submit(1, &cmd, 0, nullptr, nullptr, 0, nullptr, nullptr);

        cmd->Wait();
        CommandBuffer::Return(cmd);

        queue->WaitIdle();
    }

    void GUI::Draw(CommandBuffer *cmd)
    {
        if (!render || ImGui::GetDrawData()->TotalVtxCount <= 0)
            return;

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Image *displayRT = renderer->GetDisplayRT();
        m_attachment->image = displayRT;

        ImageBarrierInfo barrierInfo{};
        barrierInfo.image = displayRT;
        barrierInfo.layout = ImageLayout::Attachment;
        barrierInfo.stageFlags = PipelineStage::ColorAttachmentOutputBit;
        barrierInfo.accessMask = Access::ColorAttachmentReadBit;

        cmd->ImageBarrier(barrierInfo);
        cmd->BeginPass(1, m_attachment.get(), "GUI", true);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd->ApiHandle());
        cmd->EndPass();

        ImGui::RenderPlatformWindowsDefault(); // draws the windows outside the surface
    }

    void GUI::Update()
    {
        if (!render)
            return;

        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        Menu();
        Loading();
        Metrics();
        Properties();
        Shaders();
    }
}
