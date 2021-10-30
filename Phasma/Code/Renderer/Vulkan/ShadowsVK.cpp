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

#if PE_VULKAN
#include "Renderer/Shadows.h"
#include "GUI/GUI.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Vertex.h"
#include "Shader/Shader.h"
#include "Core/Queue.h"
#include "Renderer/RHI.h"
#include "Renderer/Descriptor.h"
#include "ECS/Context.h"
#include "Systems/RendererSystem.h"
#include "Core/Settings.h"

namespace pe
{
	Shadows::Shadows()
	{
		descriptorSetDeferred = {};
	}

	Shadows::~Shadows()
	{
	}

	void Shadows::createDescriptorSets()
	{
		descriptorSetDeferred = Descriptor::Create(Pipeline::getDescriptorSetLayoutShadowsDeferred());

		for (int i = 0; i < SHADOWMAP_CASCADES; i++)
		{
			DescriptorUpdateInfo info{};
			info.binding = i;
			info.pImage = &textures[i];
			info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
			descriptorSetDeferred->UpdateDescriptor(1, &info);
		}

		DescriptorUpdateInfo info{};
		info.binding = SHADOWMAP_CASCADES;
		info.pBuffer = uniformBuffer;
		descriptorSetDeferred->UpdateDescriptor(1, &info);
	}

	void Shadows::createRenderPass()
	{
		Attachment attachment{};
		attachment.format = RHII.depth.format;
		attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		renderPass.Create(attachment);
	}

	void Shadows::createFrameBuffers()
	{
		textures.resize(SHADOWMAP_CASCADES);
		int textureIdx = 0;
		for (auto& texture : textures)
		{
			texture.format = RHII.depth.format;
			texture.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			texture.addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			texture.maxAnisotropy = 1.f;
			texture.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			texture.samplerCompareEnable = VK_TRUE;
			texture.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
			texture.samplerMipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

			texture.CreateImage(
				SHADOWMAP_SIZE, SHADOWMAP_SIZE, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			);
			texture.TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
			texture.CreateImageView(VK_IMAGE_ASPECT_DEPTH_BIT);
			texture.CreateSampler();
		}

		framebuffers.resize(RHII.swapchain.images.size() * textures.size());
		for (uint32_t i = 0; i < framebuffers.size(); ++i)
		{
			uint32_t width = SHADOWMAP_SIZE;
			uint32_t height = SHADOWMAP_SIZE;
			ImageViewHandle view = textures[i % textures.size()].view;
			framebuffers[i].Create(width, height, view, renderPass);
		}
	}

	void Shadows::createPipeline()
	{
		Shader vert{ "Shaders/Shadows/shaderShadows.vert", ShaderType::Vertex, true };

		pipeline.info.pVertShader = &vert;
		pipeline.info.vertexInputBindingDescriptions = Vertex::GetBindingDescriptionGeneral();
		pipeline.info.vertexInputAttributeDescriptions = Vertex::GetAttributeDescriptionGeneral();
		pipeline.info.width = static_cast<float>(SHADOWMAP_SIZE);
		pipeline.info.height = static_cast<float>(SHADOWMAP_SIZE);
		pipeline.info.pushConstantStage = PushConstantStage::Vertex;
		pipeline.info.pushConstantSize = sizeof(mat4);
		pipeline.info.colorBlendAttachments = { textures[0].blendAttachment };
		pipeline.info.dynamicStates = { VK_DYNAMIC_STATE_DEPTH_BIAS };
		pipeline.info.descriptorSetLayouts = { Pipeline::getDescriptorSetLayoutMesh(), Pipeline::getDescriptorSetLayoutModel() };
		pipeline.info.renderPass = renderPass;

		pipeline.createGraphicsPipeline();
	}

