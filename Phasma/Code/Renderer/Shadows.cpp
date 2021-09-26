/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "PhasmaPch.h"
#include "Shadows.h"
#include "GUI/GUI.h"
#include "Swapchain.h"
#include "Vertex.h"
#include "Shader/Shader.h"
#include "Core/Queue.h"
#include "RenderApi.h"

namespace pe
{
	uint32_t Shadows::imageSize = 4096;
	
	Shadows::Shadows()
	{
		descriptorSets = make_ref(std::vector<vk::DescriptorSet>());
	}
	
	Shadows::~Shadows()
	{
	}
	
	void Shadows::createDescriptorSets()
	{
		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = *VulkanContext::Get()->descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutShadows();
		
		descriptorSets->resize(textures.size()); // size of wanted number of cascaded shadows
		for (uint32_t i = 0; i < descriptorSets->size(); i++)
		{
			(*descriptorSets)[i] = VulkanContext::Get()->device->allocateDescriptorSets(allocateInfo).at(0);
			VulkanContext::Get()->SetDebugObjectName((*descriptorSets)[i], "Shadows" + std::to_string(i));
			
			std::vector<vk::WriteDescriptorSet> textureWriteSets(2);
			// MVP
			vk::DescriptorBufferInfo dbi;
			dbi.buffer = *uniformBuffers[i].GetBufferVK();
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
			dii.sampler = *textures[i].sampler;
			dii.imageView = *textures[i].view;
			dii.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
			
			textureWriteSets[1].dstSet = (*descriptorSets)[i];
			textureWriteSets[1].dstBinding = 1;
			textureWriteSets[1].dstArrayElement = 0;
			textureWriteSets[1].descriptorCount = 1;
			textureWriteSets[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
			textureWriteSets[1].pImageInfo = &dii;
			
			VulkanContext::Get()->device->updateDescriptorSets(textureWriteSets, nullptr);
		}
	}
	
	void Shadows::createRenderPass()
	{
		vk::AttachmentDescription attachment;
		attachment.format = *VulkanContext::Get()->depth.format;
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
		
		renderPass.handle = make_ref(VulkanContext::Get()->device->createRenderPass(rpci));
	}
	
	void Shadows::createFrameBuffers()
	{
		textures.resize(3);
		int textureIdx = 0;
		for (auto& texture : textures)
		{
			texture.format = VulkanContext::Get()->depth.format;
			texture.initialLayout = make_ref(vk::ImageLayout::eUndefined);
			texture.addressMode = make_ref(vk::SamplerAddressMode::eClampToEdge);
			texture.maxAnisotropy = 1.f;
			texture.borderColor = make_ref(vk::BorderColor::eFloatOpaqueWhite);
			texture.samplerCompareEnable = VK_TRUE;
			texture.compareOp = make_ref(vk::CompareOp::eGreaterOrEqual);
			texture.samplerMipmapMode = make_ref(vk::SamplerMipmapMode::eLinear);
			
			texture.createImage(
					Shadows::imageSize, Shadows::imageSize, vk::ImageTiling::eOptimal,
					vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
					vk::MemoryPropertyFlagBits::eDeviceLocal
			);
			texture.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
			texture.createImageView(vk::ImageAspectFlagBits::eDepth);
			texture.createSampler();
			texture.SetDebugName("ShadowPass_DepthImage" + textureIdx++);
		}
		
		framebuffers.resize(VulkanContext::Get()->swapchain.images.size() * textures.size());
		for (uint32_t i = 0; i < framebuffers.size(); ++i)
		{
			uint32_t width = Shadows::imageSize;
			uint32_t height = Shadows::imageSize;
			vk::ImageView view = *textures[i % textures.size()].view;
			framebuffers[i].Create(width, height, view, renderPass);
		}
	}
	
	void Shadows::createPipeline()
	{
		Shader vert {"Shaders/Shadows/shaderShadows.vert", ShaderType::Vertex, true};
		
		pipeline.info.pVertShader = &vert;
		pipeline.info.vertexInputBindingDescriptions = make_ref(Vertex::getBindingDescriptionGeneral());
		pipeline.info.vertexInputAttributeDescriptions = make_ref(Vertex::getAttributeDescriptionGeneral());
		pipeline.info.width = static_cast<float>(Shadows::imageSize);
		pipeline.info.height = static_cast<float>(Shadows::imageSize);
		pipeline.info.cullMode = CullMode::Front;
		pipeline.info.colorBlendAttachments = make_ref(
				std::vector<vk::PipelineColorBlendAttachmentState> {*textures[0].blentAttachment}
		);
		pipeline.info.dynamicStates = make_ref(std::vector<vk::DynamicState> {vk::DynamicState::eDepthBias});
		pipeline.info.descriptorSetLayouts = make_ref(
				std::vector<vk::DescriptorSetLayout>
						{
								Pipeline::getDescriptorSetLayoutShadows(),
								Pipeline::getDescriptorSetLayoutMesh(),
								Pipeline::getDescriptorSetLayoutModel()
						}
		);
		pipeline.info.renderPass = renderPass;
		
		pipeline.createGraphicsPipeline();
	}
	
	void Shadows::createUniformBuffers()
	{
		uniformBuffers.resize(textures.size());
		int ubIdx = 0;
		for (auto& buffer : uniformBuffers)
		{
			buffer.CreateBuffer(
				sizeof(ShadowsUBO),
				(BufferUsageFlags)vk::BufferUsageFlagBits::eUniformBuffer,
				(MemoryPropertyFlags)vk::MemoryPropertyFlagBits::eHostVisible);
			buffer.Map();
			buffer.Zero();
			buffer.Flush();
			buffer.Unmap();
			buffer.SetDebugName("Shadows_UB" + ubIdx++);
		}
	}
	
	void Shadows::destroy()
	{
		if (*renderPass.handle)
			VulkanContext::Get()->device->destroyRenderPass(*renderPass.handle);
		
		if (Pipeline::getDescriptorSetLayoutShadows())
		{
			VulkanContext::Get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutShadows());
			Pipeline::getDescriptorSetLayoutShadows() = nullptr;
		}
		
		for (auto& texture : textures)
			texture.destroy();
		
		for (auto& fb : framebuffers)
			VulkanContext::Get()->device->destroyFramebuffer(*fb.handle);
		
		for (auto& buffer : uniformBuffers)
			buffer.Destroy();
		
		pipeline.destroy();
	}
	
