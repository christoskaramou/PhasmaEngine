#pragma once

#include "../VulkanContext/VulkanContext.h"
#include "../../include/SDL.h"
#include "../Math/Math.h"
#include "../Vertex/Vertex.h"
#include "../Image/Image.h"
#include "../Surface/Surface.h"
#include "../Swapchain/Swapchain.h"
#include "../Buffer/Buffer.h"
#include "../Pipeline/Pipeline.h"
#include "../Object/Object.h"
#include "../GUI/GUI.h"
#include "../Skybox/Skybox.h"
#include "../Terrain/Terrain.h"
#include "../Shadows/Shadows.h"
#include "../Light/Light.h"
#include "../Mesh/Mesh.h"
#include "../Model/Model.h"
#include "../Camera/Camera.h"
#include "../SSAO/SSAO.h"
#include "../SSR/SSR.h"
#include "../MotionBlur/MotionBlur.h"
#include "../Deferred/Deferred.h"
#include "../Compute/Compute.h"
#include "../Metrics/Metrics.h"
#include "../Script/Script.h"
#include <vector>
#include <tuple>
#include <map>

namespace vm {
	struct Context
	{
	public:
		// VULKAN CONTEXT
		VulkanContext &vulkan = VulkanContext::getVulkanContext();

		// RENDER TARGETS
		std::map<std::string, Image> renderTargets{};

		// MODELS
		//static std::vector<Model> models;

		// SHADOWS
		Shadows shadows;

		// COMPUTE
		Compute compute;

		// DEFERRED
		Deferred deferred;

		// SSAO
		SSAO ssao;

		// SSR
		SSR ssr;

		// MOTION BLUR
		MotionBlur motionBlur;

		// SKYBOX
		SkyBox skyBox;

		// TERRAIN
		Terrain terrain;

		// GUI
		GUI gui;

		// LIGHTS
		LightUniforms lightUniforms;

		// MAIN CAMERA
		Camera camera_main;

		// CAMERAS
		std::vector<Camera> camera{};

		// Metrics
		Metrics metrics;

		// Scripts
		std::vector<std::unique_ptr<Script>> scripts{};

		void initVulkanContext();
		void initRendering();
		void loadResources();
		void createUniforms();
		void destroyVkContext();
		void resizeViewport(uint32_t width, uint32_t height);

		PipelineInfo getPipelineSpecificationsShadows();
		PipelineInfo getPipelineSpecificationsSkyBox();
		PipelineInfo getPipelineSpecificationsTerrain();
		PipelineInfo getPipelineSpecificationsGUI();
		PipelineInfo getPipelineSpecificationsDeferred();

		vk::DescriptorSetLayout getDescriptorSetLayoutLights();

	private:
		vk::Instance createInstance();
		Surface createSurface();
		int getGraphicsFamilyId();
		int getPresentFamilyId();
		int getComputeFamilyId();
		vk::PhysicalDevice findGpu();
		vk::PhysicalDeviceProperties getGPUProperties();
		vk::PhysicalDeviceFeatures getGPUFeatures();
		vk::SurfaceCapabilitiesKHR getSurfaceCapabilities();
		vk::SurfaceFormatKHR getSurfaceFormat();
		vk::PresentModeKHR getPresentationMode();
		vk::Device createDevice();
		vk::Queue getGraphicsQueue();
		vk::Queue getPresentQueue();
		vk::Queue getComputeQueue();
		Swapchain createSwapchain();
		vk::CommandPool createCommandPool();
		vk::CommandPool createComputeCommadPool();
		void addRenderTarget(const std::string& name, vk::Format format);
		vk::RenderPass createRenderPass();
		vk::RenderPass createDeferredRenderPass();
		vk::RenderPass createCompositionRenderPass();
		vk::RenderPass createSSAORenderPass();
		vk::RenderPass createSSAOBlurRenderPass();
		vk::RenderPass createSSRRenderPass();
		vk::RenderPass createMotionBlurRenderPass();
		vk::RenderPass createGUIRenderPass();
		vk::RenderPass createShadowsRenderPass();
		vk::RenderPass createSkyboxRenderPass();
		Image createDepthResources();
		std::vector<vk::Framebuffer> createFrameBuffers();
		std::vector<vk::Framebuffer> createDeferredFrameBuffers();
		std::vector<vk::Framebuffer> createCompositionFrameBuffers();
		std::vector<vk::Framebuffer> createSSRFrameBuffers();
		std::vector<vk::Framebuffer> createSSAOFrameBuffers();
		std::vector<vk::Framebuffer> createSSAOBlurFrameBuffers();
		std::vector<vk::Framebuffer> createMotionBlurFrameBuffers();
		std::vector<vk::Framebuffer> createGUIFrameBuffers();
		std::vector<vk::Framebuffer> createShadowsFrameBuffers();
		std::vector<vk::Framebuffer> createSkyboxFrameBuffers();
		std::vector<vk::CommandBuffer> createCmdBuffers(const uint32_t bufferCount = 1);
		vk::CommandBuffer createComputeCmdBuffer();
		Pipeline createPipeline(const PipelineInfo& specificInfo);
		Pipeline createCompositionPipeline();
		Pipeline createSSRPipeline();
		Pipeline createSSAOPipeline();
		Pipeline createSSAOBlurPipeline();
		Pipeline createMotionBlurPipeline();
		Pipeline createComputePipeline();
		vk::DescriptorPool createDescriptorPool(const uint32_t maxDescriptorSets);
		std::vector<vk::Fence> createFences(const uint32_t fenceCount);
		std::vector<vk::Semaphore> createSemaphores(const uint32_t semaphoresCount);
	};
}