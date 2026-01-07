#include "GUI.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/RenderPass.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "Base/Log.h"
#include "GUIState.h"
#include "Helpers.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/SuperResolutionPass.h"
#include "Scene/Model.h"
#include "Systems/RendererSystem.h"
#include "Widgets/AssetViewer.h"
#include "Widgets/Console.h"
#include "Widgets/FileBrowser.h"
#include "Widgets/FileSelector.h"
#include "Widgets/Hierarchy.h"
#include "Widgets/Loading.h"
#include "Widgets/Metrics.h"
#include "Widgets/Models.h"
#include "Widgets/Particles.h"
#include "Widgets/Properties.h"
#include "Widgets/SceneView.h"
#include "imgui/imgui_impl_sdl2.h"
#include "imgui/imgui_impl_vulkan.h"


namespace pe
{
    GUI::GUI()
        : m_render(true),
          m_attachment(std::make_unique<Attachment>()),
          m_show_demo_window(false),
          m_dockspaceId(0),
          m_dockspaceInitialized(false),
          m_requestDockReset(false)
    {
    }

    GUI::~GUI()
    {
        if (GUIState::s_viewportTextureId)
        {
            ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)GUIState::s_viewportTextureId);
            GUIState::s_viewportTextureId = nullptr;
        }

        Image::Destroy(GUIState::s_sceneViewImage);
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }

    static std::atomic_bool s_modelLoading = false;

    void GUI::ShowLoadModelMenuItem()
    {
        if (ImGui::MenuItem("Load...", "Choose Model"))
        {
            if (GUIState::s_modelLoading)
                return;

            auto *fs = GetWidget<FileSelector>();
            if (fs)
            {
                std::vector<std::string> exts;
                for (const char *ext : FileBrowser::s_modelExtensionsVec)
                    exts.push_back(ext);

                fs->OpenSelection([](const std::string &path)
                                  {
                    auto loadAsync = [path]()
                    {
                        GUIState::s_modelLoading = true;
                        Model::Load(path);
                        GUIState::s_modelLoading = false;
                    };
                    ThreadPool::GUI.Enqueue(loadAsync); }, exts);
            }
        }
    }

    void GUI::ShowExitMenuItem()
    {
        if (ImGui::MenuItem("Exit", "Exit"))
        {
            m_showExitConfirmation = true;
        }
    }

    void GUI::DrawExitPopup()
    {
        if (m_showExitConfirmation)
        {
            ImGui::OpenPopup("Exit Confirmation");
            m_showExitConfirmation = false;
        }

        // Center the modal
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Exit Confirmation", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Are you sure you want to exit?\n\n");
            ImGui::Separator();

            if (ImGui::Button("OK", ImVec2(120, 0)))
            {
                EventSystem::PushEvent(EventType::Quit);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
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

        ImGuiID dockMainId = dockspace;
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Right, dockRightFrac, nullptr, &dockMainId);
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Left, dockLeftFrac, nullptr, &dockMainId);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMainId, ImGuiDir_Down, dockBottomFrac, nullptr, &dockMainId);

        // Central node is now dockMainId - dock the Scene View there
        ImGui::DockBuilderDockWindow("Scene View", dockMainId);

        // Left - Metrics, Models, Hierarchy
        ImGui::DockBuilderDockWindow("Metrics", dockLeft);
        ImGui::DockBuilderDockWindow("Models", dockLeft);
        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);

        // Right - Global Properties
        ImGui::DockBuilderDockWindow("Global Properties", dockRight);

        // Bottom - Console, Asset Viewer, File Browser (Tabbed)
        // Console first to ensure leftmost tab position
        ImGui::DockBuilderDockWindow("Console", dockBottom);
        ImGui::DockBuilderDockWindow("Asset Viewer", dockBottom);
        ImGui::DockBuilderDockWindow("File Browser", dockBottom);

        ImGui::DockBuilderFinish(dockspace);

        // Make Console the active tab
        ImGui::SetWindowFocus("Console");
    }

    void GUI::Menu()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                ShowLoadModelMenuItem();
                ImGui::Separator();
                ShowExitMenuItem();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                ImGui::MenuItem("Undo", "Ctrl+Z", false, false);
                ImGui::MenuItem("Redo", "Ctrl+Y", false, false);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Assets"))
            {
                if (ImGui::MenuItem("Recompile Shaders", "Ctrl+Shift+R"))
                    EventSystem::PushEvent(EventType::CompileShaders);

                for (auto &widget : m_menuAssetsWidgets)
                    ImGui::MenuItem(widget->GetName().c_str(), nullptr, widget->GetOpen());

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Window"))
            {
                for (auto &widget : m_menuWindowWidgets)
                    ImGui::MenuItem(widget->GetName().c_str(), nullptr, widget->GetOpen());

                if (ImGui::BeginMenu("Viewport"))
                {
                    if (ImGui::MenuItem("Floating", nullptr, &GUIState::s_sceneViewFloating))
                    {
                        if (GUIState::s_sceneViewFloating)
                        {
                            // Reset when becoming floating
                            GUIState::s_sceneViewFloating = true;
                        }
                        else
                        {
                            GUIState::s_sceneViewRedockQueued = true;
                        }
                    }
                    if (ImGui::MenuItem("Redock", nullptr, false, GUIState::s_sceneViewFloating))
                    {
                        GUIState::s_sceneViewFloating = false;
                        GUIState::s_sceneViewRedockQueued = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Layout"))
            {
                if (ImGui::MenuItem("Reset to Default Layout", "Ctrl+Shift+L"))
                    m_requestDockReset = true;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                ImGui::MenuItem("Dear ImGui Demo", nullptr, &m_show_demo_window);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void GUI::Init()
    {
        auto &gSettings = Settings::Get<GlobalSettings>();

        gSettings.model_list.clear();

        auto Deduplicate = [](auto &vec)
        {
            std::sort(vec.begin(), vec.end());
            vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
        };

        const std::filesystem::path modelsDir = std::filesystem::path(Path::Assets + "Objects");
        if (std::filesystem::exists(modelsDir))
        {
            for (auto &file : std::filesystem::recursive_directory_iterator(modelsDir))
            {
                if (FileBrowser::IsModelFile(file.path()))
                    gSettings.model_list.push_back(std::filesystem::relative(file.path(), modelsDir).generic_string());
            }
        }
        Deduplicate(gSettings.model_list);

        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // Enable docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable multiple viewports
        io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;          // Enable SRGB support

        ImGui::StyleColorsClassic();

        ui::ApplyNiceTheme();

        ImGui_ImplSDL2_InitForVulkan(RHII.GetWindow());

        // Verify the SDL2 backend supports platform windows
        PE_ERROR_IF(!(io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports),
                    "SDL2 backend doesn't support platform viewports!");

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

        auto AddGpuTimerInfo = [this](const std::any &data)
        {
            try
            {
                const auto &commandTimerInfos = std::any_cast<const std::vector<GpuTimerSample> &>(data);
                if (commandTimerInfos.empty())
                    return;

                std::lock_guard<std::mutex> lock(m_timerMutex);
                m_gpuTimerInfos.insert(m_gpuTimerInfos.end(), commandTimerInfos.begin(), commandTimerInfos.end());
            }
            catch (const std::bad_any_cast &ex)
            {
                PE_ERROR(std::string("Bad any cast in GUI::Init()::AddGpuTimerInfo: " + std::string(ex.what())).c_str());
                (void)ex;
            }
        };

        EventSystem::RegisterCallback(EventType::AfterCommandWait, std::move(AddGpuTimerInfo));

        auto properties = std::make_shared<Properties>();
        auto metrics = std::make_shared<Metrics>();
        auto models = std::make_shared<Models>();
        auto assetViewer = std::make_shared<AssetViewer>();
        auto sceneView = std::make_shared<SceneView>();
        auto loading = std::make_shared<Loading>();
        auto fileBrowser = std::make_shared<FileBrowser>();
        auto fileSelector = std::make_shared<FileSelector>(); // Separate instance for popups
        auto hierarchy = std::make_shared<Hierarchy>();
        auto particles = std::make_shared<Particles>();
        auto console = std::make_shared<Console>();

        // Console added early to potentially influence tab ordering (Leftmost)
        m_widgets = {console, properties, metrics, models, assetViewer, sceneView, loading, fileBrowser, fileSelector, hierarchy, particles};

        // Initialize Core Logging and attach Console
        Log::Attach([console](const std::string &msg, LogType type)
                    { console->AddLog(type, "%s", msg.c_str()); });

        // Populate Menu Vectors
        m_menuWindowWidgets = {console, metrics, properties, models, assetViewer, sceneView, fileBrowser, hierarchy, particles};
        m_menuAssetsWidgets = {};
        for (auto &widget : m_widgets)
            widget->Init(this);

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
            GUIState::s_sceneViewImage && displayRT &&
            GUIState::s_sceneViewImage->GetWidth() == displayRT->GetWidth() &&
            GUIState::s_sceneViewImage->GetHeight() == displayRT->GetHeight();

        if (canCopySceneView)
        {
            cmd->CopyImage(displayRT, GUIState::s_sceneViewImage);

            ImageBarrierInfo sceneViewBarrier{};
            sceneViewBarrier.image = GUIState::s_sceneViewImage;
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

    void GUI::Update()
    {
        if (!m_render)
            return;

        Menu();
        DrawExitPopup();
        BuildDockspace();

        if (m_show_demo_window)
            ImGui::ShowDemoWindow(&m_show_demo_window);

        for (auto &widget : m_widgets)
        {
            if (widget->IsOpen())
                widget->Update();
        }
    }

    std::vector<GpuTimerSample> GUI::PopGpuTimerInfos()
    {
        std::lock_guard<std::mutex> lock(m_timerMutex);
        if (m_gpuTimerInfos.empty())
            return {};

        std::vector<GpuTimerSample> timers;
        timers.swap(m_gpuTimerInfos);
        return timers;
    }
} // namespace pe
