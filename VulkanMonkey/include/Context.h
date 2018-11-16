#pragma once

#include "Vulkan.h"
#include "SDL.h"
#include "Math.h"
#include "Vertex.h"
#include "Image.h"
#include "Surface.h"
#include "Swapchain.h"
#include "Buffer.h"
#include "Pipeline.h"
#include "Object.h"
#include "GUI.h"
#include "Skybox.h"
#include "Terrain.h"
#include "Shadows.h"
#include "Mesh.h"
#include "Model.h"
#include "Camera.h"

#include <tuple>
#include <map>

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
	vk::CommandPool commandPoolCompute;
	vk::RenderPass forwardRenderPass, ssrRenderPass, guiRenderPass;
	vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e4;
	Swapchain swapchain;
	Image depth, MSColorImage, MSDepthImage;
	std::vector<vk::Framebuffer> frameBuffers{}, ssrFrameBuffers{}, guiFrameBuffers{};
	vk::CommandBuffer dynamicCmdBuffer;
	vk::CommandBuffer shadowCmdBuffer;
	vk::CommandBuffer computeCmdBuffer;
	vk::DescriptorPool descriptorPool;
	Pipeline pipeline, pipelineSSR, pipelineGUI, pipelineSkyBox, pipelineShadows, pipelineTerrain, pipelineCompute;
	std::vector<vk::Fence> fences{};
	std::vector<vk::Semaphore> semaphores{};
	std::vector<Model> models{};
	Shadows shadows;
	GUI gui;
	SkyBox skyBox;
	Camera mainCamera;
	Terrain terrain;
	std::vector<Light> light;
	Buffer UBLights, UBReflection, SBInOut;
	vk::DescriptorSet DSLights, DSCompute, DSReflection;
	vk::DescriptorSetLayout DSLayoutLights, DSLayoutCompute, DSLayoutReflection;

	// DEFERRED
	vk::RenderPass deferredRenderPass, compositionRenderPass;
	std::map<std::string, Image> renderTarget{};
	std::vector<vk::Framebuffer> deferredFrameBuffers{}, compositionFrameBuffers{};
	vk::DescriptorSet DSDeferredMainLight, DSComposition;
	vk::DescriptorSetLayout DSLayoutComposition;
	Pipeline pipelineDeferred, pipelineComposition;

	// SSAO
	Buffer UBssaoKernel, UBssaoPVM;
	Image ssaoNoise;
	vk::RenderPass ssaoRenderPass, ssaoBlurRenderPass;
	std::vector<vk::Framebuffer> ssaoFrameBuffers{}, ssaoBlurFrameBuffers{};
	Pipeline pipelineSSAO, pipelineSSAOBlur;
	vk::DescriptorSetLayout DSLayoutSSAO, DSLayoutSSAOBlur;
	vk::DescriptorSet DSssao, DSssaoBlur;

	static PipelineInfo getPipelineSpecificationsModel();
	static PipelineInfo getPipelineSpecificationsShadows();
	static PipelineInfo getPipelineSpecificationsSkyBox();
	static PipelineInfo getPipelineSpecificationsTerrain();
	static PipelineInfo getPipelineSpecificationsGUI();
	static PipelineInfo getPipelineSpecificationsDeferred();

private:
	vk::Instance createInstance();
	Surface createSurface();
	vk::PhysicalDevice findGpu();
	vk::Device createDevice();
	Swapchain createSwapchain();
	vk::CommandPool createCommandPool();
	vk::CommandPool createComputeCommadPool();
	std::map<std::string, Image> createRenderTargets(std::vector<std::tuple<std::string, vk::Format>> RTtuples);
	vk::RenderPass createRenderPass();
	vk::RenderPass createDeferredRenderPass();
	vk::RenderPass createCompositionRenderPass();
	vk::RenderPass createSSAORenderPass();
	vk::RenderPass createSSAOBlurRenderPass();
	vk::RenderPass createReflectionRenderPass();
	vk::RenderPass createGUIRenderPass();
	Image createDepthResources();
	std::vector<vk::Framebuffer> createFrameBuffers();
	std::vector<vk::Framebuffer> createDeferredFrameBuffers();
	std::vector<vk::Framebuffer> createCompositionFrameBuffers();
	std::vector<vk::Framebuffer> createReflectionFrameBuffers();
	std::vector<vk::Framebuffer> createSSAOFrameBuffers();
	std::vector<vk::Framebuffer> createSSAOBlurFrameBuffers();
	std::vector<vk::Framebuffer> createGUIFrameBuffers();
	std::vector<vk::CommandBuffer> createCmdBuffers(const uint32_t bufferCount);
	vk::CommandBuffer createCmdBuffer();
	vk::CommandBuffer createComputeCmdBuffer();
	Pipeline createPipeline(const PipelineInfo& specificInfo);
	Pipeline createCompositionPipeline();
	Pipeline createReflectionPipeline();
	Pipeline createSSAOPipeline();
	Pipeline createSSAOBlurPipeline();
	Pipeline createComputePipeline();
	vk::DescriptorPool createDescriptorPool(const uint32_t maxDescriptorSets);
	std::vector<vk::Fence> createFences(const uint32_t fenceCount);
	std::vector<vk::Semaphore> createSemaphores(const uint32_t semaphoresCount);
};