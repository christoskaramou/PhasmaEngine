#include "GUI.h"
#include <filesystem>
#include "../include/TinyFileDialogs/tinyfiledialogs.h"
#include "../Queue/Queue.h"
#include "../Timer/Timer.h"
#include "../Console/Console.h"
#include "../Vertex/Vertex.h"
#include "../Swapchain/Swapchain.h"
#include "../Surface/Surface.h"

using namespace vm;

bool endsWithExt(const std::string &mainStr, const std::string &toMatch)
{
	return mainStr.size() >= toMatch.size() && mainStr.compare(mainStr.size() - toMatch.size(), toMatch.size(), toMatch) == 0;
}

void GUI::setWindows()
{
	Menu();
	LeftPanel();
	RenderingWindowBox();
	RightPanel();
	BottomPanel();
}

void GUI::LeftPanel() const
{
	Metrics();
}

void GUI::RightPanel() const
{
	Properties();
}

void GUI::BottomPanel() const
{
	ConsoleWindow();
	Scripts();
	Models();
}

void GUI::Menu() const
{
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Load...")) {
				std::vector<const char*> filter{ "*.gltf", "*.glb" };
				const char* result = tinyfd_openFileDialog("Choose Model", "", static_cast<int>(filter.size()), filter.data(), "", 0);
				if (result) {
					const std::string path(result);
					std::string folderPath = path.substr(0, path.find_last_of('\\') + 1);
					std::string modelName = path.substr(path.find_last_of('\\') + 1);
					Queue::loadModel.emplace_back(folderPath, modelName);
				}
			}
			if (ImGui::MenuItem("Exit")) {
				const SDL_MessageBoxButtonData buttons[] = { { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "cancel" },{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "yes" } };
				const SDL_MessageBoxColorScheme colorScheme = { {{ 255,   0,   0 }, {   0, 255,   0 }, { 255, 255,   0 }, {   0,   0, 255 }, { 255,   0, 255 }} };
				const SDL_MessageBoxData messageboxdata = { SDL_MESSAGEBOX_INFORMATION, nullptr,"Exit", "Are you sure you want to exit?",SDL_arraysize(buttons),buttons, &colorScheme };
				int buttonid;
				SDL_ShowMessageBox(&messageboxdata, &buttonid);
				if (buttonid == 1) {
					SDL_Event sdlevent;
					sdlevent.type = SDL_QUIT;
					SDL_PushEvent(&sdlevent);
				}
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
}

