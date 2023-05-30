#include "GUI.h"
#include "TinyFileDialogs/tinyfiledialogs.h"
#include "Console/Console.h"
#include "Renderer/Vertex.h"
#include "Shader/Shader.h"
#include "Renderer/RHI.h"
#include "Model/Model.h"
#include "Systems/CameraSystem.h"
#include "Window/Window.h"
#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/Image.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Renderer.h"
#include "Renderer/Queue.h"
#include "Systems/RendererSystem.h"
#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_impl_sdl.h"
#include "imgui/imgui_internal.h"

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

    bool endsWithExt(const std::string &mainStr, const std::string &toMatch)
    {
        return mainStr.size() >= toMatch.size() &&
               mainStr.compare(mainStr.size() - toMatch.size(), toMatch.size(), toMatch) == 0;
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

    void GUI::UpdateWindows()
    {
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        Menu();
        Loading();
        Metrics();
        Properties();
        Shaders();
        Models();
        ConsoleWindow();
        Scripts();
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

                        GUI::modelList.push_back(path.filename().string());
                        GUI::model_scale.push_back({1.f, 1.f, 1.f});
                        GUI::model_pos.push_back({0.f, 0.f, 0.f});
                        GUI::model_rot.push_back({0.f, 0.f, 0.f});

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
    static bool console_open = false;
    static bool scripts_open = false;

    void GUI::Menu() const
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
                ImGui::Checkbox("Console", &console_open);
                ImGui::Checkbox("Scripts", &scripts_open);

                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void GUI::Loading() const
    {
        static const ImVec4 color{0, 232.f / 256, 224.f / 256, 1.f};
        static const ImVec4 bdcolor{0.f, 168.f / 256, 162.f / 256, 1.f};
        static const float radius = 25.f;
        static const int flags = ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_NoBackground;

        if (s_modelLoading)
        {
            int x, y;
            const float topBarHeight = 16.f;
            SDL_GetWindowPosition(RHII.GetWindow(), &x, &y);
            ImVec2 size(-0.001f, 25.f);
            ImGui::SetNextWindowPos(ImVec2(x + WIDTH_f / 2 - 200.f, y + topBarHeight + HEIGHT_f / 2 - size.y / 2));
            ImGui::SetNextWindowSize(ImVec2(400.f, 25.f));
            ImGui::Begin("Loading", &metrics_open, flags);
            // LoadingIndicatorCircle("Loading", radius, color, bdcolor, 10, 4.5f);
            const float progress = loadingCurrent / static_cast<float>(loadingTotal);
            ImGui::ProgressBar(progress, size);
            ImGui::End();
        }
    }

    void GUI::Metrics() const
    {
        if (!metrics_open)
            return;

        ImGui::Begin("Metrics", &metrics_open);
        ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
        auto framerate = ImGui::GetIO().Framerate;
        ImGui::Text("Average %.3f ms (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::DragInt("FPS", &fps, 0.1f);
        fps = max(fps, 10);
        ImGui::Separator();
        ImGui::Separator();

        FrameTimer &frameTimer = FrameTimer::Instance();

        double shadowsTotal = 0.0;
        if (shadow_cast)
        {
            for (int i = 0; i < SHADOWMAP_CASCADES; i++)
                shadowsTotal += frameTimer.shadowStamp[i];
        }

        ImGui::Text("CPU Total: %.3f ms", static_cast<float>(MILLI(frameTimer.cpuTotal)));
        ImGui::Indent(16.0f);
        ImGui::Text("CPU Updates: %.3f ms", static_cast<float>(MILLI(frameTimer.updatesStamp)));
        ImGui::Text("CPU Draw: %.3f ms", static_cast<float>(MILLI(frameTimer.cpuTotal - frameTimer.updatesStamp)));
        ImGui::Unindent(16.0f);
        ImGui::Text("GPU Total: %.3f ms", frameTimer.gpuStamp + shadowsTotal);
        ImGui::Indent(16.0f);
        if (shadow_cast)
        {
            for (int i = 0; i < SHADOWMAP_CASCADES; i++)
            {
                ImGui::Text("ShadowPass%i: %.3f ms", i, frameTimer.shadowStamp[i]);
            }
        }
        ImGui::Text("GBuffer: %.3f ms", frameTimer.geometryStamp);
        ImGui::Text("Composition: %.3f ms", frameTimer.compositionStamp);
        if (show_ssao)
        {
            ImGui::Text("SSAO: %.3f ms", frameTimer.ssaoStamp);
        }
        if (show_ssr)
        {
            ImGui::Text("SSR: %.3f ms", frameTimer.ssrStamp);
        }
        ImGui::Text("Color Pass: %.3f ms", frameTimer.compositionStamp);

        if (use_FSR2)
        {
            ImGui::Text("FSR2: %.3f ms", frameTimer.fsrStamp);
        }
        if (use_FXAA)
        {
            ImGui::Text("FXAA: %.3f ms", frameTimer.fxaaStamp);
        }
        if (show_Bloom)
        {
            ImGui::Text("Bloom: %.3f ms", frameTimer.bloomStamp);
        }
        if (use_DOF)
        {
            ImGui::Text("Depth of Field: %.3f ms", frameTimer.dofStamp);
        }
        if (show_motionBlur)
        {
            ImGui::Text("Motion Blur: %.3f ms", frameTimer.motionBlurStamp);
        }
        if (drawAABBs)
        {
            ImGui::Text("AABBs: %.3f ms", frameTimer.AABBsStamp);
        }

        ImGui::Text("GUI: %.3f ms", frameTimer.guiStamp);
        ImGui::Unindent(16.0f);

        ImGui::Separator();
        ImGui::Separator();
        ImGui::Separator();
        ImGui::Text("Render Target");
        static const char *current_item = "viewport";
        std::vector<const char *> items(s_renderImages.size());
        for (int i = 0; i < items.size(); i++)
            items[i] = s_renderImages[i]->imageInfo.name.c_str();

        if (ImGui::BeginCombo("##combo", current_item))
        {
            for (int n = 0; n < items.size(); n++)
            {
                bool is_selected = (current_item == items[n]);
                if (ImGui::Selectable(items[n], is_selected))
                {
                    current_item = items[n];
                    s_currRenderImage = s_renderImages[n];
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::End();
    }

    void GUI::ConsoleWindow()
    {
        static Console console;

        if (!console_open)
            return;

        console.Draw("Console", &console_open);
    }

    void GUI::Scripts() const
    {
        // if (!scripts_open)
        //	return;

        // ImGui::Begin("Scripts Folder", &scripts_open);
        //
        // const char* result = async_inputBox_ImGuiButton("Create New Script", "Script", "Give a name followed by the extension .cs");
        // if (result)
        //{
        //	std::string res = result;
        //	if (std::find(fileList.begin(), fileList.end(), res) == fileList.end())
        //	{
        //		const std::string cmd = "type nul > " + Path::Assets + "Scripts\\" + res;
        //		system(cmd.c_str());
        //		fileList.push_back(res);
        //	}
        //	else
        //	{
        //		SDL_ShowSimpleMessageBox(
        //				SDL_MESSAGEBOX_INFORMATION, "Script not created", "Script name already exists", g_Window
        //		);
        //	}
        // }
        //
        // for (uint32_t i = 0; i < fileList.size(); i++)
        //{
        //	std::string name = fileList[i] + "##" + std::to_string(i);
        //	if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
        //	{
        //		if (ImGui::IsMouseDoubleClicked(0))
        //		{
        //			std::string s = Path::Assets + "Scripts\\" + fileList[i];
        //			system(s.c_str());
        //		}
        //	}
        // }
        // ImGui::End();
    }

    void GUI::Shaders() const
    {
        if (!shaders_open)
            return;

        ImGui::Begin("Shaders Folder", &shaders_open);
        if (ImGui::Button("Compile Shaders"))
        {
            EventSystem::PushEvent(EventCompileShaders);
        }
        for (uint32_t i = 0; i < shaderList.size(); i++)
        {
            std::string name = shaderList[i] + "##" + std::to_string(i);
            if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
            {
                if (ImGui::IsMouseDoubleClicked(0))
                {
                    std::string s = Path::Assets + "Shaders\\" + shaderList[i];
                    auto lambda = [s]()
                    { system(s.c_str()); };
                    e_GUI_ThreadPool.Enqueue(lambda);
                }
            }
        }
        ImGui::End();
    }

    void GUI::Models() const
    {
        if (!models_open)
            return;

        ImGui::Begin("Models Loaded", &models_open);

        for (uint32_t i = 0; i < modelList.size(); i++)
        {
            std::string s = modelList[i] + "##" + std::to_string(i);
            if (ImGui::Selectable(s.c_str(), false))
                modelItemSelected = i;
        }

        ImGui::End();
    }

    void GUI::Properties() const
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

        static float rtScale = renderTargetsScale;
        ImGui::Begin("Global Properties", &properties_open);
        ImGui::DragFloat("Quality.", &rtScale, 0.01f, 0.05f, 1.0f);
        if (ImGui::Button("Apply"))
        {
            renderTargetsScale = clamp(rtScale, 0.1f, 4.0f);
            RHII.WaitDeviceIdle();
            EventSystem::PushEvent(EventResize);
        }
        // ImGui::Checkbox("Lock Render Window", &lock_render_window);
        ImGui::Checkbox("IBL", &use_IBL);
        ImGui::Checkbox("SSR", &show_ssr);
        ImGui::Checkbox("SSAO", &show_ssao);
        ImGui::Checkbox("Depth of Field", &use_DOF);
        if (use_DOF)
        {
            ImGui::Indent(16.0f);
            ImGui::DragFloat("Scale##DOF", &DOF_focus_scale, 0.05f, 0.5f);
            ImGui::DragFloat("Range##DOF", &DOF_blur_range, 0.05f, 0.5f);
            ImGui::Unindent(16.0f);
            ImGui::Separator();
            ImGui::Separator();
        }
        ImGui::Checkbox("Motion Blur", &show_motionBlur);
        if (show_motionBlur)
        {
            ImGui::Indent(16.0f);
            ImGui::DragFloat("Strength#mb", &motionBlur_strength, 0.05f, 0.2f);
            ImGui::Unindent(16.0f);
            ImGui::Separator();
            ImGui::Separator();
        }
        ImGui::Checkbox("Tone Mapping", &show_tonemapping);
        // ImGui::Checkbox("Compute shaders", &use_compute);
        if (show_tonemapping)
        {
            ImGui::Indent(16.0f);
            ImGui::SliderFloat("Exposure", &exposure, 0.01f, 10.f);
            ImGui::Unindent(16.0f);
            ImGui::Separator();
            ImGui::Separator();
        }

        ImGui::Checkbox("FSR2", &use_FSR2);
        if (use_FSR2)
        {
            ImGui::InputFloat("MotionX", &FSR2_MotionScaleX, 0.1f, 1.f);
            ImGui::InputFloat("MotionY", &FSR2_MotionScaleY, 0.1f, 1.f);
            ImGui::InputFloat("ProjX", &FSR2_ProjScaleX, 0.1f, 1.f);
            ImGui::InputFloat("ProjY", &FSR2_ProjScaleY, 0.1f, 1.f);
        }

        ImGui::Checkbox("FXAA", &use_FXAA);

        ImGui::Checkbox("Bloom", &show_Bloom);
        if (show_Bloom)
        {
            ImGui::Indent(16.0f);
            ImGui::SliderFloat("Inv Brightness", &Bloom_Inv_brightness, 0.01f, 50.f);
            ImGui::SliderFloat("Intensity", &Bloom_intensity, 0.01f, 10.f);
            ImGui::SliderFloat("Range", &Bloom_range, 0.1f, 20.f);
            ImGui::Checkbox("Bloom Tone Mapping", &use_tonemap);
            if (use_tonemap)
                ImGui::SliderFloat("Bloom Exposure", &Bloom_exposure, 0.01f, 10.f);
            ImGui::Unindent(16.0f);
            ImGui::Separator();
            ImGui::Separator();
        }
        ImGui::Checkbox("Fog", &use_fog);
        if (use_fog)
        {
            ImGui::Indent(16.0f);
            ImGui::DragFloat("Ground Thickness", &fog_ground_thickness, 0.1f, 1.0f);
            ImGui::DragFloat("Global Thickness", &fog_global_thickness, 0.1f, 1.0f);
            ImGui::DragFloat("Max Height", &fog_max_height, 0.01f, 0.1f);
            ImGui::Checkbox("Volumetric", &use_Volumetric_lights);
            if (use_Volumetric_lights)
            {
                ImGui::Indent(16.0f);
                ImGui::InputInt("Iterations", &volumetric_steps, 1, 3);
                ImGui::InputInt("Dither", &volumetric_dither_strength, 1, 10);
                ImGui::Unindent(16.0f);
            }
            ImGui::Unindent(16.0f);
        }
        ImGui::Checkbox("Sun Light", &shadow_cast);
        if (shadow_cast)
        {
            ImGui::Indent(16.0f);
            ImGui::SliderFloat("Intst", &sun_intensity, 0.1f, 50.f);
            ImGui::DragFloat3("Dir", sun_direction.data(), 0.01f, -1.f, 1.f);
            ImGui::DragFloat("Slope", &depthBias[2], 0.15f, 0.5f);
            ImGui::Separator();
            ImGui::Separator();
            {
                vec3 direction = normalize(make_vec3(&sun_direction[0]));
                sun_direction[0] = direction.x;
                sun_direction[1] = direction.y;
                sun_direction[2] = direction.z;
            }
            ImGui::Unindent(16.0f);
        }
        ImGui::DragFloat("CamSpeed", &cameraSpeed, 0.1f, 1.f);
        ImGui::DragFloat("TimeScale", &timeScale, 0.05f, 0.2f);
        ImGui::Separator();
        ImGui::Separator();
        if (ImGui::Button("Randomize Lights"))
            randomize_lights = true;
        ImGui::SliderFloat("Light Intst", &lights_intensity, 0.01f, 30.f);
        ImGui::SliderFloat("Light Rng", &lights_range, 0.1f, 30.f);
        ImGui::Checkbox("FreezeCamCull", &freezeFrustumCulling);
        ImGui::Checkbox("Draw AABBs", &drawAABBs);

        // Model properties
        ImGui::Separator();
        ImGui::Separator();
        ImGui::Separator();
        ImGui::LabelText("", "Model Properties");
        if (modelItemSelected > -1)
        {
            const std::string toStr = std::to_string(modelItemSelected);
            const std::string id = " ID[" + toStr + "]";
            const std::string fmt = modelList[modelItemSelected] + id;
            ImGui::TextColored(ImVec4(.6f, 1.f, .5f, 1.f), "%s", fmt.c_str());

            ImGui::Separator();
            const std::string s = "Scale##" + toStr;
            const std::string p = "Position##" + toStr;
            const std::string r = "Rotation##" + toStr;
            ImGui::DragFloat3(s.c_str(), model_scale[modelItemSelected].data(), 0.005f);
            ImGui::DragFloat3(p.c_str(), model_pos[modelItemSelected].data(), 0.005f);
            ImGui::DragFloat3(r.c_str(), model_rot[modelItemSelected].data(), 0.005f);

            ImGui::Separator();
            if (ImGui::Button("Unload Model"))
            {
                RHII.WaitDeviceIdle();
                Model::models[modelItemSelected].Destroy();
                Model::models.erase(Model::models.begin() + modelItemSelected);
                GUI::modelList.erase(GUI::modelList.begin() + modelItemSelected);
                GUI::model_scale.erase(GUI::model_scale.begin() + modelItemSelected);
                GUI::model_pos.erase(GUI::model_pos.begin() + modelItemSelected);
                GUI::model_rot.erase(GUI::model_rot.begin() + modelItemSelected);
                GUI::modelItemSelected = -1;
            }
        }
        ImGui::End();
    }

    void GUI::InitImGui()
    {
        std::string directory = Path::Assets + "Scripts";
        for (auto &file : std::filesystem::recursive_directory_iterator(directory))
        {
            auto pathStr = file.path().string();
            if (endsWithExt(pathStr, ".cs"))
                fileList.push_back(pathStr.erase(0, pathStr.find(directory) + directory.size() + 1));
        }

        directory = Path::Assets + "Shaders";
        for (auto &file : std::filesystem::recursive_directory_iterator(directory))
        {
            auto pathStr = file.path().string();
            if (endsWithExt(pathStr, ".vert") || endsWithExt(pathStr, ".frag") || endsWithExt(pathStr, ".comp") ||
                endsWithExt(pathStr, ".glsl"))
            {
                shaderList.push_back(pathStr.erase(0, pathStr.find(directory) + directory.size() + 1));
            }
        }

        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;          // Enable docking
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;        // Enable multiple viewports
        io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports; // Backend Renderer supports multiple viewports.

        SetWindowStyle();

        ImGui_ImplSDL2_InitForVulkan(RHII.GetWindow());

        Queue *queue = Queue::GetNext(QueueType::GraphicsBit | QueueType::TransferBit, 1);
        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.Instance = RHII.GetInstance();
        initInfo.PhysicalDevice = RHII.GetGpu();
        initInfo.Device = RHII.GetDevice();
        initInfo.QueueFamily = queue->GetFamilyId();
        initInfo.Queue = queue->Handle();
        initInfo.PipelineCache = nullptr;
        initInfo.DescriptorPool = RHII.GetDescriptorPool()->Handle();
        initInfo.Subpass = 0;
        initInfo.MinImageCount = static_cast<uint32_t>(RHII.GetSwapchain()->images.size());
        initInfo.ImageCount = static_cast<uint32_t>(RHII.GetSwapchain()->images.size());
        initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.Allocator = nullptr;
        initInfo.CheckVkResultFn = nullptr;

        m_displayRT = CONTEXT->GetSystem<RendererSystem>()->GetRenderTarget("display");

        AttachmentInfo attachmentInfo{};
        attachmentInfo.image = m_displayRT;
        attachmentInfo.loadOp = AttachmentLoadOp::Load;
        attachmentInfo.initialLayout = ImageLayout::General;
        attachmentInfo.finalLayout = ImageLayout::ColorAttachment;
        renderPass = CommandBuffer::GetRenderPass(1, &attachmentInfo, nullptr);
        ImGui_ImplVulkan_Init(&initInfo, renderPass->Handle());

        CommandBuffer *cmd = CommandBuffer::GetNext(queue->GetFamilyId());
        cmd->Begin();
        ImGui_ImplVulkan_CreateFontsTexture(cmd->Handle());
        cmd->End();
        queue->Submit(1, &cmd, nullptr, 0, nullptr, 0, nullptr);

        cmd->Wait();
        CommandBuffer::Return(cmd);

        queue->WaitIdle();
    }

    void GUI::InitGUI(bool show)
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

        render = show;
        InitImGui();
    }

    void GUI::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        if (!render)
            return;

        cmd->BeginDebugRegion("GUI");
        if (ImGui::GetDrawData()->TotalVtxCount > 0)
        {
            m_displayRT = CONTEXT->GetSystem<RendererSystem>()->GetRenderTarget("display");

            // Output
            cmd->ImageBarrier(m_displayRT, ImageLayout::General);

            cmd->BeginPass(renderPass, &m_displayRT, nullptr);
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd->Handle());
            cmd->EndPass();
        }
        cmd->EndDebugRegion();
    }

    void GUI::SetWindowStyle(ImGuiStyle *dst)
    {
        ImGui::StyleColorsClassic();
        ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.3f);
    }

    void GUI::CreateRenderPass()
    {
    }

    void GUI::CreateFrameBuffers()
    {
    }

    void GUI::Destroy()
    {
        if (renderPass)
        {
            RenderPass::Destroy(renderPass);
            renderPass = nullptr;
        }

        for (auto &framebuffer : framebuffers)
        {
            if (framebuffer)
            {
                vkDestroyFramebuffer(RHII.GetDevice(), framebuffer, nullptr);
                framebuffer = {};
            }
        }
    }

    void GUI::Update()
    {
        if (!render)
            return;

        ImGui_ImplSDL2_NewFrame();
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();
        UpdateWindows();
        ImGui::Render();
    }

    void GUI::RenderViewPorts()
    {
        if (!render)
            return;

        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}
