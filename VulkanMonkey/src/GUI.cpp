#include "../include/GUI.h"
#include "../include/Errors.h"
#include <iostream>

SDL_Window*					GUI::g_Window = nullptr;
Uint64						GUI::g_Time = 0;
bool						GUI::g_MousePressed[3] = { false, false, false };
SDL_Cursor*					GUI::g_MouseCursors[ImGuiMouseCursor_COUNT] = { 0 };
char*						GUI::g_ClipboardTextData = nullptr;
vk::DescriptorSetLayout		GUI::descriptorSetLayout = nullptr;

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
		VkCheck(device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
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

void GUI::initImGui(vk::Device device, vk::PhysicalDevice gpu, vk::CommandBuffer dynamicCmdBuffer, vk::Queue graphicsQueue, vk::DescriptorPool descriptorPool, SDL_Window* window)
{
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	g_Window = window;

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
	SDL_GetWindowWMInfo(window, &wmInfo);
	io.ImeWindowHandle = wmInfo.info.win.window;
#else
	(void)window;
#endif
	ImGui::StyleColorsDark();

	auto beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
		.setPInheritanceInfo(nullptr);
	VkCheck(dynamicCmdBuffer.begin(&beginInfo));

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
		texture.createImage(device, gpu, width, height, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

		texture.viewType = vk::ImageViewType::e2D;
		texture.createImageView(device, vk::ImageAspectFlagBits::eColor);

		texture.addressMode = vk::SamplerAddressMode::eRepeat;
		texture.maxAnisotropy = 1.f;
		texture.minLod = -1000.f;
		texture.maxLod = 1000.f;
		texture.createSampler(device);

		createDescriptorSet(device, descriptorPool, getDescriptorSetLayout(device));
	}
	// Create the and Upload to Buffer:
	Buffer stagingBuffer;
	{
		stagingBuffer.createBuffer(device, gpu, upload_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible);
		void* map;
		VkCheck(device.mapMemory(stagingBuffer.memory, 0, upload_size, vk::MemoryMapFlags(), &map));
		memcpy(map, pixels, upload_size);
		vk::MappedMemoryRange range[1] = {};
		range[0].memory = stagingBuffer.memory;
		range[0].size = upload_size;
		VkCheck(device.flushMappedMemoryRanges(1, range));
		device.unmapMemory(stagingBuffer.memory);
	}

	// Copy to Image:
	{
		vk::ImageMemoryBarrier copy_barrier[1] = {};
		copy_barrier[0].dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		copy_barrier[0].oldLayout = vk::ImageLayout::eUndefined;
		copy_barrier[0].newLayout = vk::ImageLayout::eTransferDstOptimal;
		copy_barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copy_barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copy_barrier[0].image = texture.image;
		copy_barrier[0].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		copy_barrier[0].subresourceRange.levelCount = 1;
		copy_barrier[0].subresourceRange.layerCount = 1;
		dynamicCmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, copy_barrier);

		vk::BufferImageCopy region = {};
		region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.imageSubresource.layerCount = 1;
		region.imageExtent.width = width;
		region.imageExtent.height = height;
		region.imageExtent.depth = 1;
		dynamicCmdBuffer.copyBufferToImage(stagingBuffer.buffer, texture.image, vk::ImageLayout::eTransferDstOptimal, 1, &region);

		vk::ImageMemoryBarrier use_barrier[1] = {};
		use_barrier[0].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		use_barrier[0].dstAccessMask = vk::AccessFlagBits::eShaderRead;
		use_barrier[0].oldLayout = vk::ImageLayout::eTransferDstOptimal;
		use_barrier[0].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		use_barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		use_barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		use_barrier[0].image = texture.image;
		use_barrier[0].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		use_barrier[0].subresourceRange.levelCount = 1;
		use_barrier[0].subresourceRange.layerCount = 1;
		dynamicCmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, use_barrier);
	}

	// Store our identifier
	io.Fonts->TexID = (ImTextureID)(intptr_t)(VkImage)texture.image;

	vk::SubmitInfo end_info = {};
	end_info.commandBufferCount = 1;
	end_info.pCommandBuffers = &dynamicCmdBuffer;
	VkCheck(dynamicCmdBuffer.end());
	graphicsQueue.submit(1, &end_info, nullptr);

	device.waitIdle();
	stagingBuffer.destroy(device);
}

