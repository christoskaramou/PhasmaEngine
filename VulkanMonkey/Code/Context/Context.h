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

#define USE_SCRIPTS
#define UNIFIED_GRAPHICS_AND_TRANSFER_QUEUE

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
		[[nodiscard]] vk::Instance createInstance() const;
		static VKAPI_ATTR VkBool32 VKAPI_CALL messageCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData);
		[[nodiscard]] vk::DebugUtilsMessengerEXT createDebugMessenger() const;
		void destroyDebugMessenger() const;
		[[nodiscard]] Surface createSurface() const;
		[[nodiscard]] int getGraphicsFamilyId() const;
		[[nodiscard]] int getTransferFamilyId() const;
		[[nodiscard]] int getComputeFamilyId() const;
		[[nodiscard]] vk::PhysicalDevice findGpu() const;
		[[nodiscard]] vk::PhysicalDeviceProperties getGPUProperties() const;
		[[nodiscard]] vk::PhysicalDeviceFeatures getGPUFeatures() const;
		[[nodiscard]] vk::SurfaceCapabilitiesKHR getSurfaceCapabilities() const;
		[[nodiscard]] vk::SurfaceFormatKHR getSurfaceFormat() const;
		[[nodiscard]] vk::PresentModeKHR getPresentationMode() const;
		[[nodiscard]] vk::Device createDevice() const;
		[[nodiscard]] vk::Queue getGraphicsQueue() const;
		[[nodiscard]] vk::Queue getTransferQueue() const;
		[[nodiscard]] vk::Queue getComputeQueue() const;
		Swapchain createSwapchain() const;
		[[nodiscard]] vk::CommandPool createCommandPool() const;
		[[nodiscard]] Image createDepthResources() const;
		[[nodiscard]] std::vector<vk::CommandBuffer> createCmdBuffers(uint32_t bufferCount = 1) const;
		[[nodiscard]] vk::DescriptorPool createDescriptorPool(uint32_t maxDescriptorSets) const;
		[[nodiscard]] std::vector<vk::Fence> createFences(uint32_t fenceCount) const;
		[[nodiscard]] std::vector<vk::Semaphore> createSemaphores(uint32_t semaphoresCount) const;
		void addRenderTarget(const std::string& name, vk::Format format, const vk::ImageUsageFlags& additionalFlags = vk::ImageUsageFlags());
	};
}