#include "GUI.h"
#include <filesystem>

using namespace vm;
ImVec2						GUI::winPos = ImVec2();
ImVec2						GUI::winSize = ImVec2();
bool						GUI::lock_render_window = true;
bool						GUI::show_ssr = true;
bool						GUI::show_ssao = true;
bool						GUI::show_motionBlur = true;
bool						GUI::shadow_cast = true;
bool						GUI::randomize_lights = false;
std::array<float, 3>		GUI::sun_position = { 0.0f, 300.0f, 50.0f };
int							GUI::fps = 60;
float						GUI::cameraSpeed = 3.5f;
std::array<float, 3>		GUI::depthBias = { 0.0f, 0.0f, -6.2f };
std::array<float, 4>		GUI::clearColor = { 0.0f, 0.31f, 0.483f, 0.0f };
float						GUI::cpuTime = 0;
float						GUI::cpuWaitingTime = 0;
float						GUI::gpuTime = 0;
float						GUI::timeScale = 1.f;
std::vector<std::string>	GUI::fileList{};
std::vector<std::string>	GUI::modelList{};
ImVec2						GUI::tlPanelPos = ImVec2();
ImVec2						GUI::tlPanelSize = ImVec2();
ImVec2						GUI::mlPanelPos = ImVec2();
ImVec2						GUI::mlPanelSize = ImVec2();

vk::DescriptorSetLayout		GUI::descriptorSetLayout = nullptr;
SDL_Window*					GUI::g_Window = nullptr;
Uint64						GUI::g_Time = 0;
bool						GUI::g_MousePressed[3] = { false, false, false };
SDL_Cursor*					GUI::g_MouseCursors[ImGuiMouseCursor_COUNT] = { 0 };
char*						GUI::g_ClipboardTextData = nullptr;

bool endsWithExt(const std::string &mainStr, const std::string &toMatch)
{
	if (mainStr.size() >= toMatch.size() &&
		mainStr.compare(mainStr.size() - toMatch.size(), toMatch.size(), toMatch) == 0)
		return true;
	else
		return false;
}

void GUI::setWindows()
{
	showMetrics();
	showOptions();
	showConsole();
	showScripts();
	showModels();
	showRenderingWindow();
}

