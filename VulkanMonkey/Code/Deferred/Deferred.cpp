#include "Deferred.h"
#include <iostream>

using namespace vm;

void Deferred::createDeferredUniforms(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms)
{
	vk::DescriptorSetAllocateInfo allocInfo = vk::DescriptorSetAllocateInfo{
	vulkan->descriptorPool,						//DescriptorPool descriptorPool;
	1,										//uint32_t descriptorSetCount;
	&DSLayoutComposition				//const DescriptorSetLayout* pSetLayouts;
	};
	DSComposition = vulkan->device.allocateDescriptorSets(allocInfo)[0];

	// Image descriptors for the offscreen color attachments
	vk::DescriptorImageInfo texDescriptorPosition = vk::DescriptorImageInfo{
		renderTargets["depth"].sampler,			//Sampler sampler;
		renderTargets["depth"].view,				//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorNormal = vk::DescriptorImageInfo{
		renderTargets["normal"].sampler,			//Sampler sampler;
		renderTargets["normal"].view,				//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorAlbedo = vk::DescriptorImageInfo{
		renderTargets["albedo"].sampler,			//Sampler sampler;
		renderTargets["albedo"].view,				//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorSRM = vk::DescriptorImageInfo{
		renderTargets["srm"].sampler,			//Sampler sampler;
		renderTargets["srm"].view,				//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorSSAOBlur = vk::DescriptorImageInfo{
		renderTargets["ssaoBlur"].sampler,			//Sampler sampler;
		renderTargets["ssaoBlur"].view,				//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorSSR = vk::DescriptorImageInfo{
		renderTargets["ssr"].sampler,				//Sampler sampler;
		renderTargets["ssr"].view,					//ImageView imageView;
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
		// Binding 3: Specula Roughness Metallic texture target
		vk::WriteDescriptorSet{
			DSComposition,						//DescriptorSet dstSet;
			3,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorSRM,					//const DescriptorImageInfo* pImageInfo;
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
				.setOffset(0)						// DeviceSize offset;
				.setRange(lightUniforms.uniform.size),// DeviceSize range;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 5: SSAO Blurred Image
		vk::WriteDescriptorSet{
			DSComposition,						//DescriptorSet dstSet;
			5,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorSSAOBlur,					//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		},
		// Binding 6: SSR Image
		vk::WriteDescriptorSet{
			DSComposition,						//DescriptorSet dstSet;
			6,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorSSR,						//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		}
	};

	vulkan->device.updateDescriptorSets(writeDescriptorSets, nullptr);
}

void Deferred::updateDescriptorSets(std::map<std::string, Image>& renderTargets, LightUniforms& lightUniforms)
{
	// Image descriptors for the offscreen color attachments
	vk::DescriptorImageInfo texDescriptorPosition = vk::DescriptorImageInfo{
		renderTargets["depth"].sampler,		//Sampler sampler;
		renderTargets["depth"].view,			//ImageView imageView;
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
	vk::DescriptorImageInfo texDescriptorSRM = vk::DescriptorImageInfo{
		renderTargets["srm"].sampler,		//Sampler sampler;
		renderTargets["srm"].view,			//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorSSAOBlur = vk::DescriptorImageInfo{
		renderTargets["ssaoBlur"].sampler,		//Sampler sampler;
		renderTargets["ssaoBlur"].view,			//ImageView imageView;
		vk::ImageLayout::eColorAttachmentOptimal	//ImageLayout imageLayout;
	};
	vk::DescriptorImageInfo texDescriptorSSR = vk::DescriptorImageInfo{
		renderTargets["ssr"].sampler,				//Sampler sampler;
		renderTargets["ssr"].view,					//ImageView imageView;
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
		// Binding 3: Specular Roughness Metallic target
		vk::WriteDescriptorSet{
			DSComposition,						//DescriptorSet dstSet;
			3,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorSRM,					//const DescriptorImageInfo* pImageInfo;
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
		},
		// Binding 6: SSR Image
		vk::WriteDescriptorSet{
			DSComposition,						//DescriptorSet dstSet;
			6,										//uint32_t dstBinding;
			0,										//uint32_t dstArrayElement;
			1,										//uint32_t descriptorCount_;
			vk::DescriptorType::eCombinedImageSampler,//DescriptorType descriptorType;
			&texDescriptorSSR,						//const DescriptorImageInfo* pImageInfo;
			nullptr,								//const DescriptorBufferInfo* pBufferInfo;
			nullptr									//const BufferView* pTexelBufferView;
		}
	};

	vulkan->device.updateDescriptorSets(writeDescriptorSets, nullptr);
}

void Deferred::draw(uint32_t imageIndex, Shadows& shadows, mat4& invViewProj, vec2 UVOffset[2])
{
	// Begin Composition
	std::vector<vk::ClearValue> clearValues0 = {
		vk::ClearColorValue().setFloat32(GUI::clearColor),
		vk::ClearColorValue().setFloat32(GUI::clearColor) };

	auto renderPassInfo0 = vk::RenderPassBeginInfo()
		.setRenderPass(compositionRenderPass)
		.setFramebuffer(compositionFrameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, vulkan->surface->actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues0.size()))
		.setPClearValues(clearValues0.data());
	vulkan->dynamicCmdBuffer.beginRenderPass(renderPassInfo0, vk::SubpassContents::eInline);

	vec4 screenSpace[6];
	screenSpace[0] = { GUI::show_ssao ? 1.f : 0.f, GUI::show_ssr ? 1.f : 0.f, 0.f, 0.f };
	screenSpace[1] = { UVOffset[0].x, UVOffset[0].y, UVOffset[1].x, UVOffset[1].y };
	screenSpace[2] = { invViewProj[0] };
	screenSpace[3] = { invViewProj[1] };
	screenSpace[4] = { invViewProj[2] };
	screenSpace[5] = { invViewProj[3] };

	vulkan->dynamicCmdBuffer.pushConstants(pipelineComposition.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(screenSpace), &screenSpace);
	vulkan->dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineComposition.pipeline);
	uint32_t ofsets = 0;
	vulkan->dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineComposition.pipeinfo.layout, 0, { DSComposition, shadows.descriptorSet }, ofsets);
	vulkan->dynamicCmdBuffer.draw(3, 1, 0, 0);
	vulkan->dynamicCmdBuffer.endRenderPass();
	// End Composition
}

void Deferred::destroy()
{
	if (renderPass) {
		vulkan->device.destroyRenderPass(renderPass);
		renderPass = nullptr;
	}
	if (compositionRenderPass) {
		vulkan->device.destroyRenderPass(compositionRenderPass);
		compositionRenderPass = nullptr;
	}
	for (auto &frameBuffer : frameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	for (auto &frameBuffer : compositionFrameBuffers) {
		if (frameBuffer) {
			vulkan->device.destroyFramebuffer(frameBuffer);
		}
	}
	if (DSLayoutComposition) {
		vulkan->device.destroyDescriptorSetLayout(DSLayoutComposition);
		DSLayoutComposition = nullptr;
	}
	pipeline.destroy();
	pipelineComposition.destroy();
}
