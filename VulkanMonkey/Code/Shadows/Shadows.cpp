#include "Shadows.h"
#include "../GUI/GUI.h"
#include "../Swapchain/Swapchain.h"
#include "../Core/Vertex.h"
#include "../Shader/Shader.h"
#include "../Core/Queue.h"
#include "../VulkanContext/VulkanContext.h"
#include <vulkan/vulkan.hpp>

namespace vm
{
	uint32_t Shadows::imageSize = 4096;

	Shadows::Shadows()
	{
		descriptorSets = std::vector<vk::DescriptorSet>();
	}

	Shadows::~Shadows()
	{
	}

	void Shadows::createDescriptorSets()
	{
		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = VulkanContext::get()->descriptorPool.Value();
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutShadows();

		descriptorSets->resize(textures.size()); // size of wanted number of cascaded shadows
		for (uint32_t i = 0; i < descriptorSets->size(); i++) {
			(*descriptorSets)[i] = VulkanContext::get()->device->allocateDescriptorSets(allocateInfo).at(0);

			std::vector<vk::WriteDescriptorSet> textureWriteSets(2);
			// MVP
			vk::DescriptorBufferInfo dbi;
			dbi.buffer = uniformBuffers[i].buffer.Value();
			dbi.offset = 0;
			dbi.range = sizeof(ShadowsUBO);

			textureWriteSets[0].dstSet = (*descriptorSets)[i];
			textureWriteSets[0].dstBinding = 0;
			textureWriteSets[0].dstArrayElement = 0;
			textureWriteSets[0].descriptorCount = 1;
			textureWriteSets[0].descriptorType = vk::DescriptorType::eUniformBuffer;
			textureWriteSets[0].pBufferInfo = &dbi;

			// sampler
			vk::DescriptorImageInfo dii;
			dii.sampler = textures[i].sampler.Value();
			dii.imageView = textures[i].view.Value();
			dii.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

			textureWriteSets[1].dstSet = (*descriptorSets)[i];
			textureWriteSets[1].dstBinding = 1;
			textureWriteSets[1].dstArrayElement = 0;
			textureWriteSets[1].descriptorCount = 1;
			textureWriteSets[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
			textureWriteSets[1].pImageInfo = &dii;

			VulkanContext::get()->device->updateDescriptorSets(textureWriteSets, nullptr);
		}
	}

	void Shadows::createRenderPass()
	{
		vk::AttachmentDescription attachment;
		attachment.format = VulkanContext::get()->depth.format.Value();
		attachment.samples = vk::SampleCountFlagBits::e1;
		attachment.loadOp = vk::AttachmentLoadOp::eClear;
		attachment.storeOp = vk::AttachmentStoreOp::eStore;
		attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		attachment.stencilStoreOp = vk::AttachmentStoreOp::eStore;
		attachment.initialLayout = vk::ImageLayout::eUndefined;
		attachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::AttachmentReference depthAttachmentRef;
		depthAttachmentRef.attachment = 0;
		depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::SubpassDescription subpassDesc;
		subpassDesc.pDepthStencilAttachment = &depthAttachmentRef;

		vk::RenderPassCreateInfo rpci;
		rpci.attachmentCount = 1;
		rpci.pAttachments = &attachment;
		rpci.subpassCount = 1;
		rpci.pSubpasses = &subpassDesc;

		renderPass.SetRef(VulkanContext::get()->device->createRenderPass(rpci));
	}

	void Shadows::createFrameBuffers()
	{
		textures.resize(3);
		for (auto& texture : textures)
		{
			texture.format = VulkanContext::get()->depth.format;
			texture.initialLayout = vk::ImageLayout::eUndefined;
			texture.addressMode = vk::SamplerAddressMode::eClampToEdge;
			texture.maxAnisotropy = 1.f;
			texture.borderColor = vk::BorderColor::eFloatOpaqueWhite;
			texture.samplerCompareEnable = VK_TRUE;
			texture.compareOp = vk::CompareOp::eGreaterOrEqual;
			texture.samplerMipmapMode = vk::SamplerMipmapMode::eLinear;

			texture.createImage(Shadows::imageSize, Shadows::imageSize, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
			texture.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
			texture.createImageView(vk::ImageAspectFlagBits::eDepth);
			texture.createSampler();
		}

		framebuffers.resize(VulkanContext::get()->swapchain.images.size() * textures.size());
		for (uint32_t i = 0; i < framebuffers.size(); ++i)
		{
			uint32_t width = Shadows::imageSize;
			uint32_t height = Shadows::imageSize;
			vk::ImageView view = textures[i % textures.size()].view.Value();
			framebuffers[i].Create(width, height, view, renderPass);
		}
	}

	void Shadows::createPipeline()
	{
		Shader vert{ "shaders/Shadows/shaderShadows.vert", ShaderType::Vertex, true };

		pipeline.info.pVertShader = &vert;
		pipeline.info.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
		pipeline.info.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
		pipeline.info.width = static_cast<float>(Shadows::imageSize);
		pipeline.info.height = static_cast<float>(Shadows::imageSize);
		pipeline.info.cullMode = CullMode::Front;
		pipeline.info.colorBlendAttachments = { textures[0].blentAttachment.Value() };
		pipeline.info.dynamicStates = { vk::DynamicState::eDepthBias };
		pipeline.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutShadows(), Pipeline::getDescriptorSetLayoutMesh(), Pipeline::getDescriptorSetLayoutModel() };
		pipeline.info.renderPass = renderPass;

		pipeline.createGraphicsPipeline();
	}

	void Shadows::createUniformBuffers()
	{
		uniformBuffers.resize(textures.size());
		for (auto& buffer : uniformBuffers) {
			buffer.createBuffer(sizeof(ShadowsUBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
			buffer.map();
			buffer.zero();
			buffer.flush();
			buffer.unmap();
		}
	}

	void Shadows::destroy()
	{
		if (*renderPass)
			VulkanContext::get()->device->destroyRenderPass(*renderPass);

		if (Pipeline::getDescriptorSetLayoutShadows()) {
			VulkanContext::get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutShadows());
			Pipeline::getDescriptorSetLayoutShadows() = nullptr;
		}

		for (auto& texture : textures)
			texture.destroy();

		for (auto& fb : framebuffers)
			VulkanContext::get()->device->destroyFramebuffer(*fb);

		for (auto& buffer : uniformBuffers)
			buffer.destroy();

		pipeline.destroy();
	}

	void Shadows::update(Camera& camera)
	{
		if (GUI::shadow_cast) {
			// far/cos(x) = the side size
			const float sideSizeOfPyramid = camera.nearPlane / cos(radians(camera.FOV * .5f)); // near plane is actually the far plane (they are reversed)
			const vec3 p = &GUI::sun_position[0];

			vec3 pointOnPyramid = camera.front * (sideSizeOfPyramid * .01f);
			vec3 pos = p + camera.position + pointOnPyramid; // sun position will be moved, so its angle to the lookat position is the same always
			vec3 front = normalize(camera.position + pointOnPyramid - pos);
			vec3 right = normalize(cross(front, camera.worldUp()));
			vec3 up = normalize(cross(right, front));
			float orthoSide = sideSizeOfPyramid * .01f; // small area
			shadows_UBO[0] = {
				ortho(-orthoSide, orthoSide, -orthoSide, orthoSide, camera.nearPlane, camera.farPlane),
				lookAt(pos, front, right, up),
				1.0f,
				sideSizeOfPyramid * .02f,
				sideSizeOfPyramid * .1f,
				sideSizeOfPyramid
			};

			Queue::memcpyRequest(&uniformBuffers[0], { { &shadows_UBO[0], sizeof(ShadowsUBO), 0 } });
			//uniformBuffers[0].map();
			//memcpy(uniformBuffers[0].data, &shadows_UBO[0], sizeof(ShadowsUBO));
			//uniformBuffers[0].flush();
			//uniformBuffers[0].unmap();

			pointOnPyramid = camera.front * (sideSizeOfPyramid * .05f);
			pos = p + camera.position + pointOnPyramid;
			front = normalize(camera.position + pointOnPyramid - pos);
			right = normalize(cross(front, camera.worldUp()));
			up = normalize(cross(right, front));
			orthoSide = sideSizeOfPyramid * .05f; // medium area
			shadows_UBO[1] = {
				ortho(-orthoSide, orthoSide, -orthoSide, orthoSide, camera.nearPlane, camera.farPlane),
				lookAt(pos, front, right, up),
				1.0f,
				sideSizeOfPyramid * .02f,
				sideSizeOfPyramid * .1f,
				sideSizeOfPyramid
			};

			Queue::memcpyRequest(&uniformBuffers[1], { { &shadows_UBO[1], sizeof(ShadowsUBO), 0} });
			//uniformBuffers[1].map();
			//memcpy(uniformBuffers[1].data, &shadows_UBO[1], sizeof(ShadowsUBO));
			//uniformBuffers[1].flush();
			//uniformBuffers[1].unmap();

			pointOnPyramid = camera.front * (sideSizeOfPyramid * .5f);
			pos = p + camera.position + pointOnPyramid;
			front = normalize(camera.position + pointOnPyramid - pos);
			right = normalize(cross(front, camera.worldUp()));
			up = normalize(cross(right, front));
			orthoSide = sideSizeOfPyramid * .5f; // large area
			shadows_UBO[2] = {
				ortho(-orthoSide, orthoSide, -orthoSide, orthoSide, camera.nearPlane, camera.farPlane),
				lookAt(pos, front, right, up),
				1.0f,
				sideSizeOfPyramid * .02f,
				sideSizeOfPyramid * .1f,
				sideSizeOfPyramid
			};

			Queue::memcpyRequest(&uniformBuffers[2], { { &shadows_UBO[2], sizeof(ShadowsUBO), 0 } });
			//uniformBuffers[2].map();
			//memcpy(uniformBuffers[2].data, &shadows_UBO[2], sizeof(ShadowsUBO));
			//uniformBuffers[2].flush();
			//uniformBuffers[2].unmap();
		}
		else
		{
			shadows_UBO[0].castShadows = 0.f;

			Queue::memcpyRequest(&uniformBuffers[0], { { &shadows_UBO[0], sizeof(ShadowsUBO), 0 } });
			//uniformBuffers[0].map();
			//memcpy(uniformBuffers[0].data, &shadows_UBO[0], sizeof(ShadowsUBO));
			//uniformBuffers[0].flush();
			//uniformBuffers[0].unmap();

			Queue::memcpyRequest(&uniformBuffers[1], { { &shadows_UBO[1], sizeof(ShadowsUBO), 0 } });
			//uniformBuffers[1].map();
			//memcpy(uniformBuffers[1].data, &shadows_UBO[0], sizeof(ShadowsUBO));
			//uniformBuffers[1].flush();
			//uniformBuffers[1].unmap();

			Queue::memcpyRequest(&uniformBuffers[2], { { &shadows_UBO[2], sizeof(ShadowsUBO), 0 } });
			//uniformBuffers[2].map();
			//memcpy(uniformBuffers[2].data, &shadows_UBO[0], sizeof(ShadowsUBO));
			//uniformBuffers[2].flush();
			//uniformBuffers[2].unmap();
		}
	}
}