void GUI::Metrics() const
{
	int totalPasses = 0;
	float totalTime = 0.f;

	static bool metrics_open = true;
	ImGui::SetNextWindowPos(ImVec2(0.f, MENU_HEIGHT));
	ImGui::SetNextWindowSize(ImVec2(LEFT_PANEL_WIDTH, HEIGHT_f - LOWER_PANEL_HEIGHT - MENU_HEIGHT));
	ImGui::Begin("Metrics", &metrics_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
	ImGui::Text("Average %.3f ms (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	//for (int i = 0; i < 120; i++)
	//	lines[i] = ImGui::GetIO().Framerate;
	//ImVec2 plotextent(ImGui::GetContentRegionAvailWidth(), 100);
	//ImGui::PlotLines("FPSPlot", lines, 120, 0, nullptr, 0.0f, 400.0f, plotextent);
	ImGui::InputInt("FPS", &fps, 1, 15); fps = maximum(fps, 10);
	ImGui::Separator(); ImGui::Separator();

	ImGui::Text("CPU Total: %.3f (waited %.3f) ms", cpuTime, cpuWaitingTime);
	ImGui::Text("GPU Total: %.3f ms", stats[0] + (shadow_cast ? stats[11] + stats[12] + stats[13] : 0.f) + (use_compute ? stats[14] : 0.f));
	ImGui::Separator();
	ImGui::Text("Render Passes:");
	//if (use_compute) {
	//	ImGui::Text("   Compute: %.3f ms", stats[13]); totalPasses++;
	//}
	//ImGui::Text("   Skybox: %.3f ms", stats[1]); totalPasses++;
	ImGui::Indent(16.0f);
	if (shadow_cast) {
		ImGui::Text("Depth: %.3f ms", stats[11]); totalPasses++; totalTime += stats[11];
		ImGui::Text("Depth: %.3f ms", stats[12]); totalPasses++; totalTime += stats[12];
		ImGui::Text("Depth: %.3f ms", stats[13]); totalPasses++; totalTime += stats[13];
	}
	ImGui::Text("GBuffer: %.3f ms", stats[2]); totalPasses++; totalTime += stats[2];
	if (show_ssao) {
		ImGui::Text("SSAO: %.3f ms", stats[3]); totalPasses++; totalTime += stats[3];
	}
	if (show_ssr) {
		ImGui::Text("SSR: %.3f ms", stats[4]); totalPasses++; totalTime += stats[4];
	}
	ImGui::Text("Lights: %.3f ms", stats[5]); totalPasses++; totalTime += stats[5];
	if ((use_FXAA || use_TAA) && use_AntiAliasing) {
		ImGui::Text("Anti aliasing: %.3f ms", stats[6]); totalPasses++; totalTime += stats[6];
	}
	if (show_Bloom) {
		ImGui::Text("Bloom: %.3f ms", stats[7]); totalPasses++; totalTime += stats[7];
	}
	if (use_DOF) {
		ImGui::Text("Depth of Field: %.3f ms", stats[8]); totalPasses++; totalTime += stats[8];
	}
	if (show_motionBlur) {
		ImGui::Text("Motion Blur: %.3f ms", stats[9]); totalPasses++; totalTime += stats[9];
	}

	ImGui::Text("GUI: %.3f ms", stats[10]); totalPasses++; totalTime += stats[10];
	ImGui::Unindent(16.0f);
	ImGui::Separator();
	ImGui::Separator();
	ImGui::Text("Total: %i (%.3f ms)", totalPasses, totalTime);

	tlPanelPos = ImGui::GetWindowPos();
	tlPanelSize = ImGui::GetWindowSize();
	ImGui::End();
}

void GUI::ConsoleWindow()
{
	static bool console_open = true;
	static Console console;
	console.Draw("Console", &console_open, ImVec2(0.f, HEIGHT_f - LOWER_PANEL_HEIGHT), ImVec2(WIDTH_f / 3.f, LOWER_PANEL_HEIGHT));
}

void GUI::Scripts() const
{
	static bool scripts_open = true;
	ImGui::SetNextWindowPos(ImVec2(WIDTH_f / 3.f, HEIGHT_f - LOWER_PANEL_HEIGHT));
	ImGui::SetNextWindowSize(ImVec2(WIDTH_f / 3.f, LOWER_PANEL_HEIGHT));
	ImGui::Begin("Scripts Folder", &scripts_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	if (ImGui::Button("Create New Script")) {
		const char* result = tinyfd_inputBox("Script", "Give a name followed by the extension .cs", "");
		if (result) {
			std::string res = result;
			res = res.substr(0, res.find_last_of(".cs") + 1);
			if (std::find(fileList.begin(), fileList.end(), res) == fileList.end()) {
				const std::string cmd = "type nul > Scripts\\" + res;
				system(cmd.c_str());
				fileList.push_back(res);
			}
			else {
				SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Script not created", "Script name already exists", g_Window);
			}
		}
	}
	for (uint32_t i = 0; i < fileList.size(); i++)
	{
		if (endsWithExt(fileList[i], ".cs")) {
			std::string name = fileList[i] + "##" + std::to_string(i);
			if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
				if (ImGui::IsMouseDoubleClicked(0)) {
					std::string s = "Scripts\\" + fileList[i];
					system(s.c_str());
				}
			}
		}
	}
	ImGui::End();
}

void GUI::Models() const
{
	static bool models_open = true;
	ImGui::SetNextWindowPos(ImVec2(WIDTH_f * 2.f / 3.f, HEIGHT_f - LOWER_PANEL_HEIGHT));
	ImGui::SetNextWindowSize(ImVec2(WIDTH_f / 3.f, LOWER_PANEL_HEIGHT));
	ImGui::Begin("Models Loaded", &models_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	if (ImGui::Button("Add New Model")) {
		std::vector<const char*> filter{ "*.gltf", "*.glb" };
		const char* result = tinyfd_openFileDialog("Choose Model", "", static_cast<int>(filter.size()), filter.data(), "", 0);
		if (result) {
			const std::string path(result);
			std::string folderPath = path.substr(0, path.find_last_of('\\') + 1);
			std::string modelName = path.substr(path.find_last_of('\\') + 1);
			Queue::loadModel.emplace_back(folderPath, modelName);
		}
	}
	for (uint32_t i = 0; i < modelList.size(); i++) {
		std::string s = modelList[i] + "##" + std::to_string(i);
		if (ImGui::Selectable(s.c_str(), false))
			modelItemSelected = i;
	}

	ImGui::End();

	if (!Queue::loadModelFutures.empty()) {
		static bool loading = true;
		static float time = 0.f;
		ImGuiStyle* style = &ImGui::GetStyle();
		const ImVec4 temp = style->Colors[ImGuiCol_WindowBg];
		style->Colors[ImGuiCol_WindowBg] = ImVec4(.5f, .5f, .5f, 1.f);
		ImGui::SetNextWindowPos(ImVec2(WIDTH_f * .5f - 44.f, HEIGHT_f * .5f - 10.f));
		ImGui::SetNextWindowSize(ImVec2(88.f, 20.f));
		ImGui::Begin("Loading", &loading, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

		if (time < 1.f)
			ImGui::Text("Loading");
		else if (time < 2.f)
			ImGui::Text("Loading.");
		else if (time < 3.f)
			ImGui::Text("Loading..");
		else if (time < 4.f)
			ImGui::Text("Loading...");
		else
			time = 0.f;
		ImGui::End();
		time += Timer::delta;
		style->Colors[ImGuiCol_WindowBg] = temp;
	}
}

void GUI::Properties() const
{
	static bool	propetries_open = true;
	static float rtScale = renderTargetsScale;
	ImGui::SetNextWindowPos(ImVec2(WIDTH_f - RIGHT_PANEL_WIDTH, MENU_HEIGHT));
	ImGui::SetNextWindowSize(ImVec2(RIGHT_PANEL_WIDTH, HEIGHT_f - LOWER_PANEL_HEIGHT - MENU_HEIGHT));
	ImGui::Begin("Global Properties", &propetries_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGui::InputFloat("Quality.", &rtScale, 0.01f, 0.05f, 2);
	if (ImGui::Button("Apply")) {
		if (scaleRenderTargetsEventType != UINT32_MAX) {
			renderTargetsScale = clamp(rtScale, 0.1f, 1.0f);
			SDL_Event event;
			SDL_zero(event);
			event.type = scaleRenderTargetsEventType; // along side with window resize
			VulkanContext::get()->device.waitIdle();
			SDL_PushEvent(&event);
		}
	}
	ImGui::Checkbox("Lock Render Window", &lock_render_window);
	ImGui::Checkbox("IBL", &use_IBL);
	ImGui::Checkbox("SSR", &show_ssr);
	ImGui::Checkbox("SSAO", &show_ssao);
	ImGui::Checkbox("Depth of Field", &use_DOF);
	if (use_DOF) {
		ImGui::Indent(16.0f);
		ImGui::InputFloat("Scale##DOF", &DOF_focus_scale, 0.05f, 0.5f);
		ImGui::InputFloat("Range##DOF", &DOF_blur_range, 0.05f, 0.5f);
		ImGui::Unindent(16.0f);
		ImGui::Separator(); ImGui::Separator();
	}
	ImGui::Checkbox("Motion Blur", &show_motionBlur);
	if (show_motionBlur) {
		ImGui::Indent(16.0f);
		ImGui::InputFloat("Strength#mb", &motionBlur_strength, 0.05f, 0.2f);
		ImGui::Unindent(16.0f);
		ImGui::Separator(); ImGui::Separator();
	}
	ImGui::Checkbox("Tone Mapping", &show_tonemapping);
	//ImGui::Checkbox("Compute shaders", &use_compute);
	if (show_tonemapping) {
		ImGui::Indent(16.0f);
		ImGui::SliderFloat("Exposure", &exposure, 0.01f, 10.f);
		ImGui::Unindent(16.0f);
		ImGui::Separator(); ImGui::Separator();
	}
	ImGui::Checkbox("Anti aliasing", &use_AntiAliasing);
	if (use_AntiAliasing) {
		ImGui::Indent(16.0f);
		if (ImGui::Checkbox("FXAA", &use_FXAA)) {
			use_TAA = false;
		}
		if (ImGui::Checkbox("TAA", &use_TAA)) {
			use_FXAA = false;
		}
		if (use_TAA) {
			ImGui::Indent(16.0f);
			ImGui::InputFloat("Jitter", &TAA_jitter_scale, 0.01f, 0.1f, 5);
			ImGui::InputFloat("FeedbackMin", &TAA_feedback_min, 0.005f, 0.05f, 3);
			ImGui::InputFloat("FeedbackMax", &TAA_feedback_max, 0.005f, 0.05f, 3);
			ImGui::InputFloat("Strength", &TAA_sharp_strength, 0.1f, 0.2f, 2);
			ImGui::InputFloat("Clamp", &TAA_sharp_clamp, 0.01f, 0.05f, 3);
			ImGui::InputFloat("Bias", &TAA_sharp_offset_bias, 0.1f, 0.3f, 1);
			ImGui::Unindent(16.0f);
		}
		ImGui::Unindent(16.0f);
		ImGui::Separator(); ImGui::Separator();
	}
	else {
		use_TAA = false;
		use_FXAA = false;
	}

	ImGui::Checkbox("Bloom", &show_Bloom);
	if (show_Bloom) {
		ImGui::Indent(16.0f);
		ImGui::SliderFloat("Inv Brightness", &Bloom_Inv_brightness, 0.01f, 50.f);
		ImGui::SliderFloat("Intensity", &Bloom_intensity, 0.01f, 10.f);
		ImGui::SliderFloat("Range", &Bloom_range, 0.1f, 20.f);
		ImGui::Checkbox("Bloom Tone Mapping", &use_tonemap);
		if (use_tonemap)
			ImGui::SliderFloat("Bloom Exposure", &Bloom_exposure, 0.01f, 10.f);
		ImGui::Unindent(16.0f);
		ImGui::Separator(); ImGui::Separator();
	}
	ImGui::Checkbox("Fog", &use_fog);
	if (use_fog) {
		ImGui::Indent(16.0f);
		ImGui::InputFloat("Intensity", &fog_intensity, 0.01f, 0.1f, 4);
		ImGui::InputFloat("Spread", &fog_spread, 0.1f, 1.0f, 4);
		ImGui::InputFloat("Height", &fog_height, 0.01f, 0.1f, 4);
		ImGui::Checkbox("Volumetric", &use_Volumetric_lights);
		if (use_Volumetric_lights) {
			ImGui::Indent(16.0f);
			ImGui::InputInt("Iterations", &volumetric_steps, 1, 3);
			ImGui::InputInt("Dither", &volumetric_dither_strength, 1, 10);
			ImGui::Unindent(16.0f);
		}
		ImGui::Unindent(16.0f);
	}
	ImGui::Checkbox("Sun Light", &shadow_cast);
	if (shadow_cast) {
		ImGui::Indent(16.0f);
		ImGui::SliderFloat("Sun Intst", &sun_intensity, 0.1f, 50.f);
		ImGui::InputFloat3("SunPos", sun_position.data(), 1);
		ImGui::InputFloat("Slope", &depthBias[2], 0.15f, 0.5f, 5); ImGui::Separator(); ImGui::Separator();
		{
			vec3 sunDist(&sun_position[0]);
			if (lengthSquared(sunDist) > 160000.f) {
				sunDist = 400.f * normalize(sunDist);
				sun_position[0] = sunDist.x;
				sun_position[1] = sunDist.y;
				sun_position[2] = sunDist.z;
			}
		}
		ImGui::Unindent(16.0f);
	}
	ImGui::InputFloat("CamSpeed", &cameraSpeed, 0.1f, 1.f, 3);
	ImGui::SliderFloat4("ClearCol", clearColor.data(), 0.0f, 1.0f);
	ImGui::InputFloat("TimeScale", &timeScale, 0.05f, 0.2f); ImGui::Separator(); ImGui::Separator();
	if (ImGui::Button("Randomize Lights"))
		randomize_lights = true;
	ImGui::SliderFloat("Light Intst", &lights_intensity, 0.01f, 30.f);
	ImGui::SliderFloat("Light Rng", &lights_range, 0.1f, 30.f);

	// Model properties
	ImGui::Separator();
	ImGui::Separator();
	ImGui::Separator();
	ImGui::LabelText("", "Model Properties");
	if (modelItemSelected > -1) {
		const std::string toStr = std::to_string(modelItemSelected);
		const std::string id = " ID[" + toStr + "]";
		const std::string fmt = modelList[modelItemSelected] + id;
		ImGui::TextColored(ImVec4(.6f, 1.f, .5f, 1.f), "%s", fmt.c_str());

		ImGui::Separator();
		if (ImGui::Button("Unload Model"))
			Queue::unloadModel.push_back(modelItemSelected);

		ImGui::Separator();
		 const std::string s = "Scale##" + toStr;
		 const std::string p = "Position##" + toStr;
		 const std::string r = "Rotation##" + toStr;
		ImGui::InputFloat3(s.c_str(), model_scale[modelItemSelected].data(), 3);
		ImGui::InputFloat3(p.c_str(), model_pos[modelItemSelected].data(), 3);
		ImGui::InputFloat3(r.c_str(), model_rot[modelItemSelected].data(), 3);

		ImGui::Separator();
		ImGui::Separator();
		if (ImGui::Button("Add Script")) {
			std::vector<const char*> filter{ "*.cs" };
			const char* result = tinyfd_openFileDialog("Choose Script", "", static_cast<int>(filter.size()), filter.data(), "", 0);
			if (result) {
				std::string path(result);
				path = path.substr(0, path.find_last_of('.'));
				Queue::addScript.emplace_back(modelItemSelected, path.substr(path.find_last_of('\\') + 1));
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Compile Script")) {
			Queue::compileScript.push_back(modelItemSelected);
		}

		if (ImGui::Button("Remove Script")) {
			Queue::removeScript.push_back(modelItemSelected);
		}
	}

	mlPanelPos = ImGui::GetWindowPos();
	mlPanelSize = ImGui::GetWindowSize();
	ImGui::End();
}

void GUI::RenderingWindowBox() const
{
	static bool active = true;
	ImGuiStyle* style = &ImGui::GetStyle();
	style->Colors[ImGuiCol_WindowBg].w = 0.0f;
	int flags = ImGuiWindowFlags_NoTitleBar;
	if (lock_render_window) {
		flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		ImGui::SetNextWindowPos(ImVec2(LEFT_PANEL_WIDTH, MENU_HEIGHT));
		ImGui::SetNextWindowSize(ImVec2(WIDTH_f - LEFT_PANEL_WIDTH - RIGHT_PANEL_WIDTH, HEIGHT_f - LOWER_PANEL_HEIGHT - MENU_HEIGHT));
	}
	ImGui::Begin("Rendering Window", &active, flags);
	winPos = ImGui::GetWindowPos();
	winSize = ImGui::GetWindowSize();
	ImGui::End();
	style->Colors[ImGuiCol_WindowBg].w = 1.0f;
}

vk::DescriptorSetLayout GUI::getDescriptorSetLayout(vk::Device device)
{
	if (!descriptorSetLayout) {
		// binding for gui texture
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBindings(1);

		descriptorSetLayoutBindings[0].binding = 0;
		descriptorSetLayoutBindings[0].descriptorCount = 1;
		descriptorSetLayoutBindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		descriptorSetLayoutBindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

		vk::DescriptorSetLayoutCreateInfo createInfo;
		createInfo.bindingCount = static_cast<uint32_t>(descriptorSetLayoutBindings.size());
		createInfo.pBindings = descriptorSetLayoutBindings.data();
		descriptorSetLayout = device.createDescriptorSetLayout(createInfo);
	}
	return descriptorSetLayout;
}

const char* GUI::ImGui_ImplSDL2_GetClipboardText(void*)
{
	if (g_ClipboardTextData)
		SDL_free(g_ClipboardTextData);
	g_ClipboardTextData = SDL_GetClipboardText();
	return g_ClipboardTextData;
}

void GUI::ImGui_ImplSDL2_SetClipboardText(void*, const char* text)
{
	SDL_SetClipboardText(text);
}

void GUI::initImGui()
{
	auto vulkan = VulkanContext::get();
	vk::CommandBufferAllocateInfo cbai;
	cbai.commandPool = vulkan->commandPool;
	cbai.level = vk::CommandBufferLevel::ePrimary;
	cbai.commandBufferCount = 1;
	cmdBuf = vulkan->device.allocateCommandBuffers(cbai).at(0);

	vk::FenceCreateInfo fiu;
	fenceUpload = vulkan->device.createFence(fiu);

	for (auto& file : std::filesystem::directory_iterator("Scripts")) {
		auto pathStr = file.path().string();
		if (endsWithExt(pathStr, ".cs"))
			fileList.push_back(pathStr.substr(pathStr.find_last_of('\\') + 1));
	}

	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	g_Window = vulkan->window;

	// Setup back-end capabilities flags
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;       // We can honor GetMouseCursor() values (optional)
	io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;        // We can honor io.WantSetMousePos requests (optional, rarely used)

	// Keyboard mapping. ImGui will use those indices to peek into the io.KeysDown[] array.
	io.KeyMap[ImGuiKey_Tab] = SDL_SCANCODE_TAB;
	io.KeyMap[ImGuiKey_LeftArrow] = SDL_SCANCODE_LEFT;
	io.KeyMap[ImGuiKey_RightArrow] = SDL_SCANCODE_RIGHT;
	io.KeyMap[ImGuiKey_UpArrow] = SDL_SCANCODE_UP;
	io.KeyMap[ImGuiKey_DownArrow] = SDL_SCANCODE_DOWN;
	io.KeyMap[ImGuiKey_PageUp] = SDL_SCANCODE_PAGEUP;
	io.KeyMap[ImGuiKey_PageDown] = SDL_SCANCODE_PAGEDOWN;
	io.KeyMap[ImGuiKey_Home] = SDL_SCANCODE_HOME;
	io.KeyMap[ImGuiKey_End] = SDL_SCANCODE_END;
	io.KeyMap[ImGuiKey_Insert] = SDL_SCANCODE_INSERT;
	io.KeyMap[ImGuiKey_Delete] = SDL_SCANCODE_DELETE;
	io.KeyMap[ImGuiKey_Backspace] = SDL_SCANCODE_BACKSPACE;
	io.KeyMap[ImGuiKey_Space] = SDL_SCANCODE_SPACE;
	io.KeyMap[ImGuiKey_Enter] = SDL_SCANCODE_RETURN;
	io.KeyMap[ImGuiKey_Escape] = SDL_SCANCODE_ESCAPE;
	io.KeyMap[ImGuiKey_A] = SDL_SCANCODE_A;
	io.KeyMap[ImGuiKey_C] = SDL_SCANCODE_C;
	io.KeyMap[ImGuiKey_V] = SDL_SCANCODE_V;
	io.KeyMap[ImGuiKey_X] = SDL_SCANCODE_X;
	io.KeyMap[ImGuiKey_Y] = SDL_SCANCODE_Y;
	io.KeyMap[ImGuiKey_Z] = SDL_SCANCODE_Z;

	io.SetClipboardTextFn = ImGui_ImplSDL2_SetClipboardText;
	io.GetClipboardTextFn = ImGui_ImplSDL2_GetClipboardText;
	io.ClipboardUserData = nullptr;

	g_MouseCursors[ImGuiMouseCursor_Arrow] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
	g_MouseCursors[ImGuiMouseCursor_TextInput] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
	g_MouseCursors[ImGuiMouseCursor_ResizeAll] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
	g_MouseCursors[ImGuiMouseCursor_ResizeNS] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
	g_MouseCursors[ImGuiMouseCursor_ResizeEW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
	g_MouseCursors[ImGuiMouseCursor_ResizeNESW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
	g_MouseCursors[ImGuiMouseCursor_ResizeNWSE] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
	g_MouseCursors[ImGuiMouseCursor_Hand] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);

#ifdef _WIN32
	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo(vulkan->window, &wmInfo);
	io.ImeWindowHandle = wmInfo.info.win.window;
#else
	(void)window;
#endif
	windowStyle();

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
	vulkan->dynamicCmdBuffer.begin(beginInfo);

	// Create fonts texture
	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	size_t upload_size = width * height * 4 * sizeof(char);

	// Create the Image:
	{
		texture.format = vk::Format::eR8G8B8A8Unorm;
		texture.mipLevels = 1;
		texture.arrayLayers = 1;
		texture.initialLayout = vk::ImageLayout::eUndefined;
		texture.createImage(width, height, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

		texture.viewType = vk::ImageViewType::e2D;
		texture.createImageView(vk::ImageAspectFlagBits::eColor);

		texture.addressMode = vk::SamplerAddressMode::eRepeat;
		texture.maxAnisotropy = 1.f;
		texture.minLod = -1000.f;
		texture.maxLod = 1000.f;
		texture.createSampler();
	}
	// Create the and Upload to Buffer:
	Buffer stagingBuffer;
	{
		stagingBuffer.createBuffer(upload_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible);
		stagingBuffer.map();
		stagingBuffer.copyData(pixels);
		stagingBuffer.flush();
		stagingBuffer.unmap();
	}

	// Copy to Image:
	{
		vk::ImageMemoryBarrier copy_barrier = {};
		copy_barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		copy_barrier.oldLayout = vk::ImageLayout::eUndefined;
		copy_barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
		copy_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copy_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copy_barrier.image = texture.image;
		copy_barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		copy_barrier.subresourceRange.levelCount = 1;
		copy_barrier.subresourceRange.layerCount = 1;
		vulkan->dynamicCmdBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eHost,
			vk::PipelineStageFlagBits::eTransfer,
			vk::DependencyFlagBits::eByRegion,
			nullptr,
			nullptr,
			copy_barrier
		);

		vk::BufferImageCopy region = {};
		region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.imageSubresource.layerCount = 1;
		region.imageExtent.width = width;
		region.imageExtent.height = height;
		region.imageExtent.depth = 1;
		vulkan->dynamicCmdBuffer.copyBufferToImage(stagingBuffer.buffer, texture.image, vk::ImageLayout::eTransferDstOptimal, region);

		vk::ImageMemoryBarrier use_barrier = {};
		use_barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		use_barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		use_barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		use_barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		use_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		use_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		use_barrier.image = texture.image;
		use_barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		use_barrier.subresourceRange.levelCount = 1;
		use_barrier.subresourceRange.layerCount = 1;
		vulkan->dynamicCmdBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eFragmentShader,
			vk::DependencyFlagBits::eByRegion,
			nullptr,
			nullptr,
			use_barrier
		);
	}

	// Store our identifier
	io.Fonts->TexID = reinterpret_cast<ImTextureID>(reinterpret_cast<intptr_t>(static_cast<VkImage>(texture.image)));

	vulkan->dynamicCmdBuffer.end();

	vulkan->submitAndWaitFence(vulkan->dynamicCmdBuffer, nullptr, nullptr, nullptr);

	stagingBuffer.destroy();
}

void GUI::loadGUI(bool show)
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

	initImGui();
}

void GUI::scaleToRenderArea(vk::CommandBuffer cmd, Image& renderedImage, uint32_t imageIndex)
{
	Image& s_chain_Image = VulkanContext::get()->swapchain->images[imageIndex];

	renderedImage.transitionImageLayout(
		cmd,
		vk::ImageLayout::eColorAttachmentOptimal,
		vk::ImageLayout::eTransferSrcOptimal,
		vk::PipelineStageFlagBits::eColorAttachmentOutput,
		vk::PipelineStageFlagBits::eTransfer,
		vk::AccessFlagBits::eColorAttachmentWrite,
		vk::AccessFlagBits::eTransferRead,
		vk::ImageAspectFlagBits::eColor);
	s_chain_Image.transitionImageLayout(
		cmd,
		vk::ImageLayout::ePresentSrcKHR,
		vk::ImageLayout::eTransferDstOptimal,
		vk::PipelineStageFlagBits::eColorAttachmentOutput,
		vk::PipelineStageFlagBits::eTransfer,
		vk::AccessFlagBits::eColorAttachmentRead,
		vk::AccessFlagBits::eTransferWrite,
		vk::ImageAspectFlagBits::eColor);

	vk::ImageBlit blit;
	blit.srcOffsets[0] = vk::Offset3D{ 0, 0, 0 };
	blit.srcOffsets[1] = vk::Offset3D{ static_cast<int32_t>(renderedImage.width), static_cast<int32_t>(renderedImage.height), 1 };
	blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	blit.srcSubresource.layerCount = 1;
	blit.dstOffsets[0] = vk::Offset3D{ static_cast<int32_t>(winPos.x), static_cast<int32_t>(winPos.y), 0 };
	blit.dstOffsets[1] = vk::Offset3D{ static_cast<int32_t>(winPos.x) + static_cast<int32_t>(winSize.x), static_cast<int32_t>(winPos.y) + static_cast<int32_t>(winSize.y), 1 };
	blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	blit.dstSubresource.layerCount = 1;

	cmd.blitImage(
		renderedImage.image,
		vk::ImageLayout::eTransferSrcOptimal,
		s_chain_Image.image,
		vk::ImageLayout::eTransferDstOptimal,
		blit,
		vk::Filter::eLinear);

	renderedImage.transitionImageLayout(
		cmd,
		vk::ImageLayout::eTransferSrcOptimal,
		vk::ImageLayout::eColorAttachmentOptimal,
		vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eColorAttachmentOutput,
		vk::AccessFlagBits::eTransferRead,
		vk::AccessFlagBits::eColorAttachmentWrite,
		vk::ImageAspectFlagBits::eColor);
	s_chain_Image.transitionImageLayout(
		cmd,
		vk::ImageLayout::eTransferDstOptimal,
		vk::ImageLayout::ePresentSrcKHR,
		vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eColorAttachmentOutput,
		vk::AccessFlagBits::eTransferWrite,
		vk::AccessFlagBits::eColorAttachmentRead,
		vk::ImageAspectFlagBits::eColor);
}

void GUI::draw(vk::CommandBuffer cmd, uint32_t imageIndex)
{
	if (!render) return;

	const auto draw_data = ImGui::GetDrawData();
	if (render && draw_data->TotalVtxCount > 0)
	{
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));

		vk::ClearDepthStencilValue depthStencil;
		depthStencil.depth = 1.f;
		depthStencil.stencil = 0;

		std::vector<vk::ClearValue> clearValues = { clearColor, depthStencil };

		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = renderPass;
		rpi.framebuffer = frameBuffers[imageIndex];
		rpi.renderArea = vk::Rect2D{ { 0, 0 }, VulkanContext::get()->surface->actualExtent };
		rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
		rpi.pClearValues = clearValues.data();

		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);

		const vk::DeviceSize offset{ 0 };
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, descriptorSet, nullptr);
		cmd.bindVertexBuffers(0, vertexBuffer.buffer, offset);
		cmd.bindIndexBuffer(indexBuffer.buffer, 0, vk::IndexType::eUint32);

		vk::Viewport viewport;
		viewport.x = 0.f;
		viewport.y = 0.f;
		viewport.width = draw_data->DisplaySize.x;
		viewport.height = draw_data->DisplaySize.y;
		viewport.minDepth = 0.f;
		viewport.maxDepth = 1.f;
		cmd.setViewport(0, viewport);

		std::vector<float> data(4);
		data[0] = 2.0f / draw_data->DisplaySize.x;				// scale
		data[1] = 2.0f / draw_data->DisplaySize.y;				// scale
		data[2] = -1.0f - draw_data->DisplayPos.x * data[0];	// transform
		data[3] = -1.0f - draw_data->DisplayPos.y * data[1];	// transform
		cmd.pushConstants<float>(pipeline.pipeinfo.layout, vk::ShaderStageFlagBits::eVertex, 0, data);

		// Render the command lists:
		int vtx_offset = 0;
		int idx_offset = 0;
		const ImVec2 display_pos = draw_data->DisplayPos;
		for (int n = 0; n < draw_data->CmdListsCount; n++)
		{
			const ImDrawList* cmd_list = draw_data->CmdLists[n];
			for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
			{
				const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
				if (pcmd->UserCallback)
				{
					pcmd->UserCallback(cmd_list, pcmd);
				}
				else
				{
					// Apply scissor/clipping rectangle
					// FIXME: We could clamp width/height based on clamped min/max values.
					vk::Rect2D scissor;
					scissor.offset.x = static_cast<int32_t>(pcmd->ClipRect.x - display_pos.x) > 0 ? static_cast<int32_t>(pcmd->ClipRect.x - display_pos.x) : 0;
					scissor.offset.y = static_cast<int32_t>(pcmd->ClipRect.y - display_pos.y) > 0 ? static_cast<int32_t>(pcmd->ClipRect.y - display_pos.y) : 0;
					scissor.extent.width = static_cast<uint32_t>(pcmd->ClipRect.z - pcmd->ClipRect.x);
					scissor.extent.height = static_cast<uint32_t>(pcmd->ClipRect.w - pcmd->ClipRect.y + 1); // FIXME: Why +1 here?
					cmd.setScissor(0, scissor);

					// Draw
					cmd.drawIndexed(pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);
				}
				idx_offset += static_cast<int>(pcmd->ElemCount);
			}
			vtx_offset += cmd_list->VtxBuffer.Size;
		}
		cmd.endRenderPass();
	}
}

void GUI::newFrame()
{
	ImGuiIO& io = ImGui::GetIO();
	IM_ASSERT(io.Fonts->IsBuilt());     // Font atlas needs to be built, call renderer _NewFrame() function e.g. ImGui_ImplOpenGL3_NewFrame() 

	// Setup display size (every frame to accommodate for window resizing)
	int w, h;
	int display_w, display_h;
	SDL_GetWindowSize(VulkanContext::get()->window, &w, &h);
	SDL_GL_GetDrawableSize(VulkanContext::get()->window, &display_w, &display_h);
	io.DisplaySize = ImVec2(static_cast<float>(w), static_cast<float>(h));
	io.DisplayFramebufferScale = ImVec2(w > 0 ? static_cast<float>(display_w) / static_cast<float>(w) : 0, h > 0 ? static_cast<float>(display_h) / static_cast<float>(h) : 0);

	// Setup time step (we don't use SDL_GetTicks() because it is using millisecond resolution)
	static Uint64 frequency = SDL_GetPerformanceFrequency();
	const Uint64 current_time = SDL_GetPerformanceCounter();
	io.DeltaTime = g_Time > 0 ? static_cast<float>(static_cast<double>(current_time - g_Time) / frequency) : static_cast<float>(1.0f / 60.0f);
	g_Time = current_time;

	// Set OS mouse position if requested (rarely used, only when ImGuiConfigFlags_NavEnableSetMousePos is enabled by user)
	if (io.WantSetMousePos)
		SDL_WarpMouseInWindow(g_Window, static_cast<int>(io.MousePos.x), static_cast<int>(io.MousePos.y));
	else
		io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);

	int mx, my;
	const Uint32 mouse_buttons = SDL_GetMouseState(&mx, &my);
	io.MouseDown[0] = g_MousePressed[0] || (mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;  // If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
	io.MouseDown[1] = g_MousePressed[1] || (mouse_buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
	io.MouseDown[2] = g_MousePressed[2] || (mouse_buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
	g_MousePressed[0] = g_MousePressed[1] = g_MousePressed[2] = false;

#if SDL_HAS_CAPTURE_MOUSE && !defined(__EMSCRIPTEN__)
	SDL_Window* focused_window = SDL_GetKeyboardFocus();
	if (g_Window == focused_window)
	{
		// SDL_GetMouseState() gives mouse position seemingly based on the last window entered/focused(?)
		// The creation of a new windows at runtime and SDL_CaptureMouse both seems to severely mess up with that, so we retrieve that position globally.
		int wx, wy;
		SDL_GetWindowPosition(focused_window, &wx, &wy);
		SDL_GetGlobalMouseState(&mx, &my);
		mx -= wx;
		my -= wy;
		io.MousePos = ImVec2(static_cast<float>(mx), static_cast<float>(my));
	}

	// SDL_CaptureMouse() let the OS know e.g. that our imgui drag outside the SDL window boundaries shouldn't e.g. trigger the OS window resize cursor. 
	// The function is only supported from SDL 2.0.4 (released Jan 2016)
	bool any_mouse_button_down = ImGui::IsAnyMouseDown();
	SDL_CaptureMouse(any_mouse_button_down ? SDL_TRUE : SDL_FALSE);
#else
	if (SDL_GetWindowFlags(g_Window) & SDL_WINDOW_INPUT_FOCUS)
		io.MousePos = ImVec2(static_cast<float>(mx), static_cast<float>(my));
#endif
	if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
		return;

	const ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
	if (io.MouseDrawCursor || imgui_cursor == ImGuiMouseCursor_None)
	{
		// Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
		//SDL_ShowCursor(SDL_FALSE);
	}
	else
	{
		// Show OS mouse cursor
		SDL_SetCursor(g_MouseCursors[imgui_cursor] ? g_MouseCursors[imgui_cursor] : g_MouseCursors[ImGuiMouseCursor_Arrow]);
		//SDL_ShowCursor(SDL_TRUE);
	}

	ImGui::NewFrame();

	setWindows();
	//if (show_demo_window)
	//	ImGui::ShowDemoWindow(&show_demo_window);

	ImGui::Render();

	const ImDrawData* draw_data = ImGui::GetDrawData();
	if (draw_data->TotalVtxCount < 1)
		return;
	const size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
	const size_t index_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
	if (!vertexBuffer.buffer || vertexBuffer.size < vertex_size)
		createVertexBuffer(vertex_size);
	if (!indexBuffer.buffer || indexBuffer.size < index_size)
		createIndexBuffer(index_size);

	//// Upload Vertex and index Data:
	//{
	//	ImDrawVert* vtx_dst = NULL;
	//	ImDrawIdx* idx_dst = NULL;
	//	vtx_dst = (ImDrawVert*)vulkan->device.mapMemory(vertexBuffer.memory, 0, vertex_size);
	//	idx_dst = (ImDrawIdx*)vulkan->device.mapMemory(indexBuffer.memory, 0, index_size);
	//	for (int n = 0; n < draw_data->CmdListsCount; n++)
	//	{
	//		const ImDrawList* cmd_list = draw_data->CmdLists[n];
	//		memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
	//		memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
	//		vtx_dst += cmd_list->VtxBuffer.Size;
	//		idx_dst += cmd_list->IdxBuffer.Size;
	//	}
	//	vk::MappedMemoryRange range[2] = {};
	//	range[0].memory = vertexBuffer.memory;
	//	range[0].size = VK_WHOLE_SIZE;
	//	range[1].memory = indexBuffer.memory;
	//	range[1].size = VK_WHOLE_SIZE;
	//	vulkan->device.flushMappedMemoryRanges(2, range);
	//	vulkan->device.unmapMemory(vertexBuffer.memory);
	//	vulkan->device.unmapMemory(indexBuffer.memory);
	//}
	
	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
	cmdBuf.begin(beginInfo);
	vk::DeviceSize vOffset = 0;
	vk::DeviceSize iOffset = 0;
	for (int n = 0; n < draw_data->CmdListsCount; n++) {
		const ImDrawList* cmd_list = draw_data->CmdLists[n];
		cmdBuf.updateBuffer(vertexBuffer.buffer, vOffset, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), cmd_list->VtxBuffer.Data);
		cmdBuf.updateBuffer(indexBuffer.buffer, iOffset, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), cmd_list->IdxBuffer.Data);
		vOffset += cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);
		iOffset += cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
	}
	cmdBuf.end();

	VulkanContext::get()->submit(cmdBuf, nullptr, nullptr, nullptr, fenceUpload);
}

