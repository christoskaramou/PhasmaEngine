#include "PhasmaPch.h"
#include "MotionBlur.h"
#include <deque>
#include "../Renderer/Surface.h"
#include "../Renderer/Swapchain.h"
#include "../GUI/GUI.h"
#include "../Shader/Shader.h"
#include "../Core/Queue.h"
#include "../Core/Timer.h"
#include "../Renderer/RenderApi.h"

namespace pe
{
	MotionBlur::MotionBlur()
	{
		DSet = make_ref(vk::DescriptorSet());
	}
	
	MotionBlur::~MotionBlur()
	{
	}
	
	void MotionBlur::Init()
	{
		frameImage.format = make_ref(VulkanContext::Get()->surface.formatKHR->format);
		frameImage.initialLayout = make_ref(vk::ImageLayout::eUndefined);
		frameImage.createImage(
				static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale),
				static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale),
				vk::ImageTiling::eOptimal,
				vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
				vk::MemoryPropertyFlagBits::eDeviceLocal
		);
		frameImage.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
		frameImage.createImageView(vk::ImageAspectFlagBits::eColor);
		frameImage.createSampler();
	}
	
	void MotionBlur::createMotionBlurUniforms(std::map<std::string, Image>& renderTargets)
	{
		auto size = 4 * sizeof(mat4);
		UBmotionBlur.CreateBuffer(size, BufferUsage::UniformBuffer, MemoryProperty::HostVisible);
		UBmotionBlur.Map();
		UBmotionBlur.Zero();
		UBmotionBlur.Flush();
		UBmotionBlur.Unmap();
		
		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = *VulkanContext::Get()->descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutMotionBlur();
		DSet = make_ref(VulkanContext::Get()->device->allocateDescriptorSets(allocateInfo).at(0));
		
		updateDescriptorSets(renderTargets);
	}
	
	void MotionBlur::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
	{
		std::deque<vk::DescriptorImageInfo> dsii {};
		auto const wSetImage = [&dsii](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Image& image)
		{
			dsii.emplace_back(*image.sampler, *image.view, vk::ImageLayout::eShaderReadOnlyOptimal);
			return vk::WriteDescriptorSet {
					dstSet, dstBinding, 0, 1, vk::DescriptorType::eCombinedImageSampler, &dsii.back(), nullptr, nullptr
			};
		};
		std::deque<vk::DescriptorBufferInfo> dsbi {};
		auto const wSetBuffer = [&dsbi](const vk::DescriptorSet& dstSet, uint32_t dstBinding, Buffer& buffer)
		{
			dsbi.emplace_back(*buffer.GetBufferVK(), 0, buffer.Size());
			return vk::WriteDescriptorSet {
					dstSet, dstBinding, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &dsbi.back(), nullptr
			};
		};
		
		std::vector<vk::WriteDescriptorSet> textureWriteSets {
				wSetImage(*DSet, 0, frameImage),
				wSetImage(*DSet, 1, renderTargets["depth"]),
				wSetImage(*DSet, 2, renderTargets["velocity"]),
				wSetBuffer(*DSet, 3, UBmotionBlur)
		};
		VulkanContext::Get()->device->updateDescriptorSets(textureWriteSets, nullptr);
	}
	
	void MotionBlur::draw(vk::CommandBuffer cmd, uint32_t imageIndex, const vk::Extent2D& extent)
	{
		vk::ClearValue clearColor;
		memcpy(clearColor.color.float32, GUI::clearColor.data(), 4 * sizeof(float));
		
		std::vector<vk::ClearValue> clearValues = {clearColor};
		
		vk::RenderPassBeginInfo rpi;
		rpi.renderPass = *renderPass.handle;
		rpi.framebuffer = *framebuffers[imageIndex].handle;
		rpi.renderArea.offset = vk::Offset2D {0, 0};
		rpi.renderArea.extent = extent;
		rpi.clearValueCount = static_cast<uint32_t>(clearValues.size());
		rpi.pClearValues = clearValues.data();
		cmd.beginRenderPass(rpi, vk::SubpassContents::eInline);
		
		const vec4 values {
				1.f / static_cast<float>(FrameTimer::Instance().delta),
				sin(static_cast<float>(FrameTimer::Instance().time) * 0.125f), GUI::motionBlur_strength, 0.f
		};
		cmd.pushConstants<vec4>(*pipeline.layout, vk::ShaderStageFlagBits::eFragment, 0, values);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline.handle);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline.layout, 0, *DSet, nullptr);
		cmd.draw(3, 1, 0, 0);
		cmd.endRenderPass();
	}
	
	void MotionBlur::destroy()
	{
		for (auto& frameBuffer : framebuffers)
			frameBuffer.Destroy();
		
		renderPass.Destroy();
		
		if (Pipeline::getDescriptorSetLayoutMotionBlur())
		{
			VulkanContext::Get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutMotionBlur());
			Pipeline::getDescriptorSetLayoutMotionBlur() = nullptr;
		}
		frameImage.destroy();
		UBmotionBlur.Destroy();
		pipeline.destroy();
	}
	
	void MotionBlur::update(Camera& camera)
	{
		if (GUI::show_motionBlur)
		{
			static mat4 previousView = camera.view;
			
			motionBlurInput[0] = camera.projection;
			motionBlurInput[1] = camera.view;
			motionBlurInput[2] = previousView;
			motionBlurInput[3] = camera.invViewProjection;
			
			previousView = camera.view;
			
			Queue::memcpyRequest(&UBmotionBlur, {{&motionBlurInput, sizeof(motionBlurInput), 0}});
			//UBmotionBlur.map();
			//memcpy(UBmotionBlur.data, &motionBlurInput, sizeof(motionBlurInput));
			//UBmotionBlur.flush();
			//UBmotionBlur.unmap();
		}
	}
	
	void MotionBlur::createRenderPass(std::map<std::string, Image>& renderTargets)
	{
		renderPass.Create(*renderTargets["viewport"].format, vk::Format::eUndefined);
	}
	
	void MotionBlur::createFrameBuffers(std::map<std::string, Image>& renderTargets)
	{
		auto vulkan = VulkanContext::Get();
		framebuffers.resize(vulkan->swapchain.images.size());
		for (size_t i = 0; i < vulkan->swapchain.images.size(); ++i)
		{
			uint32_t width = renderTargets["viewport"].width;
			uint32_t height = renderTargets["viewport"].height;
			vk::ImageView view = *renderTargets["viewport"].view;
			framebuffers[i].Create(width, height, view, renderPass);
		}
	}
	
	void MotionBlur::createPipeline(std::map<std::string, Image>& renderTargets)
	{
		// Shader stages
		Shader vert {"Shaders/Common/quad.vert", ShaderType::Vertex, true};
		Shader frag {"Shaders/MotionBlur/motionBlur.frag", ShaderType::Fragment, true};
		
		pipeline.info.pVertShader = &vert;
		pipeline.info.pFragShader = &frag;
		pipeline.info.width = renderTargets["viewport"].width_f;
		pipeline.info.height = renderTargets["viewport"].height_f;
		pipeline.info.cullMode = CullMode::Back;
		pipeline.info.colorBlendAttachments = make_ref(
				std::vector<vk::PipelineColorBlendAttachmentState> {*renderTargets["viewport"].blentAttachment}
		);
		pipeline.info.pushConstantStage = PushConstantStage::Fragment;
		pipeline.info.pushConstantSize = sizeof(vec4);
		pipeline.info.descriptorSetLayouts = make_ref(
				std::vector<vk::DescriptorSetLayout> {Pipeline::getDescriptorSetLayoutMotionBlur()}
		);
		pipeline.info.renderPass = renderPass;
		
		pipeline.createGraphicsPipeline();
	}
}
