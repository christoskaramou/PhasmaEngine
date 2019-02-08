#include "Shadows.h"
#include "../GUI/GUI.h"
#include "../Light/Light.h"

using namespace vm;

vk::DescriptorSetLayout		Shadows::descriptorSetLayout = nullptr;
uint32_t					Shadows::imageSize = 4096;

void Shadows::createDescriptorSet()
{
	auto allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&descriptorSetLayout);
	descriptorSet = vulkan->device.allocateDescriptorSets(allocateInfo).at(0);

	std::vector<vk::WriteDescriptorSet> textureWriteSets(2);
	// MVP
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)										// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)			// DescriptorType descriptorType;
		.setPBufferInfo(&vk::DescriptorBufferInfo()						// const DescriptorBufferInfo* pBufferInfo;
			.setBuffer(uniformBuffer.buffer)							// Buffer buffer;
			.setOffset(0)													// DeviceSize offset;
			.setRange(sizeof(ShadowsUBO)));									// DeviceSize range;
	// sampler
	textureWriteSets[1] = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)										// DescriptorSet dstSet;
		.setDstBinding(1)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(texture.sampler)									// Sampler sampler;
			.setImageView(texture.view)										// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal));// ImageLayout imageLayout;

	vulkan->device.updateDescriptorSets(textureWriteSets, nullptr);
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

void Shadows::createUniformBuffer()
{
	uniformBuffer.createBuffer(sizeof(ShadowsUBO), vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	uniformBuffer.data = vulkan->device.mapMemory(uniformBuffer.memory, 0, uniformBuffer.size);
}

void Shadows::destroy()
{
	if (renderPass)
		vulkan->device.destroyRenderPass(renderPass);
	if (Shadows::descriptorSetLayout) {
		vulkan->device.destroyDescriptorSetLayout(Shadows::descriptorSetLayout);
		Shadows::descriptorSetLayout = nullptr;
	}
	texture.destroy();
	for (auto& fb : frameBuffers)
		vulkan->device.destroyFramebuffer(fb);
	uniformBuffer.destroy();
	pipeline.destroy();
}

void Shadows::update(Camera& camera)
{
	static ShadowsUBO shadows_UBO = {
			ortho(-20.f, 20.f, -20.f, 20.f, 500.f, 0.005f),
			lookAt(camera.position, camera.front, camera.right, camera.up),
			0.0f
	};

	if (GUI::shadow_cast) {
		// far/cos(x) = the side size
		const float sideSizeOfPyramid = camera.nearPlane / cos(radians(camera.FOV * .5f)); // near plane is actually the far plane (they are reversed)
		const vec3 centerOfPyramid = (camera.front * (sideSizeOfPyramid * .5f)) / 10.f;
		const vec3 p = Light::sun().position;
		const vec3 pos = p + camera.position + centerOfPyramid;
		const vec3 front = normalize(camera.position + centerOfPyramid - pos);
		const vec3 right = normalize(cross(front, camera.worldUp()));
		const vec3 up = normalize(cross(right, front));

		const float orthoSide = (sideSizeOfPyramid * .5f) / 10.f;
		shadows_UBO = {
			ortho(-orthoSide, orthoSide, -orthoSide, orthoSide, 500.f, 0.005f),
			lookAt(pos, front, right, up),
			1.0f
		};
		memcpy(uniformBuffer.data, &shadows_UBO, sizeof(ShadowsUBO));
	}
	else
	{
		shadows_UBO.castShadows = 0.f;
		memcpy(uniformBuffer.data, &shadows_UBO, sizeof(ShadowsUBO));
	}
}