	void Shadows::update(Camera& camera)
	{
		if (GUI::shadow_cast)
		{
			//       Fustum
			//
			//  opposite opposite
			//     \    |    /
			//      \   |   /
			//       \ adj /
			//        \ | /
			//         \|/
			//        theta
			// 
			// opposite = adj*tan(theta)
			// 
			
			const float theta = radians(camera.FOV * .5f);
			const float adj = camera.nearPlane; // reversed near/far plane
			const float opposite = adj * tan(theta);
			const vec3 sunDirection = vec3(&GUI::sun_direction[0]) * camera.worldOrientation;
			const vec3 sunPosition = camera.position + ((-sunDirection) * adj * 0.5f);
			const vec3 sunFront = sunDirection;
			const vec3 sunRight = normalize(cross(sunFront, camera.WorldUp()));
			const vec3 sunUp = normalize(cross(sunRight, sunFront));
			const mat4 lookAt = pe::lookAt(sunPosition, sunFront, sunRight, sunUp);
			const float scale0 = 0.05f; // small area
			const float scale1 = 0.15f; // medium area
			const float scale2 = 0.5f; // large area
			const float maxDistance0 = adj * scale0 * .43f;
			const float maxDistance1 = adj * scale1 * .43f;
			const float maxDistance2 = adj * scale2 * .43f;

			float orthoSide = opposite * scale0;
			mat4 ortho = pe::ortho(-orthoSide, orthoSide, -orthoSide, orthoSide, camera.nearPlane, camera.farPlane);
			shadows_UBO[0] =
			{
				ortho,
				lookAt,
				1.0f,
				maxDistance0,
				maxDistance1,
				maxDistance2
			};

			orthoSide = opposite * scale1;
			ortho = pe::ortho(-orthoSide, orthoSide, -orthoSide, orthoSide, camera.nearPlane, camera.farPlane);
			shadows_UBO[1] =
			{
				ortho,
				lookAt,
				1.0f,
				maxDistance0,
				maxDistance1,
				maxDistance2
			};

			orthoSide = opposite * scale2;
			ortho = pe::ortho(-orthoSide, orthoSide, -orthoSide, orthoSide, camera.nearPlane, camera.farPlane);
			shadows_UBO[2] =
			{
				ortho,
				lookAt,
				1.0f,
				maxDistance0,
				maxDistance1,
				maxDistance2
			};
		}
		else
		{
			shadows_UBO[0].castShadows = 0.f;
		}

		uniformBuffers[0].CopyRequest(Launch::AsyncDeferred, { &shadows_UBO[0], sizeof(ShadowsUBO), 0 });
		uniformBuffers[1].CopyRequest(Launch::AsyncDeferred, { &shadows_UBO[1], sizeof(ShadowsUBO), 0 });
		uniformBuffers[2].CopyRequest(Launch::AsyncDeferred, { &shadows_UBO[2], sizeof(ShadowsUBO), 0 });
	}
}
