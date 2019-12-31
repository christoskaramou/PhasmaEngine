#pragma once

#include <vector>
#include "../Image/Image.h"
#include "../Surface/Surface.h"
#include "../Swapchain/Swapchain.h"
#include "../GUI/GUI.h"
#include "../Skybox/Skybox.h"
#include "../Shadows/Shadows.h"
#include "../Light/Light.h"
#include "../Model/Model.h"
#include "../Camera/Camera.h"
#include "../SSAO/SSAO.h"
#include "../SSR/SSR.h"
#include "../FXAA/FXAA.h"
#include "../TAA/TAA.h"
#include "../Bloom/Bloom.h"
#include "../DepthOfField/DOF.h"
#include "../MotionBlur/MotionBlur.h"
#include "../Deferred/Deferred.h"
#include "../Compute/Compute.h"
#include "../Metrics/Metrics.h"
#include "../Script/Script.h"

//#define USE_SCRIPTS
#define UNIFIED_GRAPHICS_AND_TRANSFER_QUEUE

namespace vm {
	struct Context
	{
		Context() = default;
		~Context() = default;

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

		// Depth of Field
		DOF dof;

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

		void initVulkanContext() const;
		void initRendering();
		void loadResources();
		void createUniforms();
		void destroyVkContext();
		void resizeViewport(uint32_t width, uint32_t height);
		void recreatePipelines();

		vk::Instance createInstance() const;
		static VKAPI_ATTR VkBool32 VKAPI_CALL messageCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData);
		vk::DebugUtilsMessengerEXT createDebugMessenger() const;
		void destroyDebugMessenger() const;
		Surface createSurface() const;
		int getGraphicsFamilyId() const;
		int getTransferFamilyId() const;
		int getComputeFamilyId() const;
		vk::PhysicalDevice findGpu() const;
		vk::PhysicalDeviceProperties getGPUProperties() const;
		vk::PhysicalDeviceFeatures getGPUFeatures() const;
		vk::SurfaceCapabilitiesKHR getSurfaceCapabilities() const;
		vk::SurfaceFormatKHR getSurfaceFormat() const;
		vk::PresentModeKHR getPresentationMode() const;
		vk::Device createDevice() const;
		vk::Queue getGraphicsQueue() const;
		vk::Queue getTransferQueue() const;
		vk::Queue getComputeQueue() const;
		Swapchain createSwapchain(uint32_t requestImageCount) const;
		vk::CommandPool createCommandPool() const;
		Image createDepthResources() const;
		std::vector<vk::CommandBuffer> createCmdBuffers(uint32_t bufferCount = 1) const;
		vk::DescriptorPool createDescriptorPool(uint32_t maxDescriptorSets) const;
		std::vector<vk::Fence> createFences(uint32_t fenceCount) const;
		std::vector<vk::Semaphore> createSemaphores(uint32_t semaphoresCount) const;
		void addRenderTarget(const std::string& name, vk::Format format, const vk::ImageUsageFlags& additionalFlags = vk::ImageUsageFlags());
	};
}