#ifndef RENDERER_H
#define RENDERER_H

#include "Context.h"
class Renderer
{
    public:
        Renderer(SDL_Window* window);
        ~Renderer();
        void update(float delta);
        void draw();
		float rand(float x1, float x2);
		Context info;
		bool prepared = false;
    private:
		void initVulkanContext();
        vk::Instance createInstance();
		Surface createSurface();
        vk::PhysicalDevice findGpu();
		vk::Device createDevice();
        Swapchain createSwapchain();
        vk::CommandPool createCommandPool();
        vk::RenderPass createRenderPass();
        Image createDepthResources();
        std::vector<vk::Framebuffer> createFrameBuffers();
		std::vector<vk::CommandBuffer> createCmdBuffers(uint32_t bufferCount);
		vk::CommandBuffer createCmdBuffer();
        Pipeline createPipeline(const specificGraphicsPipelineCreateInfo& specificInfo);

		vk::DescriptorPool createDescriptorPool(const uint32_t maxDescriptorSets);
        std::vector<vk::Semaphore> createSemaphores(uint32_t semaphoresCount);
		void recordDynamicCmdBuffer(const uint32_t imageIndex);
		void recordShadowsCmdBuffers();
};

#endif // RENDERER_H