void GUI::newFrame(vk::Device device, vk::PhysicalDevice gpu, SDL_Window* window)
{
	ImGuiIO& io = ImGui::GetIO();
	IM_ASSERT(io.Fonts->IsBuilt());     // Font atlas needs to be built, call renderer _NewFrame() function e.g. ImGui_ImplOpenGL3_NewFrame() 

	// Setup display size (every frame to accommodate for window resizing)
	int w, h;
	int display_w, display_h;
	SDL_GetWindowSize(window, &w, &h);
	SDL_GL_GetDrawableSize(window, &display_w, &display_h);
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

	if (show_demo_window)
		ImGui::ShowDemoWindow(&show_demo_window);

	ImGui::Render();

	auto draw_data = ImGui::GetDrawData();
	if (draw_data->TotalVtxCount < 1)
		return;
	size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
	size_t index_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
	if (!vertexBuffer.buffer || vertexBuffer.size < vertex_size)
		createVertexBuffer(device, gpu, vertex_size);
	if (!indexBuffer.buffer || indexBuffer.size < index_size)
		createIndexBuffer(device, gpu, index_size);

	// Upload Vertex and index Data:
	{
		ImDrawVert* vtx_dst = NULL;
		ImDrawIdx* idx_dst = NULL;
		VkCheck(device.mapMemory(vertexBuffer.memory, 0, vertex_size, vk::MemoryMapFlags(), (void**)(&vtx_dst)));
		VkCheck(device.mapMemory(indexBuffer.memory, 0, index_size, vk::MemoryMapFlags(), (void**)(&idx_dst)));
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
		VkCheck(device.flushMappedMemoryRanges(2, range));
		device.unmapMemory(vertexBuffer.memory);
		device.unmapMemory(indexBuffer.memory);
	}
}

GUI GUI::loadGUI(vk::Device device, vk::PhysicalDevice gpu, vk::CommandBuffer dynamicCmdBuffer, vk::Queue graphicsQueue, vk::DescriptorPool descriptorPool, SDL_Window* window, const std::string textureName, bool show)
{
	GUI _gui;

	if (textureName == "ImGuiDemo")
		_gui.show_demo_window = true;

	//					 counter clock wise
	// x, y, z coords orientation	// u, v coords orientation
	//			|  /|				// (0,0)-------------> u
	//			| /  +z				//	   |
	//			|/					//	   |
	//  --------|-------->			//	   |
	//		   /|		+x			//	   |
	//		  /	|					//	   |
	//	     /  |/ +y				//	   |/ v


	//_gui.vertices = {
	//	-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
	//	-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
	//	 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
	//	 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
	//	-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
	//	 1.0f,  1.0f, 0.0f, 1.0f, 1.0f
	//};
	//_gui.loadTexture(textureName, info);
	//_gui.createVertexBuffer(info, _gui.vertices.size());
	//_gui.createDescriptorSet(GUI::descriptorSetLayout, info);

	_gui.initImGui(device, gpu, dynamicCmdBuffer, graphicsQueue, descriptorPool, window);
	_gui.render = show;

	return _gui;
}

void GUI::draw(vk::RenderPass guiRenderPass, vk::Framebuffer guiFrameBuffer, Surface& surface, Pipeline& pipelineGUI, const vk::CommandBuffer & cmd)
{
	auto draw_data = ImGui::GetDrawData();
	if (render && draw_data->TotalVtxCount > 0)
	{
		std::vector<vk::ClearValue> clearValues = {
			vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
			vk::ClearDepthStencilValue({ 1.0f, 0 }) };
		auto renderPassInfo2 = vk::RenderPassBeginInfo()
			.setRenderPass(guiRenderPass)
			.setFramebuffer(guiFrameBuffer)
			.setRenderArea({ { 0, 0 }, surface.actualExtent })
			.setClearValueCount(static_cast<uint32_t>(clearValues.size()))
			.setPClearValues(clearValues.data());
		cmd.beginRenderPass(&renderPassInfo2, vk::SubpassContents::eInline);

		vk::DeviceSize offset{ 0 };
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineGUI.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineGUI.pipeinfo.layout, 0, 1, &descriptorSet, 0, nullptr);
		cmd.bindVertexBuffers(0, 1, &vertexBuffer.buffer, &offset);
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
		cmd.pushConstants(pipelineGUI.pipeinfo.layout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(float) * 4, data);

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

void GUI::createVertexBuffer(vk::Device device, vk::PhysicalDevice gpu, size_t vertex_size)
{
	vertexBuffer.destroy(device);
	vertexBuffer.createBuffer(device, gpu, vertex_size, vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
}

void GUI::createIndexBuffer(vk::Device device, vk::PhysicalDevice gpu, size_t index_size)
{
	indexBuffer.destroy(device);
	indexBuffer.createBuffer(device, gpu, index_size, vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
}

void GUI::createDescriptorSet(vk::Device device, vk::DescriptorPool descriptorPool, vk::DescriptorSetLayout & descriptorSetLayout)
{
	auto const allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&descriptorSetLayout);
	VkCheck(device.allocateDescriptorSets(&allocateInfo, &descriptorSet)); // why the handle of the vk::Image is changing with 2 dSets allocation????


	// texture sampler
	vk::WriteDescriptorSet textureWriteSets[1];
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)										// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(texture.sampler)									// Sampler sampler;
			.setImageView(texture.view)										// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;
	device.updateDescriptorSets(1, textureWriteSets, 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";
}