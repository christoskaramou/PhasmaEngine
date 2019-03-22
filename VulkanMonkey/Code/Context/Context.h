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
#include "../SSDO/SSDO.h"
#include "../SSR/SSR.h"
#include "../FXAA/FXAA.h"
#include "../Bloom/Bloom.h"
#include "../MotionBlur/MotionBlur.h"
#include "../Deferred/Deferred.h"
#include "../Compute/Compute.h"
#include "../Metrics/Metrics.h"
#include "../Script/Script.h"
#include <vector>
#include <tuple>
#include <map>

#define USE_SCRIPTS

namespace vm {
	struct Context
	{
	public:
		// VULKAN CONTEXT
		VulkanContext &vulkan = VulkanContext::get();

		// RENDER TARGETS
		std::map<std::string, Image> renderTargets{};

		// SHADOWS
		Shadows shadows;

		// COMPUTE
		Compute compute;

		// DEFERRED
		Deferred deferred;

		// SSAO
		SSAO ssao;

		// SSDO
		SSDO ssdo;

		// SSR
		SSR ssr;

		// FXAA
		FXAA fxaa;

		// BLOOM
		Bloom bloom;

		// MOTION BLUR
		MotionBlur motionBlur;

		// SKYBOXES
		SkyBox skyBoxDay;
		SkyBox skyBoxNight;

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
		std::vector<Metrics> metrics{};
#ifdef USE_SCRIPTS
		// Scripts
		std::vector<Script*> scripts{};
#endif

		void initVulkanContext();
		void initRendering();
		void loadResources();
		void createUniforms();
		void destroyVkContext();
		void resizeViewport(uint32_t width, uint32_t height);

		PipelineInfo getPipelineSpecificationsShadows();
		PipelineInfo getPipelineSpecificationsSkyBox(SkyBox& skybox);
		PipelineInfo getPipelineSpecificationsTerrain();
		PipelineInfo getPipelineSpecificationsGUI();
		PipelineInfo getPipelineSpecificationsDeferred();

		vk::DescriptorSetLayout getDescriptorSetLayoutLights();

	public:
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
		vk::RenderPass createSSDORenderPass();
		vk::RenderPass createSSDOBlurRenderPass();
		vk::RenderPass createSSRRenderPass();
		vk::RenderPass createFXAARenderPass();
		vk::RenderPass createBrightFilterRenderPass();
		vk::RenderPass createGaussianBlurRenderPass();
		vk::RenderPass createCombineRenderPass();
		vk::RenderPass createMotionBlurRenderPass();
		vk::RenderPass createGUIRenderPass();
		vk::RenderPass createShadowsRenderPass();
		vk::RenderPass createSkyboxRenderPass();
		Image createDepthResources();
		std::vector<vk::Framebuffer> createDeferredFrameBuffers();
		std::vector<vk::Framebuffer> createCompositionFrameBuffers();
		std::vector<vk::Framebuffer> createSSRFrameBuffers();
		std::vector<vk::Framebuffer> createSSAOFrameBuffers();
		std::vector<vk::Framebuffer> createSSAOBlurFrameBuffers();
		std::vector<vk::Framebuffer> createSSDOFrameBuffers();
		std::vector<vk::Framebuffer> createSSDOBlurFrameBuffers();
		std::vector<vk::Framebuffer> createFXAAFrameBuffers();
		std::vector<vk::Framebuffer> createBloomFrameBuffers();
		std::vector<vk::Framebuffer> createMotionBlurFrameBuffers();
		std::vector<vk::Framebuffer> createGUIFrameBuffers();
		std::vector<vk::Framebuffer> createShadowsFrameBuffers();
		std::vector<vk::Framebuffer> createSkyboxFrameBuffers(SkyBox& skybox);
		std::vector<vk::CommandBuffer> createCmdBuffers(const uint32_t bufferCount = 1);
		vk::CommandBuffer createComputeCmdBuffer();
		Pipeline createPipeline(const PipelineInfo& specificInfo);
		Pipeline createCompositionPipeline();
		Pipeline createSSRPipeline();
		Pipeline createFXAAPipeline();
		Pipeline createBrightFilterPipeline();
		Pipeline createGaussianBlurHorizontalPipeline();
		Pipeline createGaussianBlurVerticalPipeline();
		Pipeline createCombinePipeline();
		Pipeline createSSAOPipeline();
		Pipeline createSSAOBlurPipeline();
		Pipeline createSSDOPipeline();
		Pipeline createSSDOBlurPipeline();
		Pipeline createMotionBlurPipeline();
		Pipeline createComputePipeline();
		vk::DescriptorPool createDescriptorPool(const uint32_t maxDescriptorSets);
		std::vector<vk::Fence> createFences(const uint32_t fenceCount);
		std::vector<vk::Semaphore> createSemaphores(const uint32_t semaphoresCount);
	};
}