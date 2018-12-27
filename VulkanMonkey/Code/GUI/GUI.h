#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../../include/imgui/imgui.h"
#include "../Console/Console.h"
#include "../Object/Object.h"
#include "../Surface/Surface.h"
#include "../Pipeline/Pipeline.h"
#include "../../include/SDL.h"
#include <vector>

namespace vm {
	struct GUI : Object
	{
		// Data
		static ImVec2		winPos;
		static ImVec2		winSize;
		static bool			p_open;
		static bool			console_open;
		static bool			lock_render_window;
		static bool			deferred_rendering;
		static bool			show_ssr;
		static bool			show_ssao;
		static bool			show_motionBlur;
		static bool			shadow_cast;
		static bool			render_models;
		static bool			randomize_lights;
		static int			fps;
		static float		cameraSpeed;
		static std::array<float, 3>	depthBias;
		static std::array<float, 4>	clearColor;
		static float		cpuTime;
		static float		cpuWaitingTime;
		static float		gpuTime;

		static SDL_Window*  g_Window;
		static Uint64       g_Time;
		static bool         g_MousePressed[3];
		static SDL_Cursor*  g_MouseCursors[ImGuiMouseCursor_COUNT];
		static char*        g_ClipboardTextData;
		bool				show_demo_window = false;
		static const char*	ImGui_ImplSDL2_GetClipboardText(void*);
		static void			ImGui_ImplSDL2_SetClipboardText(void*, const char* text);
		void				initImGui();
		void				newFrame();

		std::string	name;
		vk::RenderPass renderPass;
		std::vector<vk::Framebuffer> frameBuffers{};
		Pipeline pipeline;
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		void loadGUI(const std::string textureName = "", bool show = true);
		void draw(vk::RenderPass renderPass, vk::Framebuffer guiFrameBuffer, Pipeline& pipeline, const vk::CommandBuffer & cmd);
		void windowStyle(ImGuiStyle* dst = nullptr);
		void setWindows();
		void createVertexBuffer(size_t vertex_size);
		void createIndexBuffer(size_t index_size);
		void createDescriptorSet(vk::DescriptorSetLayout & descriptorSetLayout);
		void destroy();
	};
}