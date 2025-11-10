#include "GUI.h"
#include "Helpers.h"
#include "TinyFileDialogs/tinyfiledialogs.h"
#include "API/RHI.h"
#include "Scene/ModelGltf.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "API/RenderPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/SuperResolutionPass.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    GUI::GUI()
        : m_render{true},
          m_attachment{std::make_unique<Attachment>()},
          m_show_demo_window{false}
    {
    }

    GUI::~GUI()
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
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
                    ThreadPool::GUI.Enqueue(loadAsync);
                }
            };

            ThreadPool::GUI.Enqueue(lambda);
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
                    EventSystem::PushEvent(EventType::Quit);
                }
            };
            ThreadPool::GUI.Enqueue(lambda);
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
        static const int flags = ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoBackground;

        auto &gSettings = Settings::Get<GlobalSettings>();
        if (s_modelLoading)
        {
            int x, y;
            const float topBarHeight = 16.f;
            SDL_GetWindowPosition(RHII.GetWindow(), &x, &y);
            ImVec2 size(-0.001f, 25.f);
            ImGui::SetNextWindowPos(ImVec2(x + RHII.GetWidthf() / 2 - 200.f, y + topBarHeight + RHII.GetHeightf() / 2 - size.y / 2));
            ImGui::SetNextWindowSize(ImVec2(400.f, 100.f));
            ImGui::Begin(gSettings.loading_name.c_str(), &metrics_open, flags);
            // LoadingIndicatorCircle("Loading", radius, color, bdcolor, 10, 4.5f);
            const float progress = gSettings.loading_current / static_cast<float>(gSettings.loading_total);
            ImGui::ProgressBar(progress, size);
            ImGui::End();
        }
    }

#if PE_DEBUG_MODE
    float GUI::FetchTotalGPUTime()
    {
        float total = 0.f;
        for (auto &gpuTimerInfo : m_gpuTimerInfos)
        {
            // Top level timers only
            if (gpuTimerInfo.depth == 0)
                total += gpuTimerInfo.timer->GetTime();
        }

        return total;
    }

    void GUI::ShowGpuTimings(float maxTime)
    {
        for (auto &gpuTimerInfo : m_gpuTimerInfos)
        {
            // Top level timers are in CommandBuffer::Begin
            if (gpuTimerInfo.depth == 0)
                continue;

            if (gpuTimerInfo.depth > 0)
                ImGui::Indent(gpuTimerInfo.depth * 16.0f);

            float time = gpuTimerInfo.timer->GetTime();
            ui::SetTextColorTemp(time, maxTime);
            ImGui::Text("%s: %.3f ms", gpuTimerInfo.name.c_str(), time);

            if (gpuTimerInfo.depth > 0)
                ImGui::Unindent(gpuTimerInfo.depth * 16.0f);
        }
    }

    void GUI::ShowGpuTimingsTable(float totalMs)
    {
        // Optional filter
        static char filter[64] = "";
        ImGui::SetNextItemWidth(140);
        ImGui::InputTextWithHint("##timings_filter", "filter...", filter, IM_ARRAYSIZE(filter));

        ImGuiTableFlags flags =
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_NoSavedSettings |
            ImGuiTableFlags_ScrollY;

        // Height: show ~10 rows before scrolling
        const float row_h = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
        const float table_h = row_h * 10.0f;

        if (ImGui::BeginTable("##gpu_timings", 3, flags, ImVec2(-FLT_MIN, table_h)))
        {
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 0.65f);
            ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("rel", ImGuiTableColumnFlags_WidthStretch, 0.35f);
            ImGui::TableHeadersRow();

            for (const auto &info : m_gpuTimerInfos)
            {
                // skip your depth==0 “root” if you don’t want it listed
                if (info.depth == 0)
                    continue;

                const float t = info.timer->GetTime();
                if (t <= 0.0f)
                    continue;

                // simple name filter
                if (filter[0] != '\0' && std::string(info.name).find(filter) == std::string::npos)
                    continue;

                const float rel = (totalMs > 0.0f) ? (t / totalMs) : 0.0f;

                ImGui::TableNextRow();

                // col 0: name (pretty hierarchy)
                ImGui::TableSetColumnIndex(0);
                ui::DrawHierarchyCell(info.name.c_str(), static_cast<int>(info.depth), rel);

                // col 1: ms (heat colored)
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ui::Heat(rel), "%.3f", t);

                // col 2: tiny relative bar
                ImGui::TableSetColumnIndex(2);
                ui::TinyBar(rel);
            }

            ImGui::EndTable();
        }
    }

