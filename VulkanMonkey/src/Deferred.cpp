#include "../include/Deferred.h"
#include "../include/Errors.h"
#include <iostream>

using namespace vm;

vm::Deferred::Deferred(VulkanContext * vulkan) : vulkan(vulkan)
{ }

void Deferred::createDeferredUniforms(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms)
{
	vk::DescriptorSetAllocateInfo allocInfo = vk::DescriptorSetAllocateInfo{
	vulkan->descriptorPool,						//DescriptorPool descriptorPool;
	1,										//uint32_t descriptorSetCount;
	&DSLayoutComposition				//const DescriptorSetLayout* pSetLayouts;
	};
	VkCheck(vulkan->device.allocateDescriptorSets(&allocInfo, &DSComposition));

	// Image descriptors for the offscreen color attachments
	vk::DescriptorImageInfo texDescriptorPosition = vk::DescriptorImageInfo{
		renderTargets["position"].sampler,		//Sampler sampler;
		renderTargets["position"].view,			//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorNormal = vk::DescriptorImageInfo{
		renderTargets["normal"].sampler,			//Sampler sampler;
		renderTargets["normal"].view,			//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorAlbedo = vk::DescriptorImageInfo{
		renderTargets["albedo"].sampler,			//Sampler sampler;
		renderTargets["albedo"].view,			//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorSpecular = vk::DescriptorImageInfo{
		renderTargets["specular"].sampler,		//Sampler sampler;
		renderTargets["specular"].view,			//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorSSAOBlur = vk::DescriptorImageInfo{
		renderTargets["ssaoBlur"].sampler,		//Sampler sampler;
		renderTargets["ssaoBlur"].view,			//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		// Binding 0: Position texture target
		vk::WriteDescriptorSet{
			DSComposition,						//DescriptorSet dstSet;
			0,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorPosition,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 1: Normals texture target
		vk::WriteDescriptorSet{
			DSComposition,						//DescriptorSet dstSet;
			1,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorNormal,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 2: Albedo texture target
		vk::WriteDescriptorSet{
			DSComposition,						//DescriptorSet dstSet;
			2,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorAlbedo,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 3: Specular texture target
		vk::WriteDescriptorSet{
			DSComposition,						//DescriptorSet dstSet;
			3,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorSpecular,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 4: Fragment shader lights
		vk::WriteDescriptorSet{
			DSComposition,						//DescriptorSet dstSet;
			4,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eUniformBuffer,		//DescriptorType descriptorType;
			nullptr,								//const DescriptorImageInfo* pImageInfo;
			&vk::DescriptorBufferInfo()				//const DescriptorBufferInfo* pBufferInfo;
				.setBuffer(lightUniforms.uniform.buffer)// Buffer buffer;
				.setOffset(0)							// DeviceSize offset;
				.setRange(lightUniforms.uniform.size),	// DeviceSize range;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 5: SSAO Blurred Image
		vk::WriteDescriptorSet{
			DSComposition,						//DescriptorSet dstSet;
			5,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorSSAOBlur,						//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		}
	};

	vulkan->device.updateDescriptorSets(static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";
}

void vm::Deferred::destroy()
{
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
		std::cout << "RenderPass destroyed\n";
	}
	if (compositionRenderPass) {
		vulkan->device.destroyRenderPass(compositionRenderPass);
		compositionRenderPass = nullptr;
		std::cout << "RenderPass destroyed\n";
	}
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
			std::cout << "Frame Buffer destroyed\n";
		}
	}
	for (auto &frameBuffer : compositionFrameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
			std::cout << "Frame Buffer destroyed\n";
		}
	}
	if (DSLayoutComposition) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutComposition);
		DSLayoutComposition = nullptr;
		std::cout << "Descriptor Set Layout destroyed\n";
	}
	pipeline.destroy();
	pipelineComposition.destroy();
}
