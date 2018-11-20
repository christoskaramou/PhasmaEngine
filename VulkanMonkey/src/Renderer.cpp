#include "../include/Renderer.h"
#include "../include/Errors.h"
#include "../include/Math.h"
#include <iostream>
#include <random>
#include <chrono>

using namespace vm;

Renderer::Renderer(SDL_Window* window)
{
	ctx.window = window;

	// INIT ALL VULKAN CONTEXT
	ctx.initVulkanContext();
	// INIT RENDERING
	ctx.initRendering();
	//LOAD RESOURCES
	ctx.loadResources();
	// CREATE UNIFORMS AND DESCRIPTOR SETS
	ctx.createUniforms();

	// init is done
	prepared = true;
}

Renderer::~Renderer()
{
    ctx.device.waitIdle();

	for (auto &model : ctx.models)
		model.destroy(ctx.device);
	ctx.shadows.destroy(ctx.device);
	ctx.compute.destroy(ctx.device);
	ctx.forward.destroy(ctx.device);
	ctx.deferred.destroy(ctx.device);
	ctx.ssr.destroy(ctx.device);
	ctx.ssao.destroy(ctx.device);
	ctx.skyBox.destroy(ctx.device);
	ctx.terrain.destroy(ctx.device);
	ctx.gui.destroy(ctx.device);
	ctx.lightUniforms.destroy(ctx.device);
	ctx.destroyVkContext();
}

void Renderer::update(float delta)
{
	const vm::mat4 projection = ctx.mainCamera.getPerspective();
	const vm::mat4 view = ctx.mainCamera.getView();

	//TERRAIN
	if (ctx.terrain.render) {
		const vm::mat4 pvm[3]{ projection, view };
		memcpy(ctx.terrain.uniformBuffer.data, &pvm, sizeof(pvm));
	}

	// MODELS
	for (auto &model : ctx.models) {
		std::sort(model.meshes.begin(), model.meshes.end(), [](Mesh& a, Mesh& b) -> bool { return a.colorEffects.diffuse.w > b.colorEffects.diffuse.w; });
		if (model.render) {
			const vm::mat4 pvm[3]{ projection, view, model.matrix };
			ctx.mainCamera.ExtractFrustum(model.matrix);
			for (auto &mesh : model.meshes) {
				mesh.cull = !ctx.mainCamera.SphereInFrustum(mesh.boundingSphere);
				if (!mesh.cull)
					memcpy(model.uniformBuffer.data, &pvm, sizeof(pvm));
			}
		}
	}

	// SKYBOX
	if (ctx.skyBox.render) {
		const vm::mat4 pvm[2]{ projection, view };
		memcpy(ctx.skyBox.uniformBuffer.data, &pvm, sizeof(pvm));
	}

	// SHADOWS
	const vm::vec3 pos = Light::sun().position;
	const vm::vec3 front = vm::normalize(-pos);
	const vm::vec3 right = vm::normalize(vm::cross(front, ctx.mainCamera.worldUp()));
	const vm::vec3 up = vm::normalize(vm::cross(right, front));
	std::vector<ShadowsUBO> shadows_UBO(ctx.models.size());
	for (uint32_t i = 0; i < ctx.models.size(); i++)
		shadows_UBO[i] = { vm::ortho(-4.f, 4.f, -4.f, 4.f, 0.1f, 10.f), vm::lookAt(Light::sun().position, front, right, up), ctx.models[i].matrix, Shadows::shadowCast ? 1.0f : 0.0f };
	memcpy(ctx.shadows.uniformBuffer.data, shadows_UBO.data(), sizeof(ShadowsUBO)*shadows_UBO.size());

	// GUI
	if (ctx.gui.render)
		ctx.gui.newFrame(ctx.device, ctx.gpu, ctx.window);

	// LIGHTS
	vm::vec4 camPos(ctx.mainCamera.position, 1.0f);
	memcpy(ctx.lightUniforms.uniform.data, &camPos, sizeof(vm::vec4));

	// SSAO
	if (useSSAO) {
		vm::mat4 pvm[2]{ projection, view };
		memcpy(ctx.ssao.UBssaoPVM.data, pvm, sizeof(pvm));
	}

	// REFLECTIONS
	if (useSSR) {
		vm::mat4 reflectionInput[3];
		reflectionInput[0][0] = vm::vec4(ctx.mainCamera.position, 1.0f);
		reflectionInput[0][1] = vm::vec4(ctx.mainCamera.front(), 1.0f);
		reflectionInput[0][2] = vm::vec4(static_cast<float>(ctx.surface.actualExtent.width), static_cast<float>(ctx.surface.actualExtent.height), 0.f, 0.f);
		reflectionInput[0][3] = vm::vec4();
		reflectionInput[1] = projection;
		reflectionInput[2] = view;
		memcpy(ctx.ssr.UBReflection.data, &reflectionInput, sizeof(reflectionInput));
	}
}

