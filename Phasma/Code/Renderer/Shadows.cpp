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
#include <limits>
#include "ECS/Context.h"
#include "Systems/RendererSystem.h"

namespace pe
{
	uint32_t Shadows::imageSize = 4096;

	Shadows::Shadows()
	{
		descriptorSetDeferred = make_sptr(vk::DescriptorSet());
	}

	Shadows::~Shadows()
	{
	}

	void Shadows::createDescriptorSets()
	{
		vk::DescriptorSetAllocateInfo allocateInfo;
		allocateInfo.descriptorPool = *VulkanContext::Get()->descriptorPool;
		allocateInfo.descriptorSetCount = 1;
		allocateInfo.pSetLayouts = &Pipeline::getDescriptorSetLayoutShadowsDeferred();

		*descriptorSetDeferred = VulkanContext::Get()->device->allocateDescriptorSets(allocateInfo).at(0);
		VulkanContext::Get()->SetDebugObjectName(*descriptorSetDeferred, "Shadows");

		std::vector<vk::WriteDescriptorSet> textureWriteSets(4);

		for (int i = 0; i < 3; i++)
		{
			// sampler
			vk::DescriptorImageInfo dii;
			dii.sampler = *textures[i].sampler;
			dii.imageView = *textures[i].view;
			dii.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

			vk::WriteDescriptorSet textureWriteSet;
			textureWriteSet.dstSet = *descriptorSetDeferred;
			textureWriteSet.dstBinding = i;
			textureWriteSet.dstArrayElement = 0;
			textureWriteSet.descriptorCount = 1;
			textureWriteSet.descriptorType = vk::DescriptorType::eCombinedImageSampler;
			textureWriteSet.pImageInfo = &dii;
			VulkanContext::Get()->device->updateDescriptorSets(textureWriteSet, nullptr);
		}

		vk::DescriptorBufferInfo dbi;
		dbi.buffer = uniformBuffer->Handle<vk::Buffer>();
		dbi.offset = 0;
		dbi.range = 3 * sizeof(mat4);

		vk::WriteDescriptorSet bufferWriteSet;
		bufferWriteSet.dstSet = *descriptorSetDeferred;
		bufferWriteSet.dstBinding = 3;
		bufferWriteSet.dstArrayElement = 0;
		bufferWriteSet.descriptorCount = 1;
		bufferWriteSet.descriptorType = vk::DescriptorType::eUniformBuffer;
		bufferWriteSet.pBufferInfo = &dbi;

		VulkanContext::Get()->device->updateDescriptorSets(bufferWriteSet, nullptr);
	}

	void Shadows::createRenderPass()
	{
		renderPass.Create(*VulkanContext::Get()->depth.format);
	}

	void Shadows::createFrameBuffers()
	{
		textures.resize(3);
		int textureIdx = 0;
		for (auto& texture : textures)
		{
			texture.format = VulkanContext::Get()->depth.format;
			texture.initialLayout = make_sptr(vk::ImageLayout::eUndefined);
			texture.addressMode = make_sptr(vk::SamplerAddressMode::eClampToEdge);
			texture.maxAnisotropy = 1.f;
			texture.borderColor = make_sptr(vk::BorderColor::eFloatOpaqueWhite);
			texture.samplerCompareEnable = VK_TRUE;
			texture.compareOp = make_sptr(vk::CompareOp::eGreaterOrEqual);
			texture.samplerMipmapMode = make_sptr(vk::SamplerMipmapMode::eLinear);

			texture.createImage(
				Shadows::imageSize, Shadows::imageSize, vk::ImageTiling::eOptimal,
				vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
				vk::MemoryPropertyFlagBits::eDeviceLocal
			);
			texture.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal);
			texture.createImageView(vk::ImageAspectFlagBits::eDepth);
			texture.createSampler();
			texture.name = "ShadowPass_DepthImage" + std::to_string(textureIdx++);
			texture.SetDebugName(texture.name);
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
		Shader vert{ "Shaders/Shadows/shaderShadows.vert", ShaderType::Vertex, true };

		pipeline.info.pVertShader = &vert;
		pipeline.info.vertexInputBindingDescriptions = make_sptr(Vertex::getBindingDescriptionGeneral());
		pipeline.info.vertexInputAttributeDescriptions = make_sptr(Vertex::getAttributeDescriptionGeneral());
		pipeline.info.width = static_cast<float>(Shadows::imageSize);
		pipeline.info.height = static_cast<float>(Shadows::imageSize);
		pipeline.info.cullMode = CullMode::Front;
		pipeline.info.pushConstantStage = PushConstantStage::Vertex;
		pipeline.info.pushConstantSize = sizeof(mat4);
		pipeline.info.colorBlendAttachments = make_sptr(
			std::vector<vk::PipelineColorBlendAttachmentState> {*textures[0].blentAttachment}
		);
		pipeline.info.dynamicStates = make_sptr(std::vector<vk::DynamicState> {vk::DynamicState::eDepthBias});
		pipeline.info.descriptorSetLayouts = make_sptr(std::vector<vk::DescriptorSetLayout>
		{
			Pipeline::getDescriptorSetLayoutMesh(),
			Pipeline::getDescriptorSetLayoutModel()
		});
		pipeline.info.renderPass = renderPass;

		pipeline.createGraphicsPipeline();
	}