void GUI::windowStyle(ImGuiStyle* dst)
{
	ImGuiStyle* style = dst ? dst : &ImGui::GetStyle();

	style->WindowRounding = 0.0;
	style->Colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
	style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
	style->Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
	style->Colors[ImGuiCol_ChildBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
	style->Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
	style->Colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
	style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 1.00f, 1.00f);
	style->Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.29f, 0.48f, 0.54f);
	style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
	style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
	style->Colors[ImGuiCol_TitleBg] = style->Colors[ImGuiCol_WindowBg];
	style->Colors[ImGuiCol_TitleBgActive] = style->Colors[ImGuiCol_WindowBg];
	style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
	style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.0f, 0.5f, 0.85f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
	style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
	style->Colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
	style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style->Colors[ImGuiCol_Button] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
	style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
	style->Colors[ImGuiCol_Header] = ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
	style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
	style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style->Colors[ImGuiCol_Separator] = style->Colors[ImGuiCol_Border];
	style->Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
	style->Colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
	style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
	style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
	style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
	style->Colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
	style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
	style->Colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
	style->Colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	style->Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	style->Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	style->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
}

void GUI::createVertexBuffer(size_t vertex_size)
{
	VulkanContext::get()->graphicsQueue.waitIdle();
	vertexBuffer.destroy();
	vertexBuffer.createBuffer(vertex_size, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
	//vertexBuffer.createBuffer(vertex_size, vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostCached | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible);
}

void GUI::createIndexBuffer(size_t index_size)
{
	VulkanContext::get()->graphicsQueue.waitIdle();
	indexBuffer.destroy();
	indexBuffer.createBuffer(index_size, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
	//indexBuffer.createBuffer(index_size, vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eHostCached | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible);
}

void GUI::createDescriptorSet(const vk::DescriptorSetLayout& descriptorSetLayout)
{
	vk::DescriptorSetAllocateInfo allocateInfo;
	allocateInfo.descriptorPool = VulkanContext::get()->descriptorPool;
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = &descriptorSetLayout;
	descriptorSet = VulkanContext::get()->device.allocateDescriptorSets(allocateInfo).at(0);

	updateDescriptorSets();

}

void vm::GUI::updateDescriptorSets() const
{
	// texture sampler
	vk::DescriptorImageInfo dii0;
	dii0.sampler = texture.sampler;
	dii0.imageView = texture.view;
	dii0.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

	std::vector<vk::WriteDescriptorSet> textureWriteSets(1);

	textureWriteSets[0].dstSet = descriptorSet;
	textureWriteSets[0].dstBinding = 0;
	textureWriteSets[0].dstArrayElement = 0;
	textureWriteSets[0].descriptorCount = 1;
	textureWriteSets[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
	textureWriteSets[0].pImageInfo = &dii0;

	VulkanContext::get()->device.updateDescriptorSets(textureWriteSets, nullptr);
}

void GUI::createRenderPass()
{
	std::array<vk::AttachmentDescription, 1> attachments{};
	// Color attachment
	attachments[0].format = VulkanContext::get()->surface->formatKHR.format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eLoad;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::ePresentSrcKHR;
	attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;

	std::array<vk::SubpassDescription, 1> subpassDescriptions{};

	vk::AttachmentReference colorReference = { 0, vk::ImageLayout::eColorAttachmentOptimal };

	subpassDescriptions[0].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescriptions[0].colorAttachmentCount = 1;
	subpassDescriptions[0].pColorAttachments = &colorReference;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
	renderPassInfo.pSubpasses = subpassDescriptions.data();

	renderPass = VulkanContext::get()->device.createRenderPass(renderPassInfo);
}

void GUI::createFrameBuffers()
{
	frameBuffers.resize(VulkanContext::get()->swapchain->images.size());

	for (size_t i = 0; i < frameBuffers.size(); ++i) {
		std::vector<vk::ImageView> attachments = {
			VulkanContext::get()->swapchain->images[i].view
		};
		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = renderPass;
		fbci.attachmentCount = static_cast<uint32_t>(attachments.size());
		fbci.pAttachments = attachments.data();
		fbci.width = WIDTH;
		fbci.height = HEIGHT;
		fbci.layers = 1;
		frameBuffers[i] = VulkanContext::get()->device.createFramebuffer(fbci);
	}
}

void GUI::createPipeline()
{
	// Shader stages
	std::vector<char> vertCode = readFile("shaders/GUI/vert.spv");
	vk::ShaderModuleCreateInfo vsmci;
	vsmci.codeSize = vertCode.size();
	vsmci.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
	vk::ShaderModule vertModule = VulkanContext::get()->device.createShaderModule(vsmci);

	std::vector<char> fragCode = readFile("shaders/GUI/frag.spv");
	vk::ShaderModuleCreateInfo fsmci;
	fsmci.codeSize = fragCode.size();
	fsmci.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
	vk::ShaderModule fragModule = VulkanContext::get()->device.createShaderModule(fsmci);

	vk::PipelineShaderStageCreateInfo pssci1;
	pssci1.stage = vk::ShaderStageFlagBits::eVertex;
	pssci1.module = vertModule;
	pssci1.pName = "main";

	vk::PipelineShaderStageCreateInfo pssci2;
	pssci2.stage = vk::ShaderStageFlagBits::eFragment;
	pssci2.module = fragModule;
	pssci2.pName = "main";

	std::vector<vk::PipelineShaderStageCreateInfo> stages{ pssci1, pssci2 };
	pipeline.pipeinfo.stageCount = static_cast<uint32_t>(stages.size());
	pipeline.pipeinfo.pStages = stages.data();

	// Vertex Input state
	auto vibd = Vertex::getBindingDescriptionGUI();
	auto viad = Vertex::getAttributeDescriptionGUI();
	vk::PipelineVertexInputStateCreateInfo pvisci;
	pvisci.vertexBindingDescriptionCount = static_cast<uint32_t>(vibd.size());
	pvisci.pVertexBindingDescriptions = vibd.data();
	pvisci.vertexAttributeDescriptionCount = static_cast<uint32_t>(viad.size());
	pvisci.pVertexAttributeDescriptions = viad.data();
	pipeline.pipeinfo.pVertexInputState = &pvisci;

	// Input Assembly stage
	vk::PipelineInputAssemblyStateCreateInfo piasci;
	piasci.topology = vk::PrimitiveTopology::eTriangleList;
	piasci.primitiveRestartEnable = VK_FALSE;
	pipeline.pipeinfo.pInputAssemblyState = &piasci;

	// Viewports and Scissors
	vk::Viewport vp;
	vp.x = 0.0f;
	vp.y = 0.0f;
	vp.width = WIDTH_f;
	vp.height = HEIGHT_f;
	vp.minDepth = 0.f;
	vp.maxDepth = 1.f;

	vk::Rect2D r2d;
	r2d.extent = vk::Extent2D{ WIDTH, HEIGHT };

	vk::PipelineViewportStateCreateInfo pvsci;
	pvsci.viewportCount = 1;
	pvsci.pViewports = &vp;
	pvsci.scissorCount = 1;
	pvsci.pScissors = &r2d;
	pipeline.pipeinfo.pViewportState = &pvsci;

	// Rasterization state
	vk::PipelineRasterizationStateCreateInfo prsci;
	prsci.depthClampEnable = VK_FALSE;
	prsci.rasterizerDiscardEnable = VK_FALSE;
	prsci.polygonMode = vk::PolygonMode::eFill;
	prsci.cullMode = vk::CullModeFlagBits::eBack;
	prsci.frontFace = vk::FrontFace::eClockwise;
	prsci.depthBiasEnable = VK_FALSE;
	prsci.depthBiasConstantFactor = 0.0f;
	prsci.depthBiasClamp = 0.0f;
	prsci.depthBiasSlopeFactor = 0.0f;
	prsci.lineWidth = 1.0f;
	pipeline.pipeinfo.pRasterizationState = &prsci;

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo pmsci;
	pmsci.rasterizationSamples = vk::SampleCountFlagBits::e1;
	pmsci.sampleShadingEnable = VK_FALSE;
	pmsci.minSampleShading = 1.0f;
	pmsci.pSampleMask = nullptr;
	pmsci.alphaToCoverageEnable = VK_FALSE;
	pmsci.alphaToOneEnable = VK_FALSE;
	pipeline.pipeinfo.pMultisampleState = &pmsci;

	// Depth stencil state
	vk::PipelineDepthStencilStateCreateInfo pdssci;
	pdssci.depthTestEnable = VK_TRUE;
	pdssci.depthWriteEnable = VK_TRUE;
	pdssci.depthCompareOp = vk::CompareOp::eGreater;
	pdssci.depthBoundsTestEnable = VK_FALSE;
	pdssci.stencilTestEnable = VK_FALSE;
	pdssci.front.compareOp = vk::CompareOp::eAlways;
	pdssci.back.compareOp = vk::CompareOp::eAlways;
	pdssci.minDepthBounds = 0.0f;
	pdssci.maxDepthBounds = 0.0f;
	pipeline.pipeinfo.pDepthStencilState = &pdssci;

	// Color Blending state
	VulkanContext::get()->swapchain->images[0].blentAttachment.blendEnable = VK_TRUE;
	std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments = {
		VulkanContext::get()->swapchain->images[0].blentAttachment
	};
	vk::PipelineColorBlendStateCreateInfo pcbsci;
	pcbsci.logicOpEnable = VK_FALSE;
	pcbsci.logicOp = vk::LogicOp::eCopy;
	pcbsci.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
	pcbsci.pAttachments = colorBlendAttachments.data();
	float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	memcpy(pcbsci.blendConstants, blendConstants, 4 * sizeof(float));
	pipeline.pipeinfo.pColorBlendState = &pcbsci;

	// Dynamic state
	std::vector<vk::DynamicState> dynamicStates{ vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	vk::PipelineDynamicStateCreateInfo dsi;
	dsi.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
	dsi.pDynamicStates = dynamicStates.data();
	pipeline.pipeinfo.pDynamicState = &dsi;

	// Push Constant Range
	vk::PushConstantRange pcr;
	pcr.stageFlags = vk::ShaderStageFlagBits::eVertex;
	pcr.size = sizeof(float) * 4;

	// Pipeline Layout
	std::vector<vk::DescriptorSetLayout> descriptorSetLayouts{ getDescriptorSetLayout(VulkanContext::get()->device) };
	vk::PipelineLayoutCreateInfo plci;
	plci.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	plci.pSetLayouts = descriptorSetLayouts.data();
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pcr;
	pipeline.pipeinfo.layout = VulkanContext::get()->device.createPipelineLayout(plci);

	// Render Pass
	pipeline.pipeinfo.renderPass = renderPass;

	// Subpass
	pipeline.pipeinfo.subpass = 0;

	// Base Pipeline Handle
	pipeline.pipeinfo.basePipelineHandle = nullptr;

	// Base Pipeline Index
	pipeline.pipeinfo.basePipelineIndex = -1;

	pipeline.pipeline = VulkanContext::get()->device.createGraphicsPipelines(nullptr, pipeline.pipeinfo).at(0);

	// destroy Shader Modules
	VulkanContext::get()->device.destroyShaderModule(vertModule);
	VulkanContext::get()->device.destroyShaderModule(fragModule);
}

void GUI::destroy()
{
	Object::destroy();
	if (renderPass) {
		VulkanContext::get()->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
	}
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			VulkanContext::get()->device.destroyFramebuffer(frameBuffer);
		}
	}
	pipeline.destroy();
	if (GUI::descriptorSetLayout) {
		VulkanContext::get()->device.destroyDescriptorSetLayout(GUI::descriptorSetLayout);
		GUI::descriptorSetLayout = nullptr;
	}
	if (fenceUpload) {
		VulkanContext::get()->device.destroyFence(fenceUpload);
		fenceUpload = nullptr;
	}
}

void GUI::update()
{
	if (render)
		newFrame();
}
