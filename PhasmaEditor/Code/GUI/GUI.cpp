#include "GUI.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/RenderPass.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "GUIState.h"
#include "Helpers.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/SuperResolutionPass.h"
#include "Scene/Model.h"
#include "Systems/RendererSystem.h"
#include "TinyFileDialogs/tinyfiledialogs.h"
#include "Widgets/AssetViewer.h"
#include "Widgets/FileBrowser.h"
#include "Widgets/Loading.h"
#include "Widgets/Metrics.h"
#include "Widgets/Models.h"
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
          m_requestDockReset(false),
          m_sceneObjectsOpen(true)
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

    void GUI::async_fileDialog_ImGuiMenuItem(const char *menuLabel, const char *dialogTitle,
                                             const std::vector<const char *> &filter)
    {
        if (ImGui::MenuItem(menuLabel))
        {
            if (GUIState::s_modelLoading)
                return;

            auto lambda = [dialogTitle, filter]()
            {
                const char *result = tinyfd_openFileDialog(dialogTitle, "", static_cast<int>(filter.size()), filter.data(), "", 0);
                if (result)
                {
                    auto loadAsync = [result]()
                    {
                        GUIState::s_modelLoading = true;

                        std::filesystem::path path(result);
                        Model::Load(path);

                        GUIState::s_modelLoading = false;
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
        ImGui::DockBuilderDockWindow("File Browser", dockBottom);

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
        io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;          // Enable SRGB support

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

        auto properties = std::make_shared<Properties>();
        auto metrics = std::make_shared<Metrics>();
        auto models = std::make_shared<Models>();
        auto assetViewer = std::make_shared<AssetViewer>();
        auto sceneView = std::make_shared<SceneView>();
        auto loading = std::make_shared<Loading>();
        auto fileBrowser = std::make_shared<FileBrowser>();

        m_widgets = {properties, metrics, models, assetViewer, sceneView, loading, fileBrowser};

        // Populate Menu Vectors
        m_menuWindowWidgets = {metrics, properties, models, assetViewer, sceneView, fileBrowser};
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
        BuildDockspace();

        if (m_show_demo_window)
            ImGui::ShowDemoWindow(&m_show_demo_window);

        for (auto &widget : m_widgets)
        {
            if (widget->IsOpen())
                widget->Update();
        }
    }
} // namespace pe
