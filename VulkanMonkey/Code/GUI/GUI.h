#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../../include/imgui/imgui.h"
#include "../Console/Console.h"
#include "../Object/Object.h"
#include "../Surface/Surface.h"
#include "../Pipeline/Pipeline.h"
#include "../../include/SDL.h"
#include "../Vertex/Vertex.h"
#include "../Swapchain/Swapchain.h"
#include <vector>
#include <map>
#include <functional>

namespace vm {
	constexpr float LOWER_PANEL_HEIGHT = 150.f;
	constexpr float LEFT_PANEL_WIDTH = 250.f;
	constexpr float RIGHT_PANEL_WIDTH = 250.f;
	constexpr float MENU_HEIGHT = 19.f;
	struct GUI : Object
	{
		// Data
		static inline ImVec2								winPos = ImVec2();
		static inline ImVec2								winSize = ImVec2();
		static inline float									renderTargetsScale = 1.0f;//0.71f;
		static inline bool									lock_render_window = true;
		static inline bool									use_IBL = true;
		static inline bool									show_ssr = false;
		static inline bool									show_ssao = false;
		static inline bool									show_tonemapping = false;
		static inline float									exposure = 4.5f;
		static inline bool									use_AntiAliasing = true;
		static inline bool									use_FXAA = false;
		static inline bool									use_TAA = false;
		static inline float									TAA_jitter_scale = 1.0f;
		static inline float									TAA_feedback_min = 0.01f;
		static inline float									TAA_feedback_max = 0.2f;
		static inline float									TAA_sharp_strength = 2.0f;
		static inline float									TAA_sharp_clamp = 0.35f;
		static inline float									TAA_sharp_offset_bias = 1.0f;
		static inline bool									show_Bloom = false;
		static inline float									Bloom_Inv_brightness = 20.0f;
		static inline float									Bloom_intensity = 1.5f;
		static inline float									Bloom_range = 2.5f;
		static inline bool									use_tonemap = false;
		static inline bool									use_compute = false;
		static inline float									Bloom_exposure = 3.5f;
		static inline bool									show_motionBlur = false;
		static inline float									motionBlur_strength = 1.0f;
		static inline bool									randomize_lights = false;
		static inline float									lights_intensity = 2.5f;
		static inline float									lights_range = 7.0f;
		static inline bool									shadow_cast = false;
		static inline float									sun_intensity = 7.f;
		static inline std::array<float, 3>					sun_position{ 0.0f, 300.0f, 50.0f };
		static inline int									fps = 60;
		static inline float									cameraSpeed = 3.5f;
		static inline std::array<float, 3>					depthBias{ 0.0f, 0.0f, -6.2f };
		static inline std::array<float, 4>					clearColor{ 0.0f, 0.0f, 0.0f, 1.0f };
		static inline float									cpuTime = 0;
		static inline float									cpuWaitingTime = 0;
		static inline float									gpuTime = 0;
		static inline float									timeScale = 1.f;
		static inline std::array<float, 20>					metrics = {};
		static inline std::array<float, 20>					stats = {};
		static inline std::vector<std::string>				fileList{};
		static inline std::vector<std::string>				modelList{};
		static inline std::vector <std::array<float, 3>>	model_scale{};
		static inline std::vector <std::array<float, 3>>	model_pos{};
		static inline std::vector <std::array<float, 3>>	model_rot{};
		static inline int									modelItemSelected = -1;
		static inline ImVec2								tlPanelPos = ImVec2();
		static inline ImVec2								tlPanelSize = ImVec2();
		static inline ImVec2								mlPanelPos = ImVec2();
		static inline ImVec2								mlPanelSize = ImVec2();
		static inline uint32_t								scaleRenderTargetsEventType = SDL_RegisterEvents(1);
		static inline SDL_Window*							g_Window = nullptr;
		static inline Uint64								g_Time = 0;
		static inline bool									g_MousePressed[3] = { false, false, false };
		static inline SDL_Cursor*							g_MouseCursors[ImGuiMouseCursor_COUNT] = { 0 };
		static inline char*									g_ClipboardTextData = nullptr;

		bool				show_demo_window = false;
		static const char*	ImGui_ImplSDL2_GetClipboardText(void*);
		static void			ImGui_ImplSDL2_SetClipboardText(void*, const char* text);
		void				initImGui();
		void				newFrame();

		std::string	name;
		vk::RenderPass renderPass;
		std::vector<vk::Framebuffer> frameBuffers{};
		vk::CommandBuffer cmdBuf;
		vk::Fence fenceUpload;
		Pipeline pipeline;
		static inline vk::DescriptorSetLayout descriptorSetLayout = nullptr;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		void update();
		void loadGUI(bool show = true);
		void scaleToRenderArea(vk::CommandBuffer cmd, Image& renderedImage, uint32_t imageIndex);
		void draw(vk::CommandBuffer cmd, uint32_t imageIndex);
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
		using Object::createVertexBuffer; void createVertexBuffer(size_t vertex_size); // using the base func so the derived func can not hide it, because they have the same name
		void createIndexBuffer(size_t index_size);
		void createDescriptorSet(vk::DescriptorSetLayout & descriptorSetLayout) override;
		void updateDescriptorSets();
		void createRenderPass();
		void createFrameBuffers();
		void createPipeline();
		void destroy() override;
	};
}