void Renderer::recordComputeCmds(const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ)
{
	auto beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
		.setPInheritanceInfo(nullptr);
	VkCheck(ctx.computeCmdBuffer.begin(&beginInfo));

	ctx.computeCmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, ctx.compute.pipeline.pipeline);
	ctx.computeCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, ctx.compute.pipeline.compinfo.layout, 0, 1, &ctx.compute.DSCompute, 0, nullptr);

	ctx.computeCmdBuffer.dispatch(sizeX, sizeY, sizeZ);
	ctx.computeCmdBuffer.end();
}

void Renderer::recordForwardCmds(const uint32_t& imageIndex)
{
	// Render Pass (color)
	std::vector<vk::ClearValue> clearValues = {
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearDepthStencilValue({ 1.0f, 0 }) };

	auto beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
		.setPInheritanceInfo(nullptr);
	auto renderPassInfo = vk::RenderPassBeginInfo()
		.setRenderPass(ctx.forward.renderPass)
		.setFramebuffer(ctx.forward.frameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, ctx.surface.actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues.size()))
		.setPClearValues(clearValues.data());

	VkCheck(ctx.dynamicCmdBuffer.begin(&beginInfo));
	ctx.dynamicCmdBuffer.beginRenderPass(&renderPassInfo, vk::SubpassContents::eInline);

	// TERRAIN
	ctx.terrain.draw(ctx.terrain.pipeline, ctx.dynamicCmdBuffer);
	// MODELS
	for (uint32_t m = 0; m < ctx.models.size(); m++)
		ctx.models[m].draw(ctx.forward.pipeline, ctx.dynamicCmdBuffer, m, false, &ctx.shadows, &ctx.lightUniforms.descriptorSet);
	// SKYBOX
	ctx.skyBox.draw(ctx.skyBox.pipeline, ctx.dynamicCmdBuffer);
	ctx.dynamicCmdBuffer.endRenderPass();

	// GUI
	ctx.gui.draw(ctx.gui.renderPass, ctx.gui.frameBuffers[imageIndex], ctx.surface, ctx.gui.pipeline, ctx.dynamicCmdBuffer);

	ctx.dynamicCmdBuffer.end();
}

