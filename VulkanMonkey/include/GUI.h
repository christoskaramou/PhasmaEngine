#pragma once
#include "Vulkan.h"
#include "imgui/imgui.h"
#include "Object.h"
#include "Surface.h"
#include "Pipeline.h"
#include "SDL.h"

struct GUI : Object
{
	// Data
	static SDL_Window*  g_Window;
	static Uint64       g_Time;
	static bool         g_MousePressed[3];
	static SDL_Cursor*  g_MouseCursors[ImGuiMouseCursor_COUNT];
	static char*        g_ClipboardTextData;
	bool				show_demo_window = false;
	static const char*	ImGui_ImplSDL2_GetClipboardText(void*);
	static void			ImGui_ImplSDL2_SetClipboardText(void*, const char* text);
	void				initImGui(vk::Device device, vk::PhysicalDevice gpu, vk::CommandBuffer dynamicCmdBuffer, vk::Queue graphicsQueue, vk::DescriptorPool descriptorPool, SDL_Window* window);
	void				newFrame(vk::Device device, vk::PhysicalDevice gpu, SDL_Window* window);

	std::string	name;
	static vk::DescriptorSetLayout descriptorSetLayout;
	static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
	static GUI loadGUI(vk::Device device, vk::PhysicalDevice gpu, vk::CommandBuffer dynamicCmdBuffer, vk::Queue graphicsQueue, vk::DescriptorPool descriptorPool, SDL_Window* window, const std::string textureName, bool show = true);
	void draw(vk::RenderPass guiRenderPass, vk::Framebuffer guiFrameBuffer, Surface& surface, Pipeline& pipelineGUI, const vk::CommandBuffer & cmd);
	void createVertexBuffer(vk::Device device, vk::PhysicalDevice gpu, size_t vertex_size);
	void createIndexBuffer(vk::Device device, vk::PhysicalDevice gpu, size_t index_size);
	void createDescriptorSet(vk::Device device, vk::DescriptorPool descriptorPool, vk::DescriptorSetLayout & descriptorSetLayout);
};