	void Shadows::createUniformBuffers()
	{
		uniformBuffer = Buffer::Create(
			SHADOWMAP_CASCADES * sizeof(mat4),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		uniformBuffer->Map();
		uniformBuffer->Zero();
		uniformBuffer->Flush();
		uniformBuffer->Unmap();
	}

	void Shadows::destroy()
	{
		if (VkRenderPass(renderPass.handle))
		{
			vkDestroyRenderPass(RHII.device, renderPass.handle, nullptr);
			renderPass.handle = {};
		}

		Pipeline::getDescriptorSetLayoutShadows()->Destroy();

		for (auto& texture : textures)
			texture.Destroy();

		for (auto& fb : framebuffers)
			fb.Destroy();

		uniformBuffer->Destroy();
		pipeline.destroy();
	}

	void Shadows::update(Camera& camera)
	{
		if (GUI::shadow_cast)
		{
			CalculateCascades(camera);
			uniformBuffer->CopyRequest<Launch::AsyncDeferred>({ cascades, SHADOWMAP_CASCADES * sizeof(mat4), 0 });
		}
	}

	constexpr float cascadeSplitLambda = 0.95f;
	void Shadows::CalculateCascades(Camera& camera)
	{
		float cascadeSplits[SHADOWMAP_CASCADES];

		float nearClip = camera.nearPlane;
		float farClip = camera.farPlane;
		float clipRange = farClip - nearClip;

		float minZ = nearClip;
		float maxZ = nearClip + clipRange;

		float range = maxZ - minZ;
		float ratio = maxZ / minZ;

		// Calculate split depths based on view camera frustum
		// Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
		for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++) {
			float p = (i + 1) / static_cast<float>(SHADOWMAP_CASCADES);
			float log = minZ * std::pow(ratio, p);
			float uniform = minZ + range * p;
			float d = cascadeSplitLambda * (log - uniform) + uniform;
			cascadeSplits[i] = (d - nearClip) / clipRange;

			//cascadeSplits[i] = ((i + 1) / (float)SHADOWMAP_CASCADES);
			//cascadeSplits[i] = std::pow(cascadeSplits[i], 2.5f);
		}

		// Calculate orthographic projection matrix for each cascade
		float lastSplitDist = 0.0;
		for (uint32_t i = 0; i < SHADOWMAP_CASCADES; i++) {
			float splitDist = cascadeSplits[i];

			vec3 frustumCorners[8] = {
				vec3(-1.0f, +1.0f, -1.0f),
				vec3(+1.0f, +1.0f, -1.0f),
				vec3(+1.0f, -1.0f, -1.0f),
				vec3(-1.0f, -1.0f, -1.0f),
				vec3(-1.0f, +1.0f, +1.0f),
				vec3(+1.0f, +1.0f, +1.0f),
				vec3(+1.0f, -1.0f, +1.0f),
				vec3(-1.0f, -1.0f, +1.0f),
			};

			// Project frustum corners into world space
			auto& renderArea = Context::Get()->GetSystem<RendererSystem>()->GetRenderArea();
			const float aspect = renderArea.viewport.width / renderArea.viewport.height;
			mat4 projection = perspective(camera.FovxToFovy(camera.FOV, aspect), aspect, nearClip, farClip, false);
			mat4 view = lookAt(camera.position, camera.position + camera.front, camera.WorldUp());
			mat4 invVP = inverse(projection * view);
			for (uint32_t i = 0; i < 8; i++) {
				vec4 invCorner = invVP * vec4(frustumCorners[i], 1.0f);
				frustumCorners[i] = invCorner / invCorner.w;
			}

			for (uint32_t i = 0; i < 4; i++) {
				vec3 dist = frustumCorners[i + 4] - frustumCorners[i];
				frustumCorners[i + 4] = frustumCorners[i] + (dist * splitDist);
				frustumCorners[i] = frustumCorners[i] + (dist * lastSplitDist);
			}

			// Get frustum center
			vec3 frustumCenter = vec3(0.0f);
			for (uint32_t i = 0; i < 8; i++) {
				frustumCenter += frustumCorners[i];
			}
			frustumCenter /= 8.0f;

			float radius = 0.0f;
			for (uint32_t i = 0; i < 8; i++) {
				float distance = length(frustumCorners[i] - frustumCenter);
				radius = maximum(radius, distance);
			}
			radius = std::ceil(radius * 16.0f) / 16.0f;

			vec3 maxExtents = vec3(radius);
			vec3 minExtents = -maxExtents;

			vec3 lightDir = -normalize(vec3((float*)&GUI::sun_direction));
			auto v0 = frustumCenter - (lightDir * radius);
			mat4 lightViewMatrix = lookAt(frustumCenter - (lightDir * radius), frustumCenter, camera.WorldUp());
			mat4 lightOrthoMatrix = ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, -clipRange, maxExtents.z - minExtents.z, GlobalSettings::ReverseZ);
			//mat4 lightOrthoMatrix = ortho(-20.f, 20.f, -20.f, 20.f, -700.f, 700.f, GlobalSettings::ReverseZ);

			// Store split distance and matrix in cascade
			cascades[i] = lightOrthoMatrix * lightViewMatrix;
			viewZ[i] = (camera.nearPlane + splitDist * clipRange) * cascadeSplitLambda;

			lastSplitDist = cascadeSplits[i];
		}
	}
}
#endif