void Renderer::recordDeferredCmds(const uint32_t& imageIndex)
{

	auto beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
		.setPInheritanceInfo(nullptr);
	VkCheck(ctx.dynamicCmdBuffer.begin(&beginInfo));

	// Begin deferred
	std::vector<vk::ClearValue> clearValues = {
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }),
		vk::ClearDepthStencilValue({ 1.0f, 0 }) };
	auto renderPassInfo = vk::RenderPassBeginInfo()
		.setRenderPass(ctx.deferred.renderPass)
		.setFramebuffer(ctx.deferred.frameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, ctx.surface.actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues.size()))
		.setPClearValues(clearValues.data());

	ctx.dynamicCmdBuffer.beginRenderPass(&renderPassInfo, vk::SubpassContents::eInline);

	vk::Viewport viewport;
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = static_cast<float>(ctx.surface.actualExtent.width);
	viewport.height = static_cast<float>(ctx.surface.actualExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	ctx.dynamicCmdBuffer.setViewport(0, 1, &viewport);

	vk::Rect2D scissor;
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = ctx.surface.actualExtent.width;
	scissor.extent.height = ctx.surface.actualExtent.height;
	ctx.dynamicCmdBuffer.setScissor(0, 1, &scissor);

	// MODELS
	for (uint32_t m = 0; m < ctx.models.size(); m++)
		ctx.models[m].draw(ctx.deferred.pipeline, ctx.dynamicCmdBuffer, m, true);
	ctx.dynamicCmdBuffer.endRenderPass();
	// End deferred

	// Begin SCREEN SPACE AMBIENT OCCLUSION
	if (useSSAO) {
		// SSAO image
		std::vector<vk::ClearValue> clearValuesSSAO = {
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }) };
		auto renderPassInfoSSAO = vk::RenderPassBeginInfo()
			.setRenderPass(ctx.ssao.renderPass)
			.setFramebuffer(ctx.ssao.frameBuffers[imageIndex])
			.setRenderArea({ { 0, 0 }, ctx.surface.actualExtent })
			.setClearValueCount(static_cast<uint32_t>(clearValuesSSAO.size()))
			.setPClearValues(clearValuesSSAO.data());
		ctx.dynamicCmdBuffer.beginRenderPass(&renderPassInfoSSAO, vk::SubpassContents::eInline);
		ctx.dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.ssao.pipeline.pipeline);
		const vk::DescriptorSet descriptorSets[] = { ctx.ssao.DSssao };
		const uint32_t dOffsets[] = { 0 };
		ctx.dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.ssao.pipeline.pipeinfo.layout, 0, 1, descriptorSets, 0, dOffsets);
		ctx.dynamicCmdBuffer.draw(3, 1, 0, 0);
		ctx.dynamicCmdBuffer.endRenderPass();
		
		// new blurry SSAO image
		std::vector<vk::ClearValue> clearValuesSSAOBlur = {
		vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }) };
		auto renderPassInfoSSAOBlur = vk::RenderPassBeginInfo()
			.setRenderPass(ctx.ssao.blurRenderPass)
			.setFramebuffer(ctx.ssao.blurFrameBuffers[imageIndex])
			.setRenderArea({ { 0, 0 }, ctx.surface.actualExtent })
			.setClearValueCount(static_cast<uint32_t>(clearValuesSSAOBlur.size()))
			.setPClearValues(clearValuesSSAOBlur.data());
		ctx.dynamicCmdBuffer.beginRenderPass(&renderPassInfoSSAOBlur, vk::SubpassContents::eInline);
		ctx.dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.ssao.pipelineBlur.pipeline);
		const vk::DescriptorSet descriptorSetsBlur[] = { ctx.ssao.DSssaoBlur };
		const uint32_t dOffsetsBlur[] = { 0 };
		ctx.dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.ssao.pipelineBlur.pipeinfo.layout, 0, 1, descriptorSetsBlur, 0, dOffsetsBlur);
		ctx.dynamicCmdBuffer.draw(3, 1, 0, 0);
		ctx.dynamicCmdBuffer.endRenderPass();
	}
	// End SCREEN SPACE AMBIENT OCCLUSION

	// Begin Composition
	std::vector<vk::ClearValue> clearValues0 = {
	vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }) };
	auto renderPassInfo0 = vk::RenderPassBeginInfo()
		.setRenderPass(ctx.deferred.compositionRenderPass)
		.setFramebuffer(ctx.deferred.compositionFrameBuffers[imageIndex])
		.setRenderArea({ { 0, 0 }, ctx.surface.actualExtent })
		.setClearValueCount(static_cast<uint32_t>(clearValues0.size()))
		.setPClearValues(clearValues0.data());
	ctx.dynamicCmdBuffer.beginRenderPass(&renderPassInfo0, vk::SubpassContents::eInline);
	float ssao = useSSAO ? 1.f : 0.f;
	ctx.dynamicCmdBuffer.pushConstants(ctx.deferred.pipelineComposition.pipeinfo.layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(float), &ssao);
	ctx.dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.deferred.pipelineComposition.pipeline);
	const vk::DescriptorSet descriptorSets[] = { ctx.deferred.DSComposition, ctx.shadows.descriptorSet };
	const uint32_t dOffsets[] = { 0 };
	ctx.dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.deferred.pipelineComposition.pipeinfo.layout, 0, 2, descriptorSets, 1, dOffsets);
	ctx.dynamicCmdBuffer.draw(3, 1, 0, 0);
	ctx.dynamicCmdBuffer.endRenderPass();
	// End Composition

	// SCREEN SPACE REFLECTIONS
	if (useSSR) {
		std::vector<vk::ClearValue> clearValues1 = {
			vk::ClearColorValue().setFloat32({ 0.0f, 0.0f, 0.0f, 0.0f }) };
		auto renderPassInfo1 = vk::RenderPassBeginInfo()
			.setRenderPass(ctx.ssr.renderPass)
			.setFramebuffer(ctx.ssr.frameBuffers[imageIndex])
			.setRenderArea({ { 0, 0 }, ctx.surface.actualExtent })
			.setClearValueCount(static_cast<uint32_t>(clearValues1.size()))
			.setPClearValues(clearValues1.data());
		ctx.dynamicCmdBuffer.beginRenderPass(&renderPassInfo1, vk::SubpassContents::eInline);
		ctx.dynamicCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.ssr.pipeline.pipeline);
		ctx.dynamicCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.ssr.pipeline.pipeinfo.layout, 0, 1, &ctx.ssr.DSReflection, 0, nullptr);
		ctx.dynamicCmdBuffer.draw(3, 1, 0, 0);
		ctx.dynamicCmdBuffer.endRenderPass();
	}

	// GUI
	ctx.gui.draw(ctx.gui.renderPass, ctx.gui.frameBuffers[imageIndex], ctx.surface, ctx.gui.pipeline, ctx.dynamicCmdBuffer);

	ctx.dynamicCmdBuffer.end();
}

