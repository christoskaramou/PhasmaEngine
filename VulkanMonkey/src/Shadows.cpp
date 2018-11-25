#include "../include/Shadows.h"
#include "../include/Errors.h"

using namespace vm;

vk::DescriptorSetLayout		Shadows::descriptorSetLayout = nullptr;
bool						Shadows::shadowCast = true;
uint32_t					Shadows::imageSize = 4096;

void Shadows::createDescriptorSet()
{
	auto allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(vulkan->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&descriptorSetLayout);
	VkCheck(vulkan->device.allocateDescriptorSets(&allocateInfo, &descriptorSet));

	vk::WriteDescriptorSet textureWriteSets[2];
	// MVP
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)										// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eUniformBufferDynamic)	// DescriptorType descriptorType;
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

	vulkan->device.updateDescriptorSets(2, textureWriteSets, 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";
}

vm::Shadows::Shadows(VulkanContext * vulkan) : vulkan(vulkan)
{ }

vk::DescriptorSetLayout Shadows::getDescriptorSetLayout(vk::Device device)
{
	if (!descriptorSetLayout) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};

		// binding for model mvp matrix
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eUniformBufferDynamic)
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
		VkCheck(device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
	}
	return descriptorSetLayout;
}

vk::RenderPass Shadows::getRenderPass()
{
	if (!renderPass) {
		auto attachment = vk::AttachmentDescription()
			.setFormat(vulkan->depth->format)
			.setSamples(vk::SampleCountFlagBits::e1)
			.setLoadOp(vk::AttachmentLoadOp::eClear)
			.setStoreOp(vk::AttachmentStoreOp::eStore)
			.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
			.setStencilStoreOp(vk::AttachmentStoreOp::eStore)
			.setInitialLayout(vk::ImageLayout::eUndefined)
			.setFinalLayout(vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal);

		auto const depthAttachmentRef = vk::AttachmentReference()
			.setAttachment(0)
			.setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

		auto const subpassDesc = vk::SubpassDescription() // subpass desc (there can be multiple subpasses)
			.setPDepthStencilAttachment(&depthAttachmentRef);

		std::vector<vk::SubpassDependency> spDependencies{
			vk::SubpassDependency{
				VK_SUBPASS_EXTERNAL,
				0,
				vk::PipelineStageFlagBits::eBottomOfPipe,
				vk::PipelineStageFlagBits::eLateFragmentTests,
				vk::AccessFlagBits::eMemoryRead,
				vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
				vk::DependencyFlagBits::eByRegion
			},
			vk::SubpassDependency{
				0,
				VK_SUBPASS_EXTERNAL,
				vk::PipelineStageFlagBits::eLateFragmentTests,
				vk::PipelineStageFlagBits::eBottomOfPipe,
				vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
				vk::AccessFlagBits::eMemoryRead,
				vk::DependencyFlagBits::eByRegion
			}
		};

		auto const rpci = vk::RenderPassCreateInfo()
			.setAttachmentCount(1)
			.setPAttachments(&attachment)
			.setSubpassCount(1)
			.setPSubpasses(&subpassDesc)
			.setDependencyCount((uint32_t)spDependencies.size())
			.setPDependencies(spDependencies.data());

		VkCheck(vulkan->device.createRenderPass(&rpci, nullptr, &renderPass));
		std::cout << "RenderPass created\n";
	}
	return renderPass;
}

void Shadows::createFrameBuffers(uint32_t bufferCount)
{
	frameBuffer.resize(bufferCount);
	texture.format = vulkan->depth->format;
	texture.initialLayout = vk::ImageLayout::eUndefined;
	texture.createImage(imageSize, imageSize, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);

	texture.createImageView(vk::ImageAspectFlagBits::eDepth);

	texture.addressMode = vk::SamplerAddressMode::eClampToEdge;
	texture.maxAnisotropy = 1.f;
	texture.borderColor = vk::BorderColor::eFloatOpaqueWhite;
	texture.samplerCompareEnable = VK_TRUE;
	texture.createSampler();

	for (uint32_t i = 0; i < bufferCount; ++i) {
		auto const fbci = vk::FramebufferCreateInfo()
			.setRenderPass(getRenderPass())
			.setAttachmentCount(1)
			.setPAttachments(&texture.view)
			.setWidth(imageSize)
			.setHeight(imageSize)
			.setLayers(1);
		VkCheck(vulkan->device.createFramebuffer(&fbci, nullptr, &frameBuffer[i]));
		std::cout << "Framebuffer created\n";
	}

}

void Shadows::createDynamicUniformBuffer(size_t num_of_objects)
{
	if (num_of_objects > 256) {
		std::cout << "256 objects for the Shadows Dynamic Uniform Buffer is the max for now\n";
		exit(-21);
	}
	size_t size = num_of_objects * 256;
	uniformBuffer.createBuffer(size, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	VkCheck(vulkan->device.mapMemory(uniformBuffer.memory, 0, uniformBuffer.size, vk::MemoryMapFlags(), &uniformBuffer.data));
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
	for (auto& fb : frameBuffer)
		vulkan->device.destroyFramebuffer(fb);
	uniformBuffer.destroy();
	pipeline.destroy();
}