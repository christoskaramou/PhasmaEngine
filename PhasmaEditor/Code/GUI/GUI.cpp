#include "GUI.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/RenderPass.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "Helpers.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/SuperResolutionPass.h"
#include "Scene/Model.h"
#include "Systems/RendererSystem.h"
#include "TinyFileDialogs/tinyfiledialogs.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_vulkan.h"


namespace pe
{
    GUI::GUI()
        : m_render{true},
          m_attachment{std::make_unique<Attachment>()},
          m_show_demo_window{false},
          m_dockspaceId{0},
          m_dockspaceInitialized{false},
          m_requestDockReset{false},
          m_viewportTextureId{nullptr},
          m_sceneViewFloating{false},
          m_sceneViewRedockQueued{false},
          m_sceneViewImage{nullptr},
          m_sceneObjectsOpen{true}
    {
    }

    GUI::~GUI()
    {
        if (m_viewportTextureId)
            ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)m_viewportTextureId);

        Image::Destroy(m_sceneViewImage);

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
                        Model::Load(path);

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
    static bool asset_viewer_open = true;

    enum class AssetPreviewType
    {
        None,
        Model,
        Script,
        Shader
    };

    struct AssetPreviewState
    {
        AssetPreviewType type = AssetPreviewType::None;
        std::string label{};
        std::string fullPath{};
    };

    static AssetPreviewState s_assetPreview{};

    static void OpenExternalPath(const std::string &absPath)
    {
#if defined(_WIN32)
        std::string command = "cmd /c start \"\" \"" + absPath + "\"";
#elif defined(__APPLE__)
        std::string command = "open \"" + absPath + "\"";
#else
        std::string command = "xdg-open \"" + absPath + "\"";
#endif
        ThreadPool::GUI.Enqueue([command]()
                                { system(command.c_str()); });
    }

    static void UpdateAssetPreview(AssetPreviewType type, const std::string &label, const std::string &fullPath)
    {
        s_assetPreview.type = type;
        s_assetPreview.label = label;
        s_assetPreview.fullPath = fullPath;
    }

    static void SetInitialWindowSizeFraction(float widthFraction, float heightFraction = -1.0f, ImGuiCond cond = ImGuiCond_FirstUseEver)
    {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImVec2 size = ImVec2(
            viewport->WorkSize.x * widthFraction,
            viewport->WorkSize.y * (heightFraction > 0.0f ? heightFraction : widthFraction));
        ImGui::SetNextWindowSize(size, cond);
    }

    void GUI::BuildDockspace()
    {
        if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable))
            return;

        ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        const ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoBackground;

        bool open = true;
        ImGui::Begin("EditorDockSpaceHost", &open, windowFlags);
        ImGui::PopStyleVar(3);

        const ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

        ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
        m_dockspaceId = static_cast<uint32_t>(dockspaceId);
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);

        if (!m_dockspaceInitialized || m_requestDockReset)
        {
            ResetDockspaceLayout(m_dockspaceId);
            m_dockspaceInitialized = true;
            m_requestDockReset = false;
        }

        ImGui::End();
    }

    void GUI::ResetDockspaceLayout(uint32_t dockspaceId)
    {
        if (dockspaceId == 0)
            return;

        ImGuiID dockspace = static_cast<ImGuiID>(dockspaceId);
        ImGuiViewport *viewport = ImGui::GetMainViewport();

        ImGui::DockBuilderRemoveNode(dockspace);
        ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(dockspace, viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);

        constexpr float dockRightFrac = 1.0f / 7.0f;
        constexpr float dockLeftFrac = 1.0f / 7.0f;
        constexpr float dockBottomFrac = 1.0f / 4.5f;
        constexpr float dockViewerFrac = 0.32f;

        ImGuiID dockMainId = dockspace;
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, dockRightFrac, nullptr, &dockMainId);
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, dockLeftFrac, nullptr, &dockMainId);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, dockBottomFrac, nullptr, &dockMainId);
        ImGuiID dockBottomViewer = ImGui::DockBuilderSplitNode(dockBottom, ImGuiDir_Left, dockViewerFrac, nullptr, &dockBottom);

        // Central node is now dockMainId - dock the Scene View there
        ImGui::DockBuilderDockWindow("Scene View", dockMainId);
        ImGui::DockBuilderDockWindow("Metrics", dockLeft);
        ImGui::DockBuilderDockWindow("Models", dockLeft);
        ImGui::DockBuilderDockWindow("Scene Objects", dockLeft);
        ImGui::DockBuilderDockWindow("Global Properties", dockRight);
        ImGui::DockBuilderDockWindow("Asset Viewer", dockBottomViewer);
        ImGui::DockBuilderDockWindow("Shaders Folder", dockBottom);
        ImGui::DockBuilderDockWindow("Scripts", dockBottom);

        ImGui::DockBuilderFinish(dockspace);
    }

    void GUI::Menu()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                static std::vector<const char *> filter{"*.gltf", "*.glb"};
                async_fileDialog_ImGuiMenuItem("Load...", "Choose Model", filter);
                ImGui::Separator();
                async_messageBox_ImGuiMenuItem("Exit", "Exit", "Are you sure you want to exit?");
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
                ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
                ImGui::Separator();
                ImGui::MenuItem("Preferences...", "Ctrl+,", &properties_open);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Assets"))
            {
                if (ImGui::MenuItem("Recompile Shaders", "Ctrl+Shift+R"))
                {
                    EventSystem::PushEvent(EventType::CompileShaders);
                }
                ImGui::MenuItem("Shaders Browser", nullptr, &shaders_open);
                ImGui::MenuItem("Scripts Browser", nullptr, &scripts_open);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window"))
            {
                ImGui::MenuItem("Metrics", nullptr, &metrics_open);
                ImGui::MenuItem("Global Properties", nullptr, &properties_open);
                ImGui::MenuItem("Shaders Folder", nullptr, &shaders_open);
                ImGui::MenuItem("Models", nullptr, &models_open);
                ImGui::MenuItem("Scripts", nullptr, &scripts_open);
                ImGui::MenuItem("Asset Viewer", nullptr, &asset_viewer_open);
                ImGui::MenuItem("Scene Objects", nullptr, &m_sceneObjectsOpen);

                bool prevFloating = m_sceneViewFloating;
                ImGui::MenuItem("Float Scene View", nullptr, &m_sceneViewFloating);

                if (prevFloating != m_sceneViewFloating)
                {
                    if (!m_sceneViewFloating)
                        m_sceneViewRedockQueued = true;
                    else
                        m_sceneViewRedockQueued = false;
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Layout"))
            {
                if (ImGui::MenuItem("Reset to Default Layout", "Ctrl+Shift+L"))
                {
                    m_requestDockReset = true;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                ImGui::MenuItem("Dear ImGui Demo", nullptr, &m_show_demo_window);
                ImGui::MenuItem("Show Metrics", nullptr, &metrics_open);
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
                total += gpuTimerInfo.timeMs;
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

            const float time = gpuTimerInfo.timeMs;
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

                const float t = info.timeMs;
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

        SetInitialWindowSizeFraction(1.0f / 5.5f, 1.0f / 3.0f);
        ImGui::Begin("Metrics", &metrics_open);
        ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Average %.3f ms (%.1f FPS)", 1000.0f / framerate, framerate);
        ImGui::PlotLines("##FrameTimes", fpsVector.data(), static_cast<int>(fpsVector.size()), 0, NULL, 0.0f, highestValue, ImVec2(0, 80));

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
        SetInitialWindowSizeFraction(1.0f / 5.5f, 1.0f / 3.5f);
        ImGui::Begin("Shaders Folder", &shaders_open);
        if (ImGui::Button("Compile Shaders"))
        {
            EventSystem::PushEvent(EventType::CompileShaders);
        }
        const ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;
        for (uint32_t i = 0; i < gSettings.shader_list.size(); i++)
        {
            const std::string &relative = gSettings.shader_list[i];
            std::string label = relative + "##shader_" + std::to_string(i);
            const bool selected = (s_assetPreview.type == AssetPreviewType::Shader && s_assetPreview.label == relative);
            if (ImGui::Selectable(label.c_str(), selected, flags))
            {
                std::string fullPath = Path::Executable + "Assets/Shaders/" + relative;
                UpdateAssetPreview(AssetPreviewType::Shader, relative, fullPath);
                if (ImGui::IsMouseDoubleClicked(0))
                {
                    OpenExternalPath(fullPath);
                }
            }
        }
        ImGui::End();
    }

    void GUI::Models()
    {
        if (!models_open)
            return;

        auto &gSettings = Settings::Get<GlobalSettings>();
        SetInitialWindowSizeFraction(1.0f / 6.0f, 1.0f / 3.0f);
        ImGui::Begin("Models", &models_open);
        ImGui::TextDisabled("Double click an asset to load it into the scene");

        static ImGuiTextFilter modelFilter;
        modelFilter.Draw("Filter##models", ImGui::GetFontSize() * 14.0f);
        ImGui::Separator();

        const ImGuiSelectableFlags selectFlags = ImGuiSelectableFlags_AllowDoubleClick;
        if (ImGui::BeginChild("##models_child", ImVec2(0, 0), true))
        {
            if (gSettings.model_list.empty())
            {
                ImGui::TextDisabled("No model assets were found under Assets/Objects");
            }

            for (const auto &entry : gSettings.model_list)
            {
                if (!modelFilter.PassFilter(entry.c_str()))
                    continue;

                const bool selected = (s_assetPreview.type == AssetPreviewType::Model && s_assetPreview.label == entry);
                if (ImGui::Selectable(entry.c_str(), selected, selectFlags))
                {
                    std::string fullPath = Path::Executable + "Assets/Objects/" + entry;
                    UpdateAssetPreview(AssetPreviewType::Model, entry, fullPath);
                    if (ImGui::IsMouseDoubleClicked(0) && !s_modelLoading)
                    {
                        auto loadTask = [fullPath]()
                        {
                            s_modelLoading = true;
                            Model::Load(std::filesystem::path(fullPath));
                            s_modelLoading = false;
                        };
                        ThreadPool::GUI.Enqueue(loadTask);
                    }
                }
            }

            ImGui::EndChild();
        }

        ImGui::End();
    }

    void GUI::Scripts()
    {
        if (!scripts_open)
            return;

        auto &gSettings = Settings::Get<GlobalSettings>();
        SetInitialWindowSizeFraction(1.0f / 6.0f, 1.0f / 3.0f);
        ImGui::Begin("Scripts", &scripts_open);
        ImGui::TextDisabled("Double click a script to open it externally");

        static ImGuiTextFilter scriptFilter;
        scriptFilter.Draw("Filter##scripts", ImGui::GetFontSize() * 14.0f);
        ImGui::Separator();

        const ImGuiSelectableFlags selectFlags = ImGuiSelectableFlags_AllowDoubleClick;
        if (ImGui::BeginChild("##scripts_child", ImVec2(0, 0), true))
        {
            if (gSettings.file_list.empty())
            {
                ImGui::TextDisabled("No .cs scripts found under Assets/Scripts");
            }

            for (const auto &entry : gSettings.file_list)
            {
                if (!scriptFilter.PassFilter(entry.c_str()))
                    continue;

                const bool selected = (s_assetPreview.type == AssetPreviewType::Script && s_assetPreview.label == entry);
                if (ImGui::Selectable(entry.c_str(), selected, selectFlags))
                {
                    std::string fullPath = Path::Executable + "Assets/Scripts/" + entry;
                    UpdateAssetPreview(AssetPreviewType::Script, entry, fullPath);
                    if (ImGui::IsMouseDoubleClicked(0))
                    {
                        OpenExternalPath(fullPath);
                    }
                }
            }

            ImGui::EndChild();
        }

        ImGui::End();
    }

    static const char *AssetPreviewTypeToText(AssetPreviewType type)
    {
        switch (type)
        {
        case AssetPreviewType::Model:
            return "Model";
        case AssetPreviewType::Script:
            return "Script";
        case AssetPreviewType::Shader:
            return "Shader";
        default:
            return "None";
        }
    }

    void GUI::AssetViewer()
    {
        if (!asset_viewer_open)
            return;

        SetInitialWindowSizeFraction(0.35f, 1.0f / 4.0f);
        ImGui::Begin("Asset Viewer", &asset_viewer_open);

        if (s_assetPreview.type == AssetPreviewType::None || s_assetPreview.label.empty())
        {
            ImGui::TextDisabled("Select a model, script, or shader to inspect it here.");
            ImGui::End();
            return;
        }

        ImGui::Text("Type: %s", AssetPreviewTypeToText(s_assetPreview.type));
        ImGui::Text("Asset: %s", s_assetPreview.label.c_str());
        if (!s_assetPreview.fullPath.empty())
        {
            ImGui::TextWrapped("Path: %s", s_assetPreview.fullPath.c_str());
            if (ImGui::Button("Open File"))
            {
                OpenExternalPath(s_assetPreview.fullPath);
            }
            const std::string folder = std::filesystem::path(s_assetPreview.fullPath).parent_path().string();
            if (!folder.empty())
            {
                ImGui::SameLine();
                if (ImGui::Button("Reveal In Explorer"))
                    OpenExternalPath(folder);
            }

            if (s_assetPreview.type == AssetPreviewType::Model)
            {
                ImGui::SameLine();
                const bool canLoad = !s_modelLoading;
                if (!canLoad)
                    ImGui::BeginDisabled();

                if (ImGui::Button("Load Model"))
                {
                    auto path = std::filesystem::path(s_assetPreview.fullPath);
                    ThreadPool::GUI.Enqueue([path]()
                                            {
                                                s_modelLoading = true;
                                                Model::Load(path);
                                                s_modelLoading = false; });
                }

                if (!canLoad)
                    ImGui::EndDisabled();
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Preview");
        ImGui::BeginChild("##asset_viewer_canvas", ImVec2(0, 0), true);
        ImGui::TextDisabled("Visual previews will live here.");
        ImGui::EndChild();

        ImGui::End();
    }

    void GUI::Properties()
    {
        if (!properties_open)
            return;

        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &srSettings = Settings::Get<SRSettings>();

        static float rtScale = gSettings.render_scale;
        static bool propertiesSized = false;
        if (!propertiesSized)
        {
            SetInitialWindowSizeFraction(1.0f / 7.0f, 0.35f, ImGuiCond_Always);
            propertiesSized = true;
        }
        else
        {
            SetInitialWindowSizeFraction(1.0f / 7.0f, 0.35f, ImGuiCond_Appearing);
        }
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

        static bool dynamic_rendering = gSettings.dynamic_rendering;
        if (ImGui::Checkbox("Dynamic Rendering", &dynamic_rendering))
        {
            EventSystem::PushEvent(EventType::DynamicRendering, dynamic_rendering);
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
        gSettings.file_list.clear();
        gSettings.shader_list.clear();
        gSettings.model_list.clear();

        auto Deduplicate = [](auto &vec)
        {
            std::sort(vec.begin(), vec.end());
            vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
        };

        const std::filesystem::path scriptsDir = std::filesystem::path(Path::Executable + "Assets/Scripts");
        if (std::filesystem::exists(scriptsDir))
        {
            for (auto &file : std::filesystem::recursive_directory_iterator(scriptsDir))
            {
                if (!file.is_regular_file())
                    continue;
                if (file.path().extension() == ".cs")
                    gSettings.file_list.push_back(std::filesystem::relative(file.path(), scriptsDir).generic_string());
            }
        }
        Deduplicate(gSettings.file_list);

        const std::filesystem::path shadersDir = std::filesystem::path(Path::Executable + "Assets/Shaders");
        if (std::filesystem::exists(shadersDir))
        {
            for (auto &file : std::filesystem::recursive_directory_iterator(shadersDir))
            {
                if (!file.is_regular_file())
                    continue;

                auto extension = file.path().extension().string();
                if (extension == ".vert" || extension == ".frag" || extension == ".comp" || extension == ".glsl" ||
                    extension == ".hlsl")
                {
                    gSettings.shader_list.push_back(std::filesystem::relative(file.path(), shadersDir).generic_string());
                }
            }
        }
        Deduplicate(gSettings.shader_list);

        const std::filesystem::path modelsDir = std::filesystem::path(Path::Executable + "Assets/Objects");
        if (std::filesystem::exists(modelsDir))
        {
            for (auto &file : std::filesystem::recursive_directory_iterator(modelsDir))
            {
                if (!file.is_regular_file())
                    continue;
                auto extension = file.path().extension().string();
                if (extension == ".gltf" || extension == ".glb")
                    gSettings.model_list.push_back(std::filesystem::relative(file.path(), modelsDir).generic_string());
            }
        }
        Deduplicate(gSettings.model_list);

        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // Enable docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable multiple viewports

        ImGui::StyleColorsClassic();
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.3f);
        ui::ApplyNiceTheme();

        ImGui_ImplSDL2_InitForVulkan(RHII.GetWindow());

        // Verify the SDL2 backend supports platform windows
        PE_ERROR_IF(!(io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports),
                    "SDL2 backend doesn't support platform viewports!");

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        m_attachment->image = renderer->GetDisplayRT();
        m_attachment->loadOp = vk::AttachmentLoadOp::eLoad;
        Image::Destroy(m_sceneViewImage);
        m_sceneViewImage = renderer->CreateFSSampledImage(false);
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
        // if (gSettings.dynamic_rendering)
        // {
        //     init_info.UseDynamicRendering = true;
        //     init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        //     init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        //     init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &format;
        // }
        // else
        {
            RenderPass *renderPass = CommandBuffer::GetRenderPass(1, m_attachment.get());
            init_info.UseDynamicRendering = false;
            init_info.RenderPass = renderPass->ApiHandle();
        }

        ImGui_ImplVulkan_Init(&init_info);
        ImGui_ImplVulkan_CreateFontsTexture();

        // Verify Vulkan backend supports platform windows
        // PE_ERROR_IF(!(io.BackendFlags & ImGuiBackendFlags_RendererHasViewports),
        //             "Vulkan backend doesn't support renderer viewports!");

        auto &gpuTimerInfos = m_gpuTimerInfos;
        auto AddGpuTimerInfo = [&gpuTimerInfos](const std::any &data)
        {
            try
            {
                const auto &commandTimerInfos = std::any_cast<const std::vector<GpuTimerSample> &>(data);
                if (commandTimerInfos.empty())
                    return;

                gpuTimerInfos.insert(gpuTimerInfos.end(), commandTimerInfos.begin(), commandTimerInfos.end());
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

        const bool canCopySceneView =
            m_sceneViewImage && displayRT &&
            m_sceneViewImage->GetWidth() == displayRT->GetWidth() &&
            m_sceneViewImage->GetHeight() == displayRT->GetHeight();

        if (canCopySceneView)
        {
            cmd->CopyImage(displayRT, m_sceneViewImage);

            ImageBarrierInfo sceneViewBarrier{};
            sceneViewBarrier.image = m_sceneViewImage;
            sceneViewBarrier.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
            sceneViewBarrier.stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
            sceneViewBarrier.accessMask = vk::AccessFlagBits2::eShaderRead;
            cmd->ImageBarrier(sceneViewBarrier);
        }

        ImageBarrierInfo barrierInfo{};
        barrierInfo.image = displayRT;
        barrierInfo.layout = vk::ImageLayout::eAttachmentOptimal;
        barrierInfo.stageFlags = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        barrierInfo.accessMask = vk::AccessFlagBits2::eColorAttachmentWrite;

        cmd->ImageBarrier(barrierInfo);
        cmd->BeginPass(1, m_attachment.get(), "GUI", true);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd->ApiHandle());
        cmd->EndPass();
    }

    void GUI::DrawPlatformWindows()
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    void GUI::SceneView()
    {
        if (m_sceneViewFloating)
        {
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        }
        else if (m_sceneViewRedockQueued && m_dockspaceId != 0)
        {
            ImGui::DockBuilderDockWindow("Scene View", m_dockspaceId);
            m_sceneViewRedockQueued = false;
        }

        ImGuiWindowFlags sceneViewFlags = 0;
        if (m_sceneViewFloating)
            sceneViewFlags |= ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Scene View", nullptr, sceneViewFlags);
        ImGui::PopStyleVar();

        RendererSystem *renderer = GetGlobalSystem<RendererSystem>();
        Image *displayRT = renderer->GetDisplayRT();

        auto recreateSceneTexture = [this, renderer, displayRT]()
        {
            if (!displayRT)
                return;

            Image::Destroy(m_sceneViewImage);
            m_sceneViewImage = renderer->CreateFSSampledImage(false);

            if (m_viewportTextureId)
            {
                ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)m_viewportTextureId);
                m_viewportTextureId = nullptr;
            }
        };

        if (!m_sceneViewImage || !displayRT ||
            m_sceneViewImage->GetWidth() != displayRT->GetWidth() ||
            m_sceneViewImage->GetHeight() != displayRT->GetHeight())
        {
            recreateSceneTexture();
        }

        Image *sceneTexture = m_sceneViewImage ? m_sceneViewImage : displayRT;

        // Ensure we have a valid render target with sampler and SRV
        if (!sceneTexture || !sceneTexture->HasSRV() || !sceneTexture->GetSampler())
        {
            ImGui::TextDisabled("Initializing viewport...");
            ImGui::End();
            return;
        }

        // Create descriptor set for the render target if not already created
        if (!m_viewportTextureId)
        {
            VkSampler sampler = sceneTexture->GetSampler()->ApiHandle();
            VkImageView imageView = sceneTexture->GetSRV();

            if (sampler && imageView)
            {
                m_viewportTextureId = (void *)ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }

        if (m_viewportTextureId)
        {
            // Get available content region
            ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();

            if (viewportPanelSize.x > 0 && viewportPanelSize.y > 0)
            {
                // Calculate aspect-preserving size
                float targetAspect = (float)sceneTexture->GetWidth() / (float)sceneTexture->GetHeight();
                float panelAspect = viewportPanelSize.x / viewportPanelSize.y;

                ImVec2 imageSize;
                if (panelAspect > targetAspect)
                {
                    // Panel is wider, fit to height
                    imageSize.y = viewportPanelSize.y;
                    imageSize.x = imageSize.y * targetAspect;
                }
                else
                {
                    // Panel is taller, fit to width
                    imageSize.x = viewportPanelSize.x;
                    imageSize.y = imageSize.x / targetAspect;
                }

                // Center the image
                ImVec2 cursorPos = ImGui::GetCursorPos();
                cursorPos.x += (viewportPanelSize.x - imageSize.x) * 0.5f;
                cursorPos.y += (viewportPanelSize.y - imageSize.y) * 0.5f;
                ImGui::SetCursorPos(cursorPos);

                ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_viewportTextureId)), imageSize);
            }
        }
        else
        {
            ImGui::TextDisabled("Rendering...");
        }

        ImGui::End();
    }

    void GUI::Update()
    {
        if (!m_render)
            return;

        Menu();
        BuildDockspace();

        if (m_show_demo_window)
            ImGui::ShowDemoWindow(&m_show_demo_window);

        Loading();
        SceneView();
        Metrics();
        Models();
        Scripts();
        AssetViewer();
        Properties();
        Shaders();
    }
} // namespace pe
