#pragma once

#include "../Core/Image.h"
#include "../Core/Surface.h"
#include "../Swapchain/Swapchain.h"
#include "../GUI/GUI.h"
#include "../Skybox/Skybox.h"
#include "../Shadows/Shadows.h"
#include "../Core/Light.h"
#include "../Model/Model.h"
#include "../Camera/Camera.h"
#include "../Deferred/Deferred.h"
#include "../Compute/Compute.h"
#include "../Core/Timer.h"
#include "../Script/Script.h"
#include "../PostProcess/Bloom.h"
#include "../PostProcess/DOF.h"
#include "../PostProcess/FXAA.h"
#include "../PostProcess/MotionBlur.h"
#include "../PostProcess/SSAO.h"
#include "../PostProcess/SSR.h"
#include "../PostProcess/TAA.h"

#if defined(_WIN32)
// On Windows, Vulkan commands use the stdcall convention
#define VKAPI_ATTR
#define VKAPI_CALL __stdcall
#define VKAPI_PTR  VKAPI_CALL
#elif defined(__ANDROID__) && defined(__ARM_ARCH) && __ARM_ARCH < 7
#error "Vulkan isn't supported for the 'armeabi' NDK ABI"
#elif defined(__ANDROID__) && defined(__ARM_ARCH) && __ARM_ARCH >= 7 && defined(__ARM_32BIT_STATE)
// On Android 32-bit ARM targets, Vulkan functions use the "hardfloat"
// calling convention, i.e. float parameters are passed in registers. This
// is true even if the rest of the application passes floats on the stack,
// as it does by default when compiling for the armeabi-v7a NDK ABI.
#define VKAPI_ATTR __attribute__((pcs("aapcs-vfp")))
#define VKAPI_CALL
#define VKAPI_PTR  VKAPI_ATTR
#else
// On other platforms, use the default calling convention
#define VKAPI_ATTR
#define VKAPI_CALL
#define VKAPI_PTR
#endif

//#define USE_SCRIPTS
#define UNIFIED_GRAPHICS_AND_TRANSFER_QUEUE

namespace vk
{
	class Instance;
	class DebugUtilsMessengerEXT;
	class PhysicalDevice;
	struct PhysicalDeviceProperties;
	struct PhysicalDeviceFeatures;
	struct SurfaceCapabilitiesKHR;
	struct SurfaceFormatKHR;
	enum class PresentModeKHR;
	class Device;
	class Queue;
	class CommandPool;
	class CommandBuffer;
	class Fence;
	class Semaphore;
	enum class Format;
	class DescriptorPool;

	template<class T1, class T2> class Flags;
	enum class ImageUsageFlagBits;
	using ImageUsageFlags = Flags<ImageUsageFlagBits, uint32_t>;

}

enum VkDebugUtilsMessageSeverityFlagBitsEXT;
struct VkDebugUtilsMessengerCallbackDataEXT;

namespace vm
{
	class ContextTemp
	{
	public:
		ContextTemp() = default;
		~ContextTemp() = default;

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
		std::vector<GPUTimer> metrics{};

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
		static VKAPI_ATTR uint32_t VKAPI_CALL messageCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			uint32_t messageType,
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
		void addRenderTarget(const std::string& name, vk::Format format, const vk::ImageUsageFlags& additionalFlags);
	};
}