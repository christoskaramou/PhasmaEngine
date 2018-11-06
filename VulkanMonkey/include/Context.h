#pragma once

#include "Vulkan.h"
#include "SDL.h"
#include "Math.h"
#include "Vertex.h"
#include "Image.h"
#include "Surface.h"
#include "Swapchain.h"
#include "Buffer.h"
#include "specificGraphicsPipelineCreateInfo.h"
#include "Pipeline.h"
#include "Object.h"
#include "GUI.h"
#include "Skybox.h"
#include "Terrain.h"
#include "Shadows.h"
#include "Mesh.h"
#include "Model.h"
#include "Camera.h"

constexpr auto MAX_LIGHTS = 20;

struct Context
{
public:
	static Context* info;

	struct ProjView
	{
		vm::mat4 projection, view;
	};

	struct UBO
	{
		vm::mat4 projection, view, model;
	};

	struct Light
	{
		vm::vec4 color, position, attenuation, camPos;
	};

	void initVulkanContext();
	void resizeViewport(uint32_t width, uint32_t height);

	SDL_Window* window;
	Surface surface;
	vk::Instance instance;
	vk::PhysicalDevice gpu;
	vk::PhysicalDeviceProperties gpuProperties;
	vk::PhysicalDeviceFeatures gpuFeatures;
	int graphicsFamilyId = -1, presentFamilyId = -1, computeFamilyId = -1;
	vk::Device device;
	vk::Queue graphicsQueue, presentQueue, computeQueue;
	vk::CommandPool commandPool;
	vk::RenderPass renderPass, rRenderPass, guiRenderPass;
	vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e4;
	Swapchain swapchain;
	Image depth, MSColorImage, MSDepthImage;
	std::vector<vk::Framebuffer> frameBuffers{}, rFrameBuffers{}, guiFrameBuffers{};
	vk::CommandBuffer dynamicCmdBuffer;
	vk::CommandBuffer shadowCmdBuffer;
	vk::DescriptorPool descriptorPool;
	Pipeline pipeline, pipelineReflection, pipelineGUI, pipelineSkyBox, pipelineShadows, pipelineTerrain, pipelineCompute;
	std::vector<vk::Fence> fences{};
	std::vector<vk::Semaphore> semaphores{};
	std::vector<Model> models{};
	Shadows shadows;
	GUI gui;
	SkyBox skyBox;
	Camera mainCamera;
	Terrain terrain;
	std::vector<Light> light;
	Buffer UBLights, UBReflection, SBIn, SBOut;
	vk::DescriptorSet DSLights, DSCompute, DSReflection;
	vk::DescriptorSetLayout DSLayoutLights, DSLayoutCompute, DSLayoutReflection;
	bool SSReflections = false;

	// deferred
	vk::RenderPass dRenderPass;
	Image position, normal, albedo, specular, finalColor, finalNormal, finalDepth;
	std::vector<vk::Framebuffer> dFrameBuffers{};
	vk::DescriptorSet DSDeferredMainLight, DSComposition;
	vk::DescriptorSetLayout DSLayoutComposition;
	Pipeline pipelineDeferred, pipelineComposition;

	static specificGraphicsPipelineCreateInfo getPipelineSpecificationsModel();
	static specificGraphicsPipelineCreateInfo getPipelineSpecificationsShadows();
	static specificGraphicsPipelineCreateInfo getPipelineSpecificationsSkyBox();
	static specificGraphicsPipelineCreateInfo getPipelineSpecificationsTerrain();
	static specificGraphicsPipelineCreateInfo getPipelineSpecificationsGUI();

private:
	vk::Instance createInstance();
	Surface createSurface();
	vk::PhysicalDevice findGpu();
	vk::Device createDevice();
	Swapchain createSwapchain();
	vk::CommandPool createCommandPool();
	vk::RenderPass createRenderPass();
	vk::RenderPass createDeferredRenderPass();
	vk::RenderPass createReflectionRenderPass();
	vk::RenderPass createGUIRenderPass();
	Image createDepthResources();
	std::vector<vk::Framebuffer> createFrameBuffers();
	std::vector<vk::Framebuffer> createDeferredFrameBuffers();
	std::vector<vk::Framebuffer> createReflectionFrameBuffers();
	std::vector<vk::Framebuffer> createGUIFrameBuffers();
	std::vector<vk::CommandBuffer> createCmdBuffers(const uint32_t bufferCount);
	vk::CommandBuffer createCmdBuffer();
	Pipeline createPipeline(const specificGraphicsPipelineCreateInfo& specificInfo);
	Pipeline createCompositionPipeline();
	Pipeline createReflectionPipeline();
	Pipeline createComputePipeline();
	vk::DescriptorPool createDescriptorPool(const uint32_t maxDescriptorSets);
	std::vector<vk::Fence> createFences(const uint32_t fenceCount);
	std::vector<vk::Semaphore> createSemaphores(const uint32_t semaphoresCount);
};