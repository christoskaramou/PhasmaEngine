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
	constexpr float LOWER_PANEL_HEIGHT = 150.f;
	constexpr float LEFT_PANEL_WIDTH = 250.f;
	constexpr float RIGHT_PANEL_WIDTH = 250.f;
	constexpr float MENU_HEIGHT = 19.f;
	struct GUI : Object
	{
		// Data
		static ImVec2		winPos;
		static ImVec2		winSize;
		static bool			lock_render_window;
		static bool			show_ssr;
		static bool			show_ssao;
		static bool			show_tonemapping;
		static float		exposure;
		static bool			show_FXAA;
		static bool			show_Bloom;
		static float		Bloom_Inv_brightness;
		static float		Bloom_intensity;
		static float		Bloom_range;
		static bool			use_tonemap;
		static float		Bloom_exposure;
		static bool			dSetNeedsUpdate;
		static bool			show_motionBlur;
		static bool			randomize_lights;
		static float		lights_intensity;
		static float		lights_range;
		static bool			shadow_cast;
		static float		sun_intensity;
		static std::array<float, 3>	sun_position;
		static int			fps;
		static float		cameraSpeed;
		static std::array<float, 3>	depthBias;
		static std::array<float, 4>	clearColor;
		static float		cpuTime;
		static float		cpuWaitingTime;
		static float		gpuTime;
		static float		timeScale;
		static std::array<float, 20> metrics;
		static std::array<float, 20> stats;
		static std::vector<std::string> fileList;
		static std::vector<std::string> modelList;
		static int			modelItemSelected;
		static std::vector <std::array<float, 3>> model_scale;
		static std::vector <std::array<float, 3>> model_pos;
		static std::vector <std::array<float, 3>> model_rot;
		static ImVec2		tlPanelPos;
		static ImVec2		tlPanelSize;
		static ImVec2		mlPanelPos;
		static ImVec2		mlPanelSize;

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
		void update();
		void loadGUI(bool show = true);
		void draw(uint32_t imageIndex);
		void windowStyle(ImGuiStyle* dst = nullptr);
		void setWindows();
		void LeftPanel();
		void RightPanel();
		void BottomPanel();
		void Menu();
		void Metrics();
		void ConsoleWindow();
		void Scripts();
		void Models();
		void Properties();
		void RenderingWindowBox();
		void createVertexBuffer(size_t vertex_size);
		void createIndexBuffer(size_t index_size);
		void createDescriptorSet(vk::DescriptorSetLayout & descriptorSetLayout) override;
		void destroy() override;
	};
}