#endif

    void GUI::Metrics()
    {
        if (!metrics_open)
            return;

        static auto &gSettings = Settings::Get<GlobalSettings>();
        static std::deque<float> fpsDeque(100, 0.0f);
        static std::vector<float> fpsVector(100, 0.0f);
        static float highestValue = 100.0f;

        static Timer delay;
        float framerate = ImGui::GetIO().Framerate;
        if (delay.Count() > 0.2)
        {
            delay.Start();

            fpsDeque.pop_front();
            fpsDeque.push_back(framerate);
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

        // Memory Usage
        ImGui::Separator();
        const auto ram = RHII.GetSystemAndProcessMemory();
        const auto gpu = RHII.GetGpuMemorySnapshot();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f)); // compact Y spacing
        // 1) RAM — system used (other) / process (Private Bytes) / system total
        ui::DrawRamBar("RAM", ram.sysTotal, ram.procPrivateBytes, ram.sysUsed);
        // 2) GPU (Local/VRAM) — used / budget
        ui::DrawGpuBar("GPU (Local)", gpu.vram.app, gpu.vram.other, std::max<uint64_t>(gpu.vram.budget, 1), ImVec2(-FLT_MIN, 18.0f));
        // 3) GPU (Host Visible) — used / budget
        ui::DrawGpuBar("GPU (Host Visible)", gpu.host.app, gpu.host.other, std::max<uint64_t>(gpu.host.budget, 1), ImVec2(-FLT_MIN, 18.0f));

        ImGui::PopStyleVar();
        ImGui::Separator();

        FrameTimer &ft = FrameTimer::Instance();
        const float cpuTotal = (float)MILLI(ft.GetCpuTotal());
        const float cpuUpdates = (float)MILLI(ft.GetUpdatesStamp());
        const float cpuDraw = (float)MILLI(ft.GetCpuTotal() - ft.GetUpdatesStamp());
#if PE_DEBUG_MODE
        const float gpuTotal = FetchTotalGPUTime();
#else
        const float gpuTotal = 0.0f;
#endif
        // Compact local spacing
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f));
        ui::ShowCpuGpuSummary(cpuTotal, cpuUpdates, cpuDraw, gpuTotal);
        ImGui::PopStyleVar();
