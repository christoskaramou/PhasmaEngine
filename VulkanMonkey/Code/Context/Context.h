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
#include "../Shadows/Shadows.h"
#include "../Light/Light.h"
#include "../Mesh/Mesh.h"
#include "../Model/Model.h"
#include "../Camera/Camera.h"
#include "../SSAO/SSAO.h"
#include "../SSR/SSR.h"
#include "../FXAA/FXAA.h"
#include "../TAA/TAA.h"
#include "../Bloom/Bloom.h"
#include "../MotionBlur/MotionBlur.h"
#include "../Deferred/Deferred.h"
#include "../Compute/Compute.h"
#include "../Metrics/Metrics.h"
#include "../Script/Script.h"
#include <vector>
#include <tuple>
#include <map>

//#define USE_SCRIPTS

namespace vm {
	struct Context
	{
	public:
		// VULKAN CONTEXT
		VulkanContext& vulkan = VulkanContext::get();

		// COMPUTE
		ComputePool& computePool = ComputePool::get();

		// RENDER TARGETS
		std::map<std::string, Image> renderTargets{};

		// SHADOWS
		Shadows shadows;

		// DEFERRED
		Deferred deferred;

		// SSAO
		SSAO ssao;

		// SSR
		SSR ssr;

		// FXAA
		FXAA fxaa;

		// TAA
		TAA taa;

		// BLOOM
		Bloom bloom;

		// MOTION BLUR
		MotionBlur motionBlur;

		// SKYBOXES
		SkyBox skyBoxDay;
		SkyBox skyBoxNight;

		// GUI
		GUI gui;

		// LIGHTS
		LightUniforms lightUniforms;

		// CAMERAS
		// main camera
		Camera camera_main;
		// other cameras
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

	public:
		vk::Instance createInstance();
		Surface createSurface();
		int getGraphicsFamilyId();
		int getComputeFamilyId();
		int getTransferFamilyId();
		vk::PhysicalDevice findGpu();
		vk::PhysicalDeviceProperties getGPUProperties();
		vk::PhysicalDeviceFeatures getGPUFeatures();
		vk::SurfaceCapabilitiesKHR getSurfaceCapabilities();
		vk::SurfaceFormatKHR getSurfaceFormat();
		vk::PresentModeKHR getPresentationMode();
		vk::Device createDevice();
		vk::Queue getGraphicsQueue();
		vk::Queue getComputeQueue();
		vk::Queue getTransferQueue();
		Swapchain createSwapchain();
		vk::CommandPool createCommandPool();
		Image createDepthResources();
		std::vector<vk::CommandBuffer> createCmdBuffers(const uint32_t bufferCount = 1);
		vk::DescriptorPool createDescriptorPool(const uint32_t maxDescriptorSets);
		std::vector<vk::Fence> createFences(const uint32_t fenceCount);
		std::vector<vk::Semaphore> createSemaphores(const uint32_t semaphoresCount);
		void addRenderTarget(const std::string& name, vk::Format format, vk::ImageUsageFlags additionalFlags = vk::ImageUsageFlags());
	};
}