void GUI::showMetrics()
{
	static bool metrics_open = true;
	ImGui::SetNextWindowPos(ImVec2(0.f, 1.f));
	ImGui::SetNextWindowSize(ImVec2(282.f, 111.f));
	ImGui::Begin("Metrics", &metrics_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGuiIO& io = ImGui::GetIO();
	ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
	ImGui::Text("Average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
	ImGui::Text("CPU: %.3f (waited %.3f) ms/frame", cpuTime, cpuWaitingTime);
	ImGui::Text("GPU: %.3f ms/frame", gpuTime);
	ImGui::Separator();
	tlPanelPos = ImGui::GetWindowPos();
	tlPanelSize = ImGui::GetWindowSize();
	ImGui::End();
}

void GUI::showOptions()
{

	static bool	options_open = true;
	ImGui::SetNextWindowPos(ImVec2(0.f, tlPanelSize.y));
	ImGui::SetNextWindowSize(ImVec2(tlPanelSize.x, HEIGHT_f - 279.f - tlPanelSize.y));
	ImGui::Begin("Options", &options_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGui::Checkbox("Lock Render Window", &lock_render_window);
	ImGui::Checkbox("SSR", &show_ssr);
	ImGui::Checkbox("SSAO", &show_ssao);
	ImGui::Checkbox("Motion Blur", &show_motionBlur);
	ImGui::Checkbox("Sun Light", &shadow_cast);
	ImGui::Separator();
	if (ImGui::Button("Randomize Lights"))
		randomize_lights = true;
	ImGui::Separator();
	ImGui::InputFloat3("Sun Position", (float*)sun_position.data(), 1);
	{
		vec3 sunDist((float*)&sun_position);
		if (length(sunDist) > 400.f) {
			sunDist = 400.f * normalize(sunDist);
			sun_position[0] = sunDist.x;
			sun_position[1] = sunDist.y;
			sun_position[2] = sunDist.z;
		}
	}
	ImGui::InputInt("FPS", &fps, 1, 15); if (fps < 10) fps = 10;
	ImGui::InputFloat("Camera Speed", &cameraSpeed, 0.1f, 1.f, 3);
	ImGui::SliderFloat4("Clear Color", (float*)clearColor.data(), 0.0f, 1.0f);
	ImGui::InputFloat("Depth Bias", &depthBias[0], 0.00001f, 0.0002f, 5);
	ImGui::InputFloat("Depth Clamp", &depthBias[1], 0.00001f, 0.0002f, 5);
	ImGui::InputFloat("Depth Slope", &depthBias[2], 0.15f, 0.5f, 5);
	ImGui::InputFloat("Time Scale", &timeScale, 0.05f, 0.2f);
	mlPanelPos = ImGui::GetWindowPos();
	mlPanelSize = ImGui::GetWindowSize();
	ImGui::End();
}

void GUI::showConsole()
{
	static bool console_open = true;
	static Console console;
	console.Draw("Console", &console_open, ImVec2(0.f, HEIGHT_f - 279.f), ImVec2(WIDTH_f / 3.f, 279.f));
}

void GUI::showScripts()
{
	static bool scripts_open = true;
	ImGui::SetNextWindowPos(ImVec2(WIDTH_f / 3.f, HEIGHT_f - 279.f));
	ImGui::SetNextWindowSize(ImVec2(WIDTH_f / 3.f, 279.f));
	ImGui::Begin("Scripts Folder", &scripts_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	for (auto& file : fileList)
	{
		if (endsWithExt(file, ".cs"))
			ImGui::TextColored(ImVec4(.5f, 0.f, .5f, 1.f), file.c_str());
		else
			ImGui::TextColored(ImVec4(.3f, .3f, .3f, 1.f), file.c_str());
	}
	ImGui::End();
}

void GUI::showModels()
{
	static bool models_open = true;
	ImGui::SetNextWindowPos(ImVec2(WIDTH_f * 2.f / 3.f, HEIGHT_f - 279.f));
	ImGui::SetNextWindowSize(ImVec2(WIDTH_f / 3.f, 279.f));
	ImGui::Begin("Models Loaded", &models_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	for (auto& model : modelList)
		ImGui::Text(model.c_str());
	ImGui::End();
}

void GUI::showRenderingWindow()
{
	static bool active = true;
	ImGuiStyle* style = &ImGui::GetStyle();
	style->Colors[ImGuiCol_WindowBg].w = 0.0f;
	int flags = ImGuiWindowFlags_NoTitleBar;
	if (lock_render_window) {
		flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		ImGui::SetNextWindowPos(ImVec2(mlPanelSize.x, 0.f));
		ImGui::SetNextWindowSize(ImVec2(WIDTH_f - tlPanelSize.x, HEIGHT_f - 279.f));
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
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};

		// binding for gui texture
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access


		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
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
	for (auto& file : std::filesystem::directory_iterator("Scripts")) {
		auto pathStr = file.path().string();
		fileList.push_back(pathStr.substr(pathStr.rfind('\\') + 1).c_str());
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
	io.ClipboardUserData = NULL;

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

	auto beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
		.setPInheritanceInfo(nullptr);
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
		void* map = vulkan->device.mapMemory(stagingBuffer.memory, 0, upload_size);
		memcpy(map, pixels, upload_size);
		vk::MappedMemoryRange range;
		range.memory = stagingBuffer.memory;
		range.size = upload_size;
		vulkan->device.flushMappedMemoryRanges(range);
		vulkan->device.unmapMemory(stagingBuffer.memory);
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
		vulkan->dynamicCmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags(), nullptr, nullptr, copy_barrier);

		vk::BufferImageCopy region = {};
		region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.imageSubresource.layerCount = 1;
		region.imageExtent.width = width;
		region.imageExtent.height = height;
		region.imageExtent.depth = 1;
		vulkan->dynamicCmdBuffer.copyBufferToImage(stagingBuffer.buffer, texture.image, vk::ImageLayout::eTransferDstOptimal, 1, &region);

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
		vulkan->dynamicCmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlags(), nullptr, nullptr, use_barrier);
	}

	// Store our identifier
	io.Fonts->TexID = (ImTextureID)(intptr_t)(VkImage)texture.image;

	vk::SubmitInfo end_info = {};
	end_info.commandBufferCount = 1;
	end_info.pCommandBuffers = &vulkan->dynamicCmdBuffer;
	vulkan->dynamicCmdBuffer.end();
	vulkan->graphicsQueue.submit(end_info, nullptr);

	vulkan->device.waitIdle();
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

void GUI::draw(uint32_t imageIndex)
{
	auto draw_data = ImGui::GetDrawData();
	if (render && draw_data->TotalVtxCount > 0)
	{
		std::vector<vk::ClearValue> clearValues = {
			vk::ClearColorValue().setFloat32(GUI::clearColor),
			vk::ClearDepthStencilValue({ 1.0f, 0 }) };
		auto renderPassInfo2 = vk::RenderPassBeginInfo()
			.setRenderPass(renderPass)
			.setFramebuffer(frameBuffers[imageIndex])
			.setRenderArea({ { 0, 0 }, vulkan->surface->actualExtent })
			.setClearValueCount(static_cast<uint32_t>(clearValues.size()))
			.setPClearValues(clearValues.data());

		auto& cmd = vulkan->dynamicCmdBuffer;
		cmd.beginRenderPass(renderPassInfo2, vk::SubpassContents::eInline);

		vk::DeviceSize offset{ 0 };
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, descriptorSet, nullptr);
		cmd.bindVertexBuffers(0, vertexBuffer.buffer, offset);
		cmd.bindIndexBuffer(indexBuffer.buffer, 0, vk::IndexType::eUint16);

		vk::Viewport viewport{
			0,							// viewport.x =
			0,							// viewport.y =
			draw_data->DisplaySize.x,	// viewport.width =
			draw_data->DisplaySize.y,	// viewport.height =
			0.0f,						// viewport.minDepth =
			1.0f						// viewport.maxDepth =
		};
		cmd.setViewport(0, 1, &viewport);

		float data[4];
		data[0] = 2.0f / draw_data->DisplaySize.x;				// scale
		data[1] = 2.0f / draw_data->DisplaySize.y;				// scale
		data[2] = -1.0f - draw_data->DisplayPos.x * data[0];	// transform
		data[3] = -1.0f - draw_data->DisplayPos.y * data[1];	// transform
		cmd.pushConstants(pipeline.pipeinfo.layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(float) * 4, data);

		// Render the command lists:
		int vtx_offset = 0;
		int idx_offset = 0;
		ImVec2 display_pos = draw_data->DisplayPos;
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
					scissor.offset.x = (int32_t)(pcmd->ClipRect.x - display_pos.x) > 0 ? (int32_t)(pcmd->ClipRect.x - display_pos.x) : 0;
					scissor.offset.y = (int32_t)(pcmd->ClipRect.y - display_pos.y) > 0 ? (int32_t)(pcmd->ClipRect.y - display_pos.y) : 0;
					scissor.extent.width = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
					scissor.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y + 1); // FIXME: Why +1 here?
					cmd.setScissor(0, 1, &scissor);

					// Draw
					cmd.drawIndexed(pcmd->ElemCount, 1, idx_offset, vtx_offset, 0);
				}
				idx_offset += pcmd->ElemCount;
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
	SDL_GetWindowSize(vulkan->window, &w, &h);
	SDL_GL_GetDrawableSize(vulkan->window, &display_w, &display_h);
	io.DisplaySize = ImVec2((float)w, (float)h);
	io.DisplayFramebufferScale = ImVec2(w > 0 ? ((float)display_w / w) : 0, h > 0 ? ((float)display_h / h) : 0);

	// Setup time step (we don't use SDL_GetTicks() because it is using millisecond resolution)
	static Uint64 frequency = SDL_GetPerformanceFrequency();
	Uint64 current_time = SDL_GetPerformanceCounter();
	io.DeltaTime = g_Time > 0 ? (float)((double)(current_time - g_Time) / frequency) : (float)(1.0f / 60.0f);
	g_Time = current_time;

	// Set OS mouse position if requested (rarely used, only when ImGuiConfigFlags_NavEnableSetMousePos is enabled by user)
	if (io.WantSetMousePos)
		SDL_WarpMouseInWindow(g_Window, (int)io.MousePos.x, (int)io.MousePos.y);
	else
		io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);

	int mx, my;
	Uint32 mouse_buttons = SDL_GetMouseState(&mx, &my);
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
		io.MousePos = ImVec2((float)mx, (float)my);
	}

	// SDL_CaptureMouse() let the OS know e.g. that our imgui drag outside the SDL window boundaries shouldn't e.g. trigger the OS window resize cursor. 
	// The function is only supported from SDL 2.0.4 (released Jan 2016)
	bool any_mouse_button_down = ImGui::IsAnyMouseDown();
	SDL_CaptureMouse(any_mouse_button_down ? SDL_TRUE : SDL_FALSE);
#else
	if (SDL_GetWindowFlags(g_Window) & SDL_WINDOW_INPUT_FOCUS)
		io.MousePos = ImVec2((float)mx, (float)my);
#endif
	if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
		return;

	ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
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

	auto draw_data = ImGui::GetDrawData();
	if (draw_data->TotalVtxCount < 1)
		return;
	size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
	size_t index_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
	if (!vertexBuffer.buffer || vertexBuffer.size < vertex_size)
		createVertexBuffer(vertex_size);
	if (!indexBuffer.buffer || indexBuffer.size < index_size)
		createIndexBuffer(index_size);

	// Upload Vertex and index Data:
	{
		ImDrawVert* vtx_dst = NULL;
		ImDrawIdx* idx_dst = NULL;
		vtx_dst = (ImDrawVert*)vulkan->device.mapMemory(vertexBuffer.memory, 0, vertex_size);
		idx_dst = (ImDrawIdx*)vulkan->device.mapMemory(indexBuffer.memory, 0, index_size);
		for (int n = 0; n < draw_data->CmdListsCount; n++)
		{
			const ImDrawList* cmd_list = draw_data->CmdLists[n];
			memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
			vtx_dst += cmd_list->VtxBuffer.Size;
			idx_dst += cmd_list->IdxBuffer.Size;
		}
		vk::MappedMemoryRange range[2] = {};
		range[0].memory = vertexBuffer.memory;
		range[0].size = VK_WHOLE_SIZE;
		range[1].memory = indexBuffer.memory;
		range[1].size = VK_WHOLE_SIZE;
		vulkan->device.flushMappedMemoryRanges(2, range);
		vulkan->device.unmapMemory(vertexBuffer.memory);
		vulkan->device.unmapMemory(indexBuffer.memory);
	}
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
	style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
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
	vertexBuffer.destroy();
	vertexBuffer.createBuffer(vertex_size, vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
}

void GUI::createIndexBuffer(size_t index_size)
{
	indexBuffer.destroy();
	indexBuffer.createBuffer(index_size, vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
}

void GUI::createDescriptorSet(vk::DescriptorSetLayout & descriptorSetLayout)
{
	auto const allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&descriptorSetLayout);
	descriptorSet = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);


	// texture sampler
	vk::WriteDescriptorSet textureWriteSets = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)										// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(texture.sampler)									// Sampler sampler;
			.setImageView(texture.view)										// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;
	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);
}

void GUI::destroy()
{
	Object::destroy();
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
	}
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	pipeline.destroy();
	if (GUI::descriptorSetLayout) {
		vulkan->device.destroyDescriptorSetLayout(GUI::descriptorSetLayout);
		GUI::descriptorSetLayout = nullptr;
	}
}

void vm::GUI::update()
{
	if (render)
		newFrame();
}