#if PE_DEBUG_MODE
        // keep the nice timings table you added below
        ShowGpuTimingsTable(gpuTotal);
        m_gpuTimerInfos.clear();
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
            EventSystem::PushEvent(EventType::CompileShaders);
        }
        for (uint32_t i = 0; i < gSettings.shader_list.size(); i++)
        {
            std::string name = gSettings.shader_list[i] + "##" + std::to_string(i);
            if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
            {
                if (ImGui::IsMouseDoubleClicked(0))
                {
                    std::string s = Path::Executable + "Assets/Shaders/" + gSettings.shader_list[i];
                    auto lambda = [s]()
                    { system(s.c_str()); };
                    ThreadPool::GUI.Enqueue(lambda);
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
            ImGui::SetNextWindowPos(ImVec2(RHII.GetWidthf() - 200.f, 100.f));
            ImGui::SetNextWindowSize(ImVec2(200.f, RHII.GetHeightf() - 150.f));
            initialized = true;
        }

        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &srSettings = Settings::Get<SRSettings>();

        static float rtScale = gSettings.render_scale;
        ImGui::Begin("Global Properties", &properties_open);
        ImGui::Text("Resolution: %d x %d", static_cast<int>(RHII.GetWidthf() * gSettings.render_scale), static_cast<int>(RHII.GetHeightf() * gSettings.render_scale));
        ImGui::DragFloat("Quality", &rtScale, 0.01f, 0.05f, 1.0f);
        if (ImGui::Button("Apply"))
        {
            gSettings.render_scale = clamp(rtScale, 0.1f, 4.0f);
            RHII.WaitDeviceIdle();
            EventSystem::PushEvent(EventType::Resize);
        }
        ImGui::Separator();

        ImGui::Text("Present Mode");
        static vk::PresentModeKHR currentPresentMode = RHII.GetSurface()->GetPresentMode();

        if (ImGui::BeginCombo("##present_mode", RHII.PresentModeToString(currentPresentMode)))
        {
            const auto &presentModes = RHII.GetSurface()->GetSupportedPresentModes();
            for (uint32_t i = 0; i < static_cast<uint32_t>(presentModes.size()); i++)
            {
                const bool isSelected = (currentPresentMode == presentModes[i]);
                if (ImGui::Selectable(RHII.PresentModeToString(presentModes[i]), isSelected))
                {
                    if (currentPresentMode != presentModes[i])
                    {
                        currentPresentMode = presentModes[i];
                        gSettings.preferred_present_mode = currentPresentMode;
                        EventSystem::PushEvent(EventType::PresentMode);
                    }
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
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
        ImGui::Checkbox("Frustum Culling", &gSettings.frustum_culling);
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
        std::string directory = Path::Executable + "Assets/Scripts";
        for (auto &file : std::filesystem::recursive_directory_iterator(directory))
        {
            auto pathStr = file.path().string();
            if (pathStr.ends_with(".cs"))
                gSettings.file_list.push_back(pathStr.erase(0, pathStr.find(directory) + directory.size() + 1));
        }

        directory = Path::Executable + "Assets/Shaders";
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
        ui::ApplyNiceTheme();

        ImGui_ImplSDL2_InitForVulkan(RHII.GetWindow());

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        m_attachment->image = renderer->GetDisplayRT();
        m_attachment->loadOp = vk::AttachmentLoadOp::eLoad;
        VkFormat format = static_cast<VkFormat>(RHII.GetSurface()->GetFormat());
        Queue *queue = RHII.GetMainQueue();

        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.Instance = RHII.GetInstance();
        init_info.PhysicalDevice = RHII.GetGpu();
        init_info.Device = RHII.GetDevice();
        init_info.QueueFamily = queue->GetFamilyId();
        init_info.Queue = queue->ApiHandle();
        init_info.PipelineCache = nullptr;
        init_info.DescriptorPool = RHII.GetDescriptorPool()->ApiHandle();
        init_info.Subpass = 0;
        init_info.MinImageCount = RHII.GetSwapchain()->GetImageCount();
        init_info.ImageCount = RHII.GetSwapchain()->GetImageCount();
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        init_info.Allocator = nullptr;
        init_info.CheckVkResultFn = nullptr;
        if (gSettings.dynamic_rendering)
        {
            init_info.UseDynamicRendering = true;
            init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
            init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
            init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;
        }
        else
        {
            RenderPass *renderPass = CommandBuffer::GetRenderPass(1, m_attachment.get());
            init_info.UseDynamicRendering = false;
            init_info.RenderPass = renderPass->ApiHandle();
        }

        ImGui_ImplVulkan_Init(&init_info);
        ImGui_ImplVulkan_CreateFontsTexture();

        auto &gpuTimerInfos = m_gpuTimerInfos;
        auto AddGpuTimerInfo = [&gpuTimerInfos](const std::any &data)
        {
            try
            {
                // cast to const ref because 'data' is const
                const auto &commandTimerInfos = std::any_cast<const std::vector<GpuTimerInfo> &>(data);
                if (commandTimerInfos.empty())
                    return;

                auto *cmd = commandTimerInfos[0].timer->GetCommandBuffer();
                uint32_t count = cmd->GetGpuTimerInfosCount();
                gpuTimerInfos.insert(gpuTimerInfos.end(), commandTimerInfos.begin(), commandTimerInfos.begin() + count);
            }
            catch (const std::bad_any_cast &ex)
            {
                PE_ERROR(std::string("Bad any cast in GUI::Init()::AddGpuTimerInfo: " + std::string(ex.what())).c_str());
                (void)ex;
            }
        };

        EventSystem::RegisterEvent(EventType::AfterCommandWait);
        EventSystem::RegisterCallback(EventType::AfterCommandWait, std::move(AddGpuTimerInfo));

        queue->WaitIdle();
    }

    void GUI::Draw(CommandBuffer *cmd)
    {
        if (!m_render || ImGui::GetDrawData()->TotalVtxCount <= 0)
            return;

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Image *displayRT = renderer->GetDisplayRT();
        m_attachment->image = displayRT;

        ImageBarrierInfo barrierInfo{};
        barrierInfo.image = displayRT;
        barrierInfo.layout = vk::ImageLayout::eAttachmentOptimal;
        barrierInfo.stageFlags = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        barrierInfo.accessMask = vk::AccessFlagBits2::eColorAttachmentRead;

        cmd->ImageBarrier(barrierInfo);
        cmd->BeginPass(1, m_attachment.get(), "GUI", !Settings::Get<GlobalSettings>().dynamic_rendering);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd->ApiHandle());
        cmd->EndPass();

        ImGui::RenderPlatformWindowsDefault(); // draws the windows outside the surface
    }

    void GUI::Update()
    {
        if (!m_render)
            return;

        if (m_show_demo_window)
            ImGui::ShowDemoWindow(&m_show_demo_window);

        Menu();
        Loading();
        Metrics();
        Properties();
        Shaders();
    }
}