	void Shadows::createUniformBuffers()
	{
		uniformBuffer = Buffer::Create(
			3 * sizeof(mat4),
			(BufferUsageFlags)vk::BufferUsageFlagBits::eUniformBuffer,
			(MemoryPropertyFlags)vk::MemoryPropertyFlagBits::eHostVisible);
		uniformBuffer->Map();
		uniformBuffer->Zero();
		uniformBuffer->Flush();
		uniformBuffer->Unmap();
		uniformBuffer->SetDebugName("Shadows_UB_Fragment");
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

		uniformBuffer->Destroy();
		pipeline.destroy();
	}

	void Shadows::update(Camera& camera)
	{
		if (GUI::shadow_cast)
		{
			CalculateCascades(camera);
			uniformBuffer->CopyRequest<Launch::AsyncDeferred>({ cascades, 3 * sizeof(mat4), 0 });
		}
	}

	void Shadows::CalculateCascades(Camera& camera)
	{
		const mat4 view = camera.view;
		const mat4 invView = camera.invView;
		const vec3 sunDirection = vec3(&GUI::sun_direction[0]);// *camera.worldOrientation;
		const vec3 sunFront = sunDirection;
		const vec3 sunRight = normalize(cross(sunFront, camera.WorldUp()));
		const vec3 sunUp = normalize(cross(sunRight, sunFront));
		const mat4 sunView = lookAt(-sunFront * (camera.nearPlane - 5.f), sunFront, sunRight, sunUp);

		auto& renderArea = Context::Get()->GetSystem<RendererSystem>()->GetRenderArea();
		const float aspect = renderArea.viewport.width / renderArea.viewport.height;
		const float tanHalfHFOV = tanf(radians(camera.FOV * .5f * aspect));
		const float tanHalfVFOV = tanf(radians(camera.FOV * .5f));

		const float cascadeEnd[] = {
			-10.f,
			10.f,
			40.f,
			130.f
		};

		for (uint32_t i = 0; i < 3; i++)
		{
			float xn = cascadeEnd[i + 0] * tanHalfHFOV;
			float xf = cascadeEnd[i + 1] * tanHalfHFOV;
			float yn = cascadeEnd[i + 0] * tanHalfVFOV;
			float yf = cascadeEnd[i + 1] * tanHalfVFOV;

			vec4 frustumCorners[] = {
				// near face
				vec4( xn,  yn, cascadeEnd[i], 1.0),
				vec4(-xn,  yn, cascadeEnd[i], 1.0),
				vec4( xn, -yn, cascadeEnd[i], 1.0),
				vec4(-xn, -yn, cascadeEnd[i], 1.0),

				// far face
				vec4( xf,  yf, cascadeEnd[i + 1], 1.0),
				vec4(-xf,  yf, cascadeEnd[i + 1], 1.0),
				vec4( xf, -yf, cascadeEnd[i + 1], 1.0),
				vec4(-xf, -yf, cascadeEnd[i + 1], 1.0)
			};

			vec4 frustumCornersL[8];

			float minX = std::numeric_limits<float>::max();
			float maxX = std::numeric_limits<float>::min();
			float minY = std::numeric_limits<float>::max();
			float maxY = std::numeric_limits<float>::min();
			float minZ = std::numeric_limits<float>::max();
			float maxZ = std::numeric_limits<float>::min();

			for (uint32_t j = 0; j < 8; j++) {

				// Transform the frustum coordinate from view to world space
				vec4 vW = invView * frustumCorners[j];// *vec4(camera.worldOrientation, 1.f);

				// Transform the frustum coordinate from world to light space
				frustumCornersL[j] = sunView * vW;

				minX = minimum(minX, frustumCornersL[j].x);
				maxX = maximum(maxX, frustumCornersL[j].x);
				minY = minimum(minY, frustumCornersL[j].y);
				maxY = maximum(maxY, frustumCornersL[j].y);
				minZ = minimum(minZ, frustumCornersL[j].z);
				maxZ = maximum(maxZ, frustumCornersL[j].z);
			}
			//std::swap(minZ, maxZ); // reverse Z
			//std::swap(minY, maxY); // reverse Y
			mat4 sunProj = ortho(minX, maxX, minY, maxY, minZ, maxZ);

			cascades[i] = sunProj * sunView;
			viewZ[i] = cascadeEnd[i + 1];
		}
	}
}