void Renderer::recordShadowsCmds(const uint32_t& imageIndex)
{
	// Render Pass (shadows mapping) (outputs the depth image with the light POV)

	std::array<vk::ClearValue, 1> clearValuesShadows = {};
	clearValuesShadows[0].setDepthStencil({ 1.0f, 0 });
	auto beginInfoShadows = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
		.setPInheritanceInfo(nullptr);
	auto renderPassInfoShadows = vk::RenderPassBeginInfo()
		.setRenderPass(Shadows::getRenderPass(ctx.device, ctx.depth))
		.setFramebuffer(ctx.shadows.frameBuffer[imageIndex])
		.setRenderArea({ { 0, 0 },{ ctx.shadows.imageSize, ctx.shadows.imageSize } })
		.setClearValueCount(static_cast<uint32_t>(clearValuesShadows.size()))
		.setPClearValues(clearValuesShadows.data());

	VkCheck(ctx.shadowCmdBuffer.begin(&beginInfoShadows));
	ctx.shadowCmdBuffer.beginRenderPass(&renderPassInfoShadows, vk::SubpassContents::eInline);

	vk::DeviceSize offset = vk::DeviceSize();

	ctx.shadowCmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.shadows.pipeline.pipeline);

	for (uint32_t m = 0; m < ctx.models.size(); m++) {
		if (ctx.models[m].render) {
			ctx.shadowCmdBuffer.bindVertexBuffers(0, 1, &ctx.models[m].vertexBuffer.buffer, &offset);
			ctx.shadowCmdBuffer.bindIndexBuffer(ctx.models[m].indexBuffer.buffer, 0, vk::IndexType::eUint32);

			const uint32_t dOffsets[] = { m * sizeof(ShadowsUBO) };
			ctx.shadowCmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.shadows.pipeline.pipeinfo.layout, 0, 1, &ctx.shadows.descriptorSet, 1, dOffsets);

			for (auto& mesh : ctx.models[m].meshes) {
				if (mesh.render)
					ctx.shadowCmdBuffer.drawIndexed(static_cast<uint32_t>(mesh.indices.size()), 1, mesh.indexOffset, mesh.vertexOffset, 0);
			}
		}
	}

	ctx.shadowCmdBuffer.endRenderPass();
	ctx.shadowCmdBuffer.end();
}

