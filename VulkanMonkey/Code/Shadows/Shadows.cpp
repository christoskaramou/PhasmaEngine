#include "Shadows.h"
#include "../GUI/GUI.h"

using namespace vm;

vk::DescriptorSetLayout		Shadows::descriptorSetLayout = nullptr;
uint32_t					Shadows::imageSize = 4096;

void Shadows::createDescriptorSets()
{
	vk::DescriptorSetAllocateInfo allocateInfo;
	allocateInfo.descriptorPool = vulkan->descriptorPool;
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = &descriptorSetLayout;

	descriptorSets.resize(3); // size of wanted number of cascaded shadows
	for (uint32_t i = 0; i < 3; i++) {
		descriptorSets[i] = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

		std::vector<vk::WriteDescriptorSet> textureWriteSets(2);
		// MVP
		vk::DescriptorBufferInfo dbi;
		dbi.buffer = uniformBuffers[i].buffer;
		dbi.offset = 0;
		dbi.range = sizeof(ShadowsUBO);

		textureWriteSets[0].dstSet = descriptorSets[i];
		textureWriteSets[0].dstBinding = 0;
		textureWriteSets[0].dstArrayElement = 0;
		textureWriteSets[0].descriptorCount = 1;
		textureWriteSets[0].descriptorType = vk::DescriptorType::eUniformBuffer;
		textureWriteSets[0].pBufferInfo = &dbi;

		// sampler
		vk::DescriptorImageInfo dii;
		dii.sampler = textures[i].sampler;
		dii.imageView = textures[i].view;
		dii.imageLayout = vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal;

		textureWriteSets[1].dstSet = descriptorSets[i];
		textureWriteSets[1].dstBinding = 1;
		textureWriteSets[1].dstArrayElement = 0;
		textureWriteSets[1].descriptorCount = 1;
		textureWriteSets[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
		textureWriteSets[1].pImageInfo = &dii;

		vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);
	}
}

vk::DescriptorSetLayout Shadows::getDescriptorSetLayout(vk::Device device)
{
	if (!descriptorSetLayout) {
		auto layoutBinding = [](uint32_t binding, vk::DescriptorType descriptorType, vk::ShaderStageFlags stageFlags) {
			return vk::DescriptorSetLayoutBinding{ binding, descriptorType, 1, stageFlags, nullptr };
		};
		std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings{
			layoutBinding(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex),
			layoutBinding(1, vk::DescriptorType::eCombinedImageSampler,vk::ShaderStageFlagBits::eFragment),
		};
		vk::DescriptorSetLayoutCreateInfo descriptorLayout;
		descriptorLayout.bindingCount = (uint32_t)setLayoutBindings.size();
		descriptorLayout.pBindings = setLayoutBindings.data();
		descriptorSetLayout = VulkanContext::get().device.createDescriptorSetLayout(descriptorLayout);
	}
	return descriptorSetLayout;
}

void Shadows::createUniformBuffers()
{
	uniformBuffers.resize(3); // 3 buffers for the 3 shadow render passes
	for (auto& buffer : uniformBuffers) {
		buffer.createBuffer(sizeof(ShadowsUBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		buffer.data = vulkan->device.mapMemory(buffer.memory, 0, buffer.size);
	}
}

void Shadows::destroy()
{
	if (renderPass)
		vulkan->device.destroyRenderPass(renderPass);
	if (Shadows::descriptorSetLayout) {
		vulkan->device.destroyDescriptorSetLayout(Shadows::descriptorSetLayout);
		Shadows::descriptorSetLayout = nullptr;
	}
	for (auto& texture : textures)
		texture.destroy();
	for (auto& fb : frameBuffers)
		vulkan->device.destroyFramebuffer(fb);
	for (auto& buffer : uniformBuffers)
		buffer.destroy();
	pipeline.destroy();
}

void Shadows::update(Camera& camera)
{
	static ShadowsUBO shadows_UBO = {};

	if (GUI::shadow_cast) {
		// far/cos(x) = the side size
		float sideSizeOfPyramid = camera.nearPlane / cos(radians(camera.FOV * .5f)); // near plane is actually the far plane (they are reversed)
		vec3 p = (float*)&GUI::sun_position;

		vec3 pointOnPyramid = camera.front * (sideSizeOfPyramid * .01f);
		vec3 pos = p + camera.position + pointOnPyramid; // sun position will be moved, so its angle to the lookat position is the same always
		vec3 front = normalize(camera.position + pointOnPyramid - pos);
		vec3 right = normalize(cross(front, camera.worldUp()));
		vec3 up = normalize(cross(right, front));
		float orthoSide = sideSizeOfPyramid * .01f; // small area
		shadows_UBO = {
			ortho(-orthoSide, orthoSide, -orthoSide, orthoSide, 500.f, 0.005f),
			lookAt(pos, front, right, up),
			1.0f,
			sideSizeOfPyramid * .02f,
			sideSizeOfPyramid * .1f,
			sideSizeOfPyramid
		};
		memcpy(uniformBuffers[0].data, &shadows_UBO, sizeof(ShadowsUBO));

		pointOnPyramid = camera.front * (sideSizeOfPyramid * .05f);
		pos = p + camera.position + pointOnPyramid; 
		front = normalize(camera.position + pointOnPyramid - pos);
		right = normalize(cross(front, camera.worldUp()));
		up = normalize(cross(right, front));
		orthoSide = sideSizeOfPyramid * .05f; // medium area
		shadows_UBO = {
			ortho(-orthoSide, orthoSide, -orthoSide, orthoSide, 500.f, 0.005f),
			lookAt(pos, front, right, up),
			1.0f,
			sideSizeOfPyramid * .02f,
			sideSizeOfPyramid * .1f,
			sideSizeOfPyramid 
		};
		memcpy(uniformBuffers[1].data, &shadows_UBO, sizeof(ShadowsUBO));

		pointOnPyramid = camera.front * (sideSizeOfPyramid * .5f);
		pos = p + camera.position + pointOnPyramid;
		front = normalize(camera.position + pointOnPyramid - pos);
		right = normalize(cross(front, camera.worldUp()));
		up = normalize(cross(right, front));
		orthoSide = sideSizeOfPyramid * .5f; // large area
		shadows_UBO = {
			ortho(-orthoSide, orthoSide, -orthoSide, orthoSide, 500.f, 0.005f),
			lookAt(pos, front, right, up),
			1.0f,
			sideSizeOfPyramid * .02f,
			sideSizeOfPyramid * .1f,
			sideSizeOfPyramid
		};
		memcpy(uniformBuffers[2].data, &shadows_UBO, sizeof(ShadowsUBO));
	}
	else
	{
		shadows_UBO.castShadows = 0.f;
		memcpy(uniformBuffers[0].data, &shadows_UBO, sizeof(ShadowsUBO));
		memcpy(uniformBuffers[1].data, &shadows_UBO, sizeof(ShadowsUBO));
		memcpy(uniformBuffers[2].data, &shadows_UBO, sizeof(ShadowsUBO));
	}
}
