/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "PhasmaPch.h"
#include "GUI.h"
#include <filesystem>
#include "TinyFileDialogs/tinyfiledialogs.h"
#include "Core/Queue.h"
#include "Console/Console.h"
#include "Renderer/Vertex.h"
#include "Shader/Shader.h"
#include "Renderer/Vulkan/Vulkan.h"
#include "Core/Path.h"
#include "Systems/EventSystem.h"
#include "ECS/Context.h"
#include "Model/Model.h"
#include "Core/Timer.h"
#include "Systems/CameraSystem.h"
#include "Window/Window.h"
#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/RenderPass.h"
#include "Renderer/CommandBuffer.h"
#include "Renderer/CommandPool.h"
#include "Renderer/Fence.h"
#include "Renderer/Semaphore.h"
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
	}
	
	bool endsWithExt(const std::string& mainStr, const std::string& toMatch)
	{
		return mainStr.size() >= toMatch.size() &&
		       mainStr.compare(mainStr.size() - toMatch.size(), toMatch.size(), toMatch) == 0;
	}

	// https://github.com/ocornut/imgui/issues/1901#issuecomment-444929973
	void LoadingIndicatorCircle(const char* label, const float indicator_radius,
		const ImVec4& main_color, const ImVec4& backdrop_color,
		const int circle_count, const float speed) {
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return;

		ImGuiContext& g = *GImGui;
		const ImGuiID id = window->GetID(label);

		const ImVec2 pos = window->DC.CursorPos;
		const float circle_radius = indicator_radius / 10.f;
		const ImRect bb(pos, ImVec2(pos.x + indicator_radius * 2.0f, pos.y + indicator_radius * 2.0f));
		ImGui::ItemSize(bb, ImGui::GetStyle().FramePadding.y);
		if (!ImGui::ItemAdd(bb, id))
			return;

		const float t = (float)g.Time;
		const auto degree_offset = 2.0f * IM_PI / circle_count;
		for (int i = 0; i < circle_count; ++i) {
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

	static bool s_modelLoading = false;
	void GUI::async_fileDialog_ImGuiMenuItem(const char* menuLabel, const char* dialogTitle, const std::vector<const char*>& filter)
	{
		if (ImGui::MenuItem(menuLabel))
		{
			auto lambda = [dialogTitle, filter]()
			{
				const char* result = tinyfd_openFileDialog(dialogTitle, "", static_cast<int>(filter.size()), filter.data(), "", 0);
				if (result)
				{
					const std::string path(result);
					std::string folderPath = path.substr(0, path.find_last_of('\\') + 1);
					std::string modelName = path.substr(path.find_last_of('\\') + 1);
					Model::models.emplace_back();
					Model& model = Model::models.back();
					auto update = []() { s_modelLoading = true; };
					auto signal = []() { s_modelLoading = false; };
					VULKAN.device->waitIdle();
					auto loadAsync = [&model, folderPath, modelName]() { model.Load(folderPath, modelName); };
					Queue<Launch::AsyncNoWait>::Request(loadAsync, update, signal);
					GUI::modelList.push_back(modelName);
					GUI::model_scale.push_back({ 1.f, 1.f, 1.f });
					GUI::model_pos.push_back({ 0.f, 0.f, 0.f });
					GUI::model_rot.push_back({ 0.f, 0.f, 0.f });
				}
			};

			Queue<Launch::AsyncNoWait>::Request(lambda);
		}
	}
	
	void GUI::async_messageBox_ImGuiMenuItem(const char* menuLabel, const char* messageBoxTitle, const char* message)
	{
		if (ImGui::MenuItem(menuLabel))
		{
			auto lambda = [messageBoxTitle, message]()
			{
				int result = tinyfd_messageBox(messageBoxTitle, message, "yesno", "warning", 0);
				if (result == 1)
				{
					EventSystem* eventSystem = CONTEXT->GetSystem<EventSystem>();
					eventSystem->PushEvent(EventType::Quit);
				}
			};
			Queue<Launch::AsyncNoWait>::Request(lambda);
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
				static std::vector<const char*> filter {"*.gltf", "*.glb"};
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
		static const ImVec4 color{ 0, 232.f / 256, 224.f / 256, 1.f };
		static const ImVec4 bdcolor{ 0.f, 168.f / 256, 162.f / 256, 1.f };
		static const float radius = 25.f;
		static const int flags = ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground;

		if (s_modelLoading)
		{
			ImGui::SetNextWindowPos(ImVec2(WIDTH_f / 2, HEIGHT_f / 2));
			ImGui::Begin("Loading", &metrics_open, flags);
			LoadingIndicatorCircle("Loading", radius, color, bdcolor, 10, 4.5f);
			ImGui::End();
		}
	}
	
	void GUI::Metrics() const
	{
		if (!metrics_open)
			return;

		int totalPasses = 0;
		float totalTime = 0.f;
		
		ImGui::Begin("Metrics", &metrics_open);
		ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
		auto framerate = ImGui::GetIO().Framerate;
		ImGui::Text("Average %.3f ms (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::DragInt("FPS", &fps, 0.1f);
		fps = maximum(fps, 10);
		ImGui::Separator();
		ImGui::Separator();

		const FrameTimer& frameTimer = FrameTimer::Instance();
		ImGui::Text("CPU Total: %.3f ms", static_cast<float>(frameTimer.delta * 1000.0));
		ImGui::Indent(16.0f);
		ImGui::Text("Draw wait %.3f ms", static_cast<float>(MILLI(frameTimer.timestamps[1])));
		ImGui::Text("Updates Total: %.3f ms", static_cast<float>(MILLI(frameTimer.timestamps[0])));
		ImGui::Unindent(16.0f);
		ImGui::Separator();
		ImGui::Text(
			"GPU Total: %.3f ms",
			frameTimer.timestamps[2] +
			(shadow_cast ? frameTimer.timestamps[13] + frameTimer.timestamps[14] + frameTimer.timestamps[15] : 0.f) +
			(use_compute ? frameTimer.timestamps[16] : 0.f)
		);
		ImGui::Separator();
		ImGui::Text("Render Passes:");
		//if (use_compute) {
		//	ImGui::Text("   Compute: %.3f ms", stats[13]); totalPasses++;
		//}
		//ImGui::Text("   Skybox: %.3f ms", stats[1]); totalPasses++;
		ImGui::Indent(16.0f);
		if (shadow_cast)
		{
			for (int i = 0; i < SHADOWMAP_CASCADES; i++)
			{
				int index = 13 + i;
				ImGui::Text("ShadowPass%i: %.3f ms", i, frameTimer.timestamps[index]);
				totalPasses++;
				totalTime += (float)frameTimer.timestamps[index];
			}
		}
		ImGui::Text("GBuffer: %.3f ms", frameTimer.timestamps[4]);
		totalPasses++;
		totalTime += (float)frameTimer.timestamps[4];
		if (show_ssao)
		{
			ImGui::Text("SSAO: %.3f ms", frameTimer.timestamps[5]);
			totalPasses++;
			totalTime += (float)frameTimer.timestamps[5];
		}
		if (show_ssr)
		{
			ImGui::Text("SSR: %.3f ms", frameTimer.timestamps[6]);
			totalPasses++;
			totalTime += (float)frameTimer.timestamps[6];
		}
		ImGui::Text("Lights: %.3f ms", frameTimer.timestamps[7]);
		totalPasses++;
		totalTime += (float)frameTimer.timestamps[7];
		if ((use_FXAA || use_TAA) && use_AntiAliasing)
		{
			ImGui::Text("Anti aliasing: %.3f ms", frameTimer.timestamps[8]);
			totalPasses++;
			totalTime += (float)frameTimer.timestamps[8];
		}
		if (show_Bloom)
		{
			ImGui::Text("Bloom: %.3f ms", frameTimer.timestamps[9]);
			totalPasses++;
			totalTime += (float)frameTimer.timestamps[9];
		}
		if (use_DOF)
		{
			ImGui::Text("Depth of Field: %.3f ms", frameTimer.timestamps[10]);
			totalPasses++;
			totalTime += (float)frameTimer.timestamps[10];
		}
		if (show_motionBlur)
		{
			ImGui::Text("Motion Blur: %.3f ms", frameTimer.timestamps[11]);
			totalPasses++;
			totalTime += (float)frameTimer.timestamps[11];
		}

		ImGui::Text("GUI: %.3f ms", frameTimer.timestamps[12]);
		totalPasses++;
		totalTime += (float)frameTimer.timestamps[12];
		ImGui::Unindent(16.0f);
		ImGui::Separator();
		ImGui::Separator();
		ImGui::Text("Total: %i (%.3f ms)", totalPasses, totalTime);

		ImGui::Separator();
		ImGui::Separator();
		ImGui::Separator();
		ImGui::Text("Render Target");
		static const char* current_item = "viewport";
		std::vector<const char*> items(s_renderImages.size());
		for (int i = 0; i < items.size(); i++)
			items[i] = s_renderImages[i]->name.c_str();

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
		//if (!scripts_open)
		//	return;

		//ImGui::Begin("Scripts Folder", &scripts_open);
		//
		//const char* result = async_inputBox_ImGuiButton("Create New Script", "Script", "Give a name followed by the extension .cs");
		//if (result)
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
		//}
		//
		//for (uint32_t i = 0; i < fileList.size(); i++)
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
		//}
		//ImGui::End();
	}
	
	void GUI::Shaders() const
	{
		if (!shaders_open)
			return;

		ImGui::Begin("Shaders Folder", &shaders_open);
		if (ImGui::Button("Compile Shaders"))
		{
			VULKAN.device->waitIdle();
			CONTEXT->GetSystem<EventSystem>()->PushEvent(EventType::CompileShaders);
		}
		for (uint32_t i = 0; i < shaderList.size(); i++)
		{
			std::string name = shaderList[i] + "##" + std::to_string(i);
			if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
			{
				if (ImGui::IsMouseDoubleClicked(0))
				{
					std::string s = Path::Assets + "Shaders\\" + shaderList[i];
					system(s.c_str());
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

		static float rtScale = renderTargetsScale;
		ImGui::Begin("Global Properties", &properties_open);
		ImGui::DragFloat("Quality.", &rtScale, 0.01f, 0.05f);
		if (ImGui::Button("Apply"))
		{
			renderTargetsScale = clamp(rtScale, 0.1f, 4.0f);
			VULKAN.device->waitIdle();
			CONTEXT->GetSystem<EventSystem>()->PushEvent(EventType::ScaleRenderTargets);
		}
		//ImGui::Checkbox("Lock Render Window", &lock_render_window);
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
		//ImGui::Checkbox("Compute shaders", &use_compute);
		if (show_tonemapping)
		{
			ImGui::Indent(16.0f);
			ImGui::SliderFloat("Exposure", &exposure, 0.01f, 10.f);
			ImGui::Unindent(16.0f);
			ImGui::Separator();
			ImGui::Separator();
		}
		ImGui::Checkbox("Anti aliasing", &use_AntiAliasing);
		if (use_AntiAliasing)
		{
			ImGui::Indent(16.0f);
			if (ImGui::Checkbox("FXAA", &use_FXAA))
			{
				use_TAA = false;
			}
			if (ImGui::Checkbox("TAA", &use_TAA))
			{
				use_FXAA = false;
			}
			if (use_TAA)
			{
				ImGui::Indent(16.0f);
				ImGui::DragFloat("Jitter", &TAA_jitter_scale, 0.01f, 0.1f);
				ImGui::DragFloat("FeedbackMin", &TAA_feedback_min, 0.005f, 0.05f);
				ImGui::DragFloat("FeedbackMax", &TAA_feedback_max, 0.005f, 0.05f);
				ImGui::DragFloat("Strength", &TAA_sharp_strength, 0.1f, 0.2f);
				ImGui::DragFloat("Clamp", &TAA_sharp_clamp, 0.01f, 0.05f);
				ImGui::DragFloat("Bias", &TAA_sharp_offset_bias, 0.1f, 0.3f);
				ImGui::Unindent(16.0f);
			}
			ImGui::Unindent(16.0f);
			ImGui::Separator();
			ImGui::Separator();
		}
		else
		{
			use_TAA = false;
			use_FXAA = false;
		}
		
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
				vec3 direction = normalize(vec3(&sun_direction[0]));
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
			if (ImGui::Button("Unload Model"))
			{
				VULKAN.device->waitIdle();
				Model::models[modelItemSelected].destroy();
				Model::models.erase(Model::models.begin() + modelItemSelected);
				GUI::modelList.erase(GUI::modelList.begin() + modelItemSelected);
				GUI::model_scale.erase(GUI::model_scale.begin() + modelItemSelected);
				GUI::model_pos.erase(GUI::model_pos.begin() + modelItemSelected);
				GUI::model_rot.erase(GUI::model_rot.begin() + modelItemSelected);
				GUI::modelItemSelected = -1;
			}
			
			ImGui::Separator();
			const std::string s = "Scale##" + toStr;
			const std::string p = "Position##" + toStr;
			const std::string r = "Rotation##" + toStr;
			ImGui::DragFloat3(s.c_str(), model_scale[modelItemSelected].data());
			ImGui::DragFloat3(p.c_str(), model_pos[modelItemSelected].data());
			ImGui::DragFloat3(r.c_str(), model_rot[modelItemSelected].data());
		}
		ImGui::End();
	}
	
	void GUI::InitImGui()
	{		
		std::string directory = Path::Assets + "Scripts";
		for (auto& file : std::filesystem::recursive_directory_iterator(directory))
		{
			auto pathStr = file.path().string();
			if (endsWithExt(pathStr, ".cs"))
				fileList.push_back(pathStr.erase(0, pathStr.find(directory) + directory.size() + 1));
		}
		
		directory = Path::Assets + "Shaders";
		for (auto& file : std::filesystem::recursive_directory_iterator(directory))
		{
			auto pathStr = file.path().string();
			if (endsWithExt(pathStr, ".vert") || endsWithExt(pathStr, ".frag") || endsWithExt(pathStr, ".comp") ||
			    endsWithExt(pathStr, ".glsl"))
			{
				shaderList.push_back(pathStr.erase(0, pathStr.find(directory) + directory.size() + 1));
			}
		}
		
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		(void) io;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;			// Enable docking
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;			// Enable multiple viewports
		io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;	// Backend Renderer supports multiple viewports.

		SetWindowStyle();

		ImGui_ImplSDL2_InitForVulkan(VULKAN.window);

		ImGui_ImplVulkan_InitInfo info;
		info.Instance = *VULKAN.instance;
		info.PhysicalDevice = *VULKAN.gpu;
		info.Device = *VULKAN.device;
		info.QueueFamily = VULKAN.graphicsFamilyId;
		info.Queue = *VULKAN.graphicsQueue;
		info.PipelineCache = nullptr; // Will it help to use it?
		info.DescriptorPool = *VULKAN.descriptorPool;
		info.Subpass = 0;
		info.MinImageCount = VULKAN.surface.capabilities->minImageCount;
		info.ImageCount = (uint32_t)VULKAN.swapchain.images.size();
		info.MSAASamples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
		info.Allocator = nullptr;
		info.CheckVkResultFn = nullptr;
		ImGui_ImplVulkan_Init(&info, *renderPass.handle);

		CommandBuffer cmd = VkCommandBuffer((*VULKAN.dynamicCmdBuffers)[0]);
		cmd.Begin();
		ImGui_ImplVulkan_CreateFontsTexture(cmd.Handle());
		cmd.End();
		VULKAN.submitAndWaitFence(vk::CommandBuffer(cmd.Handle()), nullptr, nullptr, nullptr);
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

	void GUI::Draw(CommandBuffer cmd, uint32_t imageIndex)
	{
		if (!render)
			return;

		if (render && ImGui::GetDrawData()->TotalVtxCount > 0)
		{
			const vec4 color(0.0f, 0.0f, 0.0f, 1.0f);
			vk::ClearValue clearColor;
			memcpy(clearColor.color.float32, &color, 4 * sizeof(float));

			vk::ClearDepthStencilValue depthStencil;
			depthStencil.depth = 1.f;
			depthStencil.stencil = 0;

			std::vector<vk::ClearValue> clearValues = { clearColor, depthStencil };

			vk::RenderPassBeginInfo rpi;
			rpi.renderPass = *renderPass.handle;
			rpi.framebuffer = *framebuffers[imageIndex].handle;
			rpi.renderArea = vk::Rect2D{ {0, 0}, *VULKAN.surface.actualExtent };
			rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
			rpi.pClearValues = clearValues.data();

			cmd.BeginPass(renderPass, framebuffers[imageIndex]);
			ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd.Handle());
			cmd.EndPass();
		}
	}

	void GUI::SetWindowStyle(ImGuiStyle* dst)
	{
		ImGui::StyleColorsClassic();
		ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.3f);
	}

	void GUI::CreateRenderPass()
	{
		std::array<vk::AttachmentDescription, 1> attachments {};
		// Color attachment
		attachments[0].format = VULKAN.surface.formatKHR->format;
		attachments[0].samples = vk::SampleCountFlagBits::e1;
		attachments[0].loadOp = vk::AttachmentLoadOp::eLoad;
		attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
		attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		attachments[0].initialLayout = vk::ImageLayout::ePresentSrcKHR;
		attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;
		
		std::array<vk::SubpassDescription, 1> subpassDescriptions {};
		
		vk::AttachmentReference colorReference = {0, vk::ImageLayout::eColorAttachmentOptimal};
		
		subpassDescriptions[0].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpassDescriptions[0].colorAttachmentCount = 1;
		subpassDescriptions[0].pColorAttachments = &colorReference;
		
		vk::RenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
		renderPassInfo.pSubpasses = subpassDescriptions.data();
		
		renderPass.handle = make_sptr(VULKAN.device->createRenderPass(renderPassInfo));
	}

	void GUI::CreateFrameBuffers()
	{
		framebuffers.resize(VULKAN.swapchain.images.size());
		for (uint32_t i = 0; i < VULKAN.swapchain.images.size(); ++i)
		{
			uint32_t width = WIDTH;
			uint32_t height = HEIGHT;
			vk::ImageView view = VULKAN.swapchain.images[i].view;
			framebuffers[i].Create(width, height, view, renderPass);
		}
	}

	void GUI::Destroy()
	{
		renderPass.Destroy();
		for (auto& framebuffer : framebuffers)
			framebuffer.Destroy();
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