void Renderer::present()
{
	if (!prepared) return;

	if (useCompute) {
		recordComputeCmds(2, 2, 1);
		auto const siCompute = vk::SubmitInfo()
			.setCommandBufferCount(1)
			.setPCommandBuffers(&ctx.computeCmdBuffer);
		VkCheck(ctx.computeQueue.submit(1, &siCompute, ctx.fences[1]));
		ctx.device.waitForFences(1, &ctx.fences[1], VK_TRUE, UINT64_MAX);
		ctx.device.resetFences(1, &ctx.fences[1]);
	}

	// what stage of a pipeline at a command buffer to wait for the semaphores to be done until keep going
	const vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

    uint32_t imageIndex;

	// if using shadows use the semaphore[0], record and submit the shadow commands, else use the semaphore[1]
	if (Shadows::shadowCast) {
		VkCheck(ctx.device.acquireNextImageKHR(ctx.swapchain.swapchain, UINT64_MAX, ctx.semaphores[0], vk::Fence(), &imageIndex));

		recordShadowsCmds(imageIndex);

		auto const siShadows = vk::SubmitInfo()
			.setWaitSemaphoreCount(1)
			.setPWaitSemaphores(&ctx.semaphores[0])
			.setPWaitDstStageMask(waitStages)
			.setCommandBufferCount(1)
			.setPCommandBuffers(&ctx.shadowCmdBuffer)
			.setSignalSemaphoreCount(1)
			.setPSignalSemaphores(&ctx.semaphores[1]);
		VkCheck(ctx.graphicsQueue.submit(1, &siShadows, nullptr));
	}
	else
		VkCheck(ctx.device.acquireNextImageKHR(ctx.swapchain.swapchain, UINT64_MAX, ctx.semaphores[1], vk::Fence(), &imageIndex));

	if (useDeferredRender) {
		// use the deferred command buffer
		recordDeferredCmds(imageIndex);
	}
	else {
		// use the dynamic command buffer
		recordForwardCmds(imageIndex);
	}

	// submit the main command buffer
	auto const si = vk::SubmitInfo()
		.setWaitSemaphoreCount(1)
		.setPWaitSemaphores(&ctx.semaphores[1])
		.setPWaitDstStageMask(waitStages)
		.setCommandBufferCount(1)
		.setPCommandBuffers(&ctx.dynamicCmdBuffer)
		.setSignalSemaphoreCount(1)
		.setPSignalSemaphores(&ctx.semaphores[2]);
	VkCheck(ctx.graphicsQueue.submit(1, &si, ctx.fences[0]));

    // Presentation
	auto const pi = vk::PresentInfoKHR()
		.setWaitSemaphoreCount(1)
		.setPWaitSemaphores(&ctx.semaphores[2])
		.setSwapchainCount(1)
		.setPSwapchains(&ctx.swapchain.swapchain)
		.setPImageIndices(&imageIndex)
		.setPResults(nullptr); //optional
	VkCheck(ctx.presentQueue.presentKHR(&pi));

	ctx.device.waitForFences(1, &ctx.fences[0], VK_TRUE, UINT64_MAX);
	ctx.device.resetFences(1, &ctx.fences[0]);

	if (overloadedGPU)
		ctx.presentQueue.waitIdle(); // user set, when GPU can't catch the CPU commands 
}