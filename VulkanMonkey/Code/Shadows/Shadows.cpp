#include "Shadows.h"
#include "../GUI/GUI.h"
#include "../Light/Light.h"

using namespace vm;

vk::DescriptorSetLayout		Shadows::descriptorSetLayout = nullptr;
uint32_t					Shadows::imageSize = 4096;

void Shadows::createDescriptorSets()
{
	auto allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&descriptorSetLayout);

	descriptorSets.resize(3); // size of wanted number of cascaded shadows
	for (uint32_t i = 0; i < 3; i++) {
		descriptorSets[i] = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

		std::vector<vk::WriteDescriptorSet> textureWriteSets(2);
		// MVP
		textureWriteSets[0] = vk::WriteDescriptorSet()
			.setDstSet(descriptorSets[i])										// DescriptorSet dstSet;
			.setDstBinding(0)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)			// DescriptorType descriptorType;
			.setPBufferInfo(&vk::DescriptorBufferInfo()						// const DescriptorBufferInfo* pBufferInfo;
				.setBuffer(uniformBuffers[i].buffer)							// Buffer buffer;
				.setOffset(0)													// DeviceSize offset;
				.setRange(sizeof(ShadowsUBO)));									// DeviceSize range;
		// sampler
		textureWriteSets[1] = vk::WriteDescriptorSet()
			.setDstSet(descriptorSets[i])										// DescriptorSet dstSet;
			.setDstBinding(1)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(textures[i].sampler)									// Sampler sampler;
				.setImageView(textures[i].view)										// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal));// ImageLayout imageLayout;

		vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);
	}
}

vk::DescriptorSetLayout Shadows::getDescriptorSetLayout(vk::Device device)
{
	if (!descriptorSetLayout) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};

		// binding for model mvp matrix
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)
			.setStageFlags(vk::ShaderStageFlagBits::eVertex)); // which pipeline shader stages can access

		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(1) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access

		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		descriptorSetLayout = device.createDescriptorSetLayout(createInfo);
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
		vec3 p = Light::sun().position;

		vec3 pointOnPyramid = camera.front * (sideSizeOfPyramid * .01f);
		vec3 pos = p + camera.position + pointOnPyramid; // sun position will be moved, so its angle to the lookat position is the same always
		vec3 front = normalize(camera.position + pointOnPyramid - pos);
		vec3 right = normalize(cross(front, camera.worldUp()));
		vec3 up = normalize(cross(right, front));
		float orthoSide = sideSizeOfPyramid * .01f; // small area
		shadows_UBO = {
			ortho(-orthoSide, orthoSide, -orthoSide, orthoSide, 500.f, 0.005f),
			lookAt(pos, front, right, up),
			1.0f
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
			1.0f
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
			1.0f
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
