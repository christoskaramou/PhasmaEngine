#include "TAA.h"

using namespace vm;

void TAA::Init()
{
	if (initialized) return;

	previous.format = vk::Format::eR8G8B8A8Unorm;
	previous.initialLayout = vk::ImageLayout::eUndefined;
	previous.createImage(WIDTH, HEIGHT, vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	previous.transitionImageLayout(vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal);
	previous.createImageView(vk::ImageAspectFlagBits::eColor);
	previous.createSampler();

	initialized = true;
}

void TAA::createUniforms(std::map<std::string, Image>& renderTargets)
{
}

void TAA::updateDescriptorSets(std::map<std::string, Image>& renderTargets)
{
}

void TAA::draw(uint32_t imageIndex)
{
}

void TAA::createRenderPasses(std::map<std::string, Image>& renderTargets)
{
	std::array<vk::AttachmentDescription, 2> attachments{};
	// swapchain image
	attachments[0].format = vulkan->surface->formatKHR.format;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::ePresentSrcKHR;
	// composition2 render target
	attachments[1].format = renderTargets["composition2"].format;
	attachments[1].samples = vk::SampleCountFlagBits::e1;
	attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[1].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[1].initialLayout = vk::ImageLayout::eUndefined;
	attachments[1].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

	std::vector<vk::AttachmentReference> colorReferences{
		{ 0, vk::ImageLayout::eColorAttachmentOptimal },
		{ 1, vk::ImageLayout::eColorAttachmentOptimal }
	};

	vk::SubpassDescription subpassDescription;
	subpassDescription.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
	subpassDescription.pColorAttachments = colorReferences.data();
	subpassDescription.pDepthStencilAttachment = nullptr;

	vk::RenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;

	renderPass = vulkan->device.createRenderPass(renderPassInfo);
}

void TAA::createFrameBuffers(std::map<std::string, Image>& renderTargets)
{
}

void TAA::createPipelines(std::map<std::string, Image>& renderTargets)
{
}

void TAA::copyImage(const vk::CommandBuffer& cmd, Image& source)
{
	// change image layouts for copy
	vk::ImageMemoryBarrier barrier;
	barrier.image = previous.image;
	barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, previous.mipLevels, 0, previous.arrayLayers };

	cmd.pipelineBarrier(
		vk::PipelineStageFlagBits::eFragmentShader,
		vk::PipelineStageFlagBits::eTransfer,
		vk::DependencyFlagBits::eByRegion,
		nullptr,
		nullptr,
		barrier
	);

	barrier.image = source.image;
	barrier.oldLayout = source.layoutState == LayoutState::Read ? vk::ImageLayout::eShaderReadOnlyOptimal : vk::ImageLayout::eColorAttachmentOptimal;
	barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, source.mipLevels, 0, source.arrayLayers };

	cmd.pipelineBarrier(
		source.layoutState == LayoutState::Read ? vk::PipelineStageFlagBits::eFragmentShader : vk::PipelineStageFlagBits::eColorAttachmentOutput,
		vk::PipelineStageFlagBits::eTransfer,
		vk::DependencyFlagBits::eByRegion,
		nullptr,
		nullptr,
		barrier
	);

	// copy the image
	vk::ImageCopy region;
	region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	region.srcSubresource.layerCount = 1;
	region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	region.dstSubresource.layerCount = 1;
	region.extent.width = WIDTH;
	region.extent.height = HEIGHT;
	region.extent.depth = 1;

	cmd.copyImage(
		source.image,
		vk::ImageLayout::eTransferSrcOptimal,
		previous.image,
		vk::ImageLayout::eTransferDstOptimal,
		region
	);

	// change image layouts back to shader read
	barrier.image = previous.image;
	barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, previous.mipLevels, 0, previous.arrayLayers };

	cmd.pipelineBarrier(
		vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eFragmentShader,
		vk::DependencyFlagBits::eByRegion,
		nullptr,
		nullptr,
		barrier
	);

	barrier.image = source.image;
	barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
	barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, source.mipLevels, 0, source.arrayLayers };

	cmd.pipelineBarrier(
		vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eFragmentShader,
		vk::DependencyFlagBits::eByRegion,
		nullptr,
		nullptr,
		barrier
	);
	source.layoutState = LayoutState::Read;
}

void TAA::destroy()
{
}
