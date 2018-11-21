#pragma once
#include "VulkanContext.h"
#include "imgui/imgui.h"
#include "Object.h"
#include "Surface.h"
#include "Pipeline.h"
#include "SDL.h"
#include <vector>

namespace vm {
	struct GUI : Object
	{
		GUI(VulkanContext* vulkan);

		// Data
		static SDL_Window*  g_Window;
		static Uint64       g_Time;
		static bool         g_MousePressed[3];
		static SDL_Cursor*  g_MouseCursors[ImGuiMouseCursor_COUNT];
		static char*        g_ClipboardTextData;
		bool				show_demo_window = false;
		static const char*	ImGui_ImplSDL2_GetClipboardText(void*);
		static void			ImGui_ImplSDL2_SetClipboardText(void*, const char* text);
		void				initImGui();
		void				newFrame(vk::Device device, vk::PhysicalDevice gpu, SDL_Window* window);

		std::string	name;
		vk::RenderPass renderPass;
		std::vector<vk::Framebuffer> frameBuffers{};
		Pipeline pipeline = Pipeline(vulkan);
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		void loadGUI(const std::string textureName, bool show = true);
		void draw(vk::RenderPass renderPass, vk::Framebuffer guiFrameBuffer, Pipeline& pipeline, const vk::CommandBuffer & cmd);
		void createVertexBuffer(size_t vertex_size);
		void createIndexBuffer(size_t index_size);
		void createDescriptorSet(vk::DescriptorSetLayout & descriptorSetLayout);
		void destroy();
	};
}