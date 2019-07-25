#include "Renderer.h"
#include "../Event/Event.h"

using namespace vm;

Renderer::Renderer(SDL_Window* window)
{
	ctx.vulkan.window = window;
	// INIT ALL VULKAN CONTEXT
	ctx.initVulkanContext();
	// INIT RENDERING
	ctx.initRendering();
	//LOAD RESOURCES
	ctx.loadResources();
	// CREATE UNIFORMS AND DESCRIPTOR SETS
	ctx.createUniforms();
}

Renderer::~Renderer()
{
	ctx.vulkan.device.waitIdle();
	if (Model::models.size() == 0) {
		if (Model::descriptorSetLayout) {
			ctx.vulkan.device.destroyDescriptorSetLayout(Model::descriptorSetLayout);
			Model::descriptorSetLayout = nullptr;
		}
		if (Mesh::descriptorSetLayout) {
			ctx.vulkan.device.destroyDescriptorSetLayout(Mesh::descriptorSetLayout);
			Mesh::descriptorSetLayout = nullptr;
		}

		if (Primitive::descriptorSetLayout) {
			ctx.vulkan.device.destroyDescriptorSetLayout(Primitive::descriptorSetLayout);
			Primitive::descriptorSetLayout = nullptr;
		}
	}
#ifdef USE_SCRIPTS
	for (auto& script : ctx.scripts)
		delete script;
#endif
	for (auto &model : Model::models)
		model.destroy();
	for (auto& texture : Mesh::uniqueTextures)
		texture.second.destroy();
	Mesh::uniqueTextures.clear();

	ctx.computePool.destroy();
	ctx.shadows.destroy();
	ctx.deferred.destroy();
	ctx.ssao.destroy();
	ctx.ssr.destroy();
	ctx.fxaa.destroy();
	ctx.taa.destroy();
	ctx.bloom.destroy();
	ctx.motionBlur.destroy();
	ctx.skyBoxDay.destroy();
	ctx.skyBoxNight.destroy();
	ctx.gui.destroy();
	ctx.lightUniforms.destroy();
	for (auto& metric : ctx.metrics)
		metric.destroy();
	ctx.destroyVkContext();
}

void vm::Renderer::changeLayout(vk::CommandBuffer cmd, Image& image, LayoutState state)
{
	if (state != image.layoutState) {
		if (state == LayoutState::ColorRead) {
			image.transitionImageLayout(
				cmd,
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::PipelineStageFlagBits::eColorAttachmentOutput,
				vk::PipelineStageFlagBits::eFragmentShader,
				vk::AccessFlagBits::eColorAttachmentWrite,
				vk::AccessFlagBits::eShaderRead,
				vk::ImageAspectFlagBits::eColor
				);
		}
		else if (state == LayoutState::ColorWrite) {
			image.transitionImageLayout(
				cmd,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::PipelineStageFlagBits::eFragmentShader,
				vk::PipelineStageFlagBits::eColorAttachmentOutput,
				vk::AccessFlagBits::eShaderRead,
				vk::AccessFlagBits::eColorAttachmentWrite,
				vk::ImageAspectFlagBits::eColor
				);
		}
		else if (state == LayoutState::DepthRead) {
			image.transitionImageLayout(
				cmd,
				vk::ImageLayout::eDepthStencilAttachmentOptimal,
				vk::ImageLayout::eDepthStencilReadOnlyOptimal,
				vk::PipelineStageFlagBits::eEarlyFragmentTests,
				vk::PipelineStageFlagBits::eFragmentShader,
				vk::AccessFlagBits::eDepthStencilAttachmentWrite,
				vk::AccessFlagBits::eShaderRead,
				vk::ImageAspectFlagBits::eDepth);
		}
		else if (state == LayoutState::DepthWrite) {
			image.transitionImageLayout(
				cmd,
				vk::ImageLayout::eDepthStencilReadOnlyOptimal,
				vk::ImageLayout::eDepthStencilAttachmentOptimal,
				vk::PipelineStageFlagBits::eFragmentShader,
				vk::PipelineStageFlagBits::eEarlyFragmentTests,
				vk::AccessFlagBits::eShaderRead,
				vk::AccessFlagBits::eDepthStencilAttachmentWrite,
				vk::ImageAspectFlagBits::eDepth);
		}
		image.layoutState = state;
	}

}

void Renderer::checkQueue()
{
	for (auto it = Queue::loadModel.begin(); it != Queue::loadModel.end();) {
		VulkanContext::get().device.waitIdle();
		Queue::loadModelFutures.push_back(std::async(std::launch::async, [](const std::string& folderPath, const std::string& modelName, bool show = true) {
				Model model;
				model.loadModel(folderPath, modelName, show);
				return std::any(std::move(model));
			}, std::get<0>(*it), std::get<1>(*it), true));
		it = Queue::loadModel.erase(it);
	}

	for (auto it = Queue::loadModelFutures.begin(); it != Queue::loadModelFutures.end();) {
		if (it->wait_for(std::chrono::seconds(0)) != std::future_status::timeout) {
			Model::models.push_back(std::any_cast<Model>(it->get()));
			GUI::modelList.push_back(Model::models.back().name);
			GUI::model_scale.push_back({ 1.f, 1.f, 1.f });
			GUI::model_pos.push_back({ 0.f, 0.f, 0.f });
			GUI::model_rot.push_back({ 0.f, 0.f, 0.f });
			it = Queue::loadModelFutures.erase(it);
		}
		else {
			it++;
		}
	}

	for (auto it = Queue::unloadModel.begin(); it != Queue::unloadModel.end();) {
		VulkanContext::get().device.waitIdle();
		Model::models[*it].destroy();
		Model::models.erase(Model::models.begin() + *it);
		GUI::modelList.erase(GUI::modelList.begin() + *it);
		GUI::model_scale.erase(GUI::model_scale.begin() + *it);
		GUI::model_pos.erase(GUI::model_pos.begin() + *it);
		GUI::model_rot.erase(GUI::model_rot.begin() + *it);
		GUI::modelItemSelected = -1;
		it = Queue::unloadModel.erase(it);
	}
#ifdef USE_SCRIPTS
	for (auto it = Queue::addScript.begin(); it != Queue::addScript.end();) {
		if (Model::models[std::get<0>(*it)].script)
			delete Model::models[std::get<0>(*it)].script;
		Model::models[std::get<0>(*it)].script = new Script(std::get<1>(*it).c_str());
		it = Queue::addScript.erase(it);
	}
	for (auto it = Queue::removeScript.begin(); it != Queue::removeScript.end();) {
		if (Model::models[*it].script) {
			delete Model::models[*it].script;
			Model::models[*it].script = nullptr;
		}
		it = Queue::removeScript.erase(it);
	}

	for (auto it = Queue::compileScript.begin(); it != Queue::compileScript.end();) {
		std::string name;
		if (Model::models[*it].script) {
			name = Model::models[*it].script->name;
			delete Model::models[*it].script;
			Model::models[*it].script = new Script(name.c_str());
		}
		it = Queue::compileScript.erase(it);
	}

#endif
}

void Renderer::update(float delta)
{
	FIRE_EVENT(Event::OnUpdate);

	// check for commands in queue
	checkQueue();

#ifdef USE_SCRIPTS
	// universal scripts
	for (auto& s : ctx.scripts)
		s->update(delta);
#endif

	// update camera matrices
	ctx.camera_main.update();

	// MODELS
	if (GUI::modelItemSelected > -1) {
		Model::models[GUI::modelItemSelected].scale = vec3(GUI::model_scale[GUI::modelItemSelected].data());
		Model::models[GUI::modelItemSelected].pos = vec3(GUI::model_pos[GUI::modelItemSelected].data());
		Model::models[GUI::modelItemSelected].rot = vec3(GUI::model_rot[GUI::modelItemSelected].data());
	}
	for (auto &model : Model::models)
		model.update(ctx.camera_main, delta);

	// GUI
	ctx.gui.update();

	// LIGHTS
	ctx.lightUniforms.update(ctx.camera_main);

	// SSAO
	ctx.ssao.update(ctx.camera_main);

	// SSR
	ctx.ssr.update(ctx.camera_main);

	// TAA
	ctx.taa.update(ctx.camera_main);

	// MOTION BLUR
	ctx.motionBlur.update(ctx.camera_main);

	// SHADOWS
	ctx.shadows.update(ctx.camera_main);

#ifdef RENDER_SKYBOX
	// SKYBOXES
	if (GUI::shadow_cast)
		ctx.skyBoxDay.update(ctx.camera_main);
	else
		ctx.skyBoxNight.update(ctx.camera_main);
#endif
}

void Renderer::recordComputeCmds(const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ)
{
	//auto beginInfo = vk::CommandBufferBeginInfo()
	//	.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
	//	.setPInheritanceInfo(nullptr);
	//
	//auto& cmd = ctx.vulkan.computeCmdBuffer;
	//cmd.begin(beginInfo);
	//
	//ctx.metrics[13].start(cmd);
	//cmd.bindPipeline(vk::PipelineBindPoint::eCompute, ctx.compute.pipeline.pipeline);
	//cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, ctx.compute.pipeline.compinfo.layout, 0, ctx.compute.DSCompute, nullptr);
	//cmd.dispatch(sizeX, sizeY, sizeZ);
	//ctx.metrics[13].end(&GUI::metrics[13]);
	//
	//cmd.end();
}

void Renderer::recordDeferredCmds(const uint32_t& imageIndex)
{
	// wait for vertex and index data to be ready on gui buffers
	ctx.vulkan.waitFences(ctx.gui.fenceUpload);

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	const auto& cmd = ctx.vulkan.dynamicCmdBuffer;
	cmd.begin(beginInfo);
	ctx.metrics[0].start(cmd);
	//cmd.setViewport(0, ctx.camera_main.renderArea.viewport);
	//cmd.setScissor(0, ctx.camera_main.renderArea.scissor);

	// SKYBOX
	SkyBox& skybox = GUI::shadow_cast ? ctx.skyBoxDay : ctx.skyBoxNight;
#ifdef RENDER_SKYBOX
	ctx.metrics[1].start(cmd);
	skybox.draw(imageIndex);
	ctx.metrics[1].end(&GUI::metrics[1]);
#endif

	// MODELS
	ctx.metrics[2].start(cmd);
	ctx.deferred.batchStart(cmd, imageIndex, ctx.renderTargets["viewport"].extent);
	for (uint32_t m = 0; m < Model::models.size(); m++)
		Model::models[m].draw();
	ctx.deferred.batchEnd();
	ctx.metrics[2].end(&GUI::metrics[2]);
	
	changeLayout(cmd, ctx.renderTargets["albedo"], LayoutState::ColorRead);
	changeLayout(cmd, ctx.renderTargets["depth"], LayoutState::ColorRead);
	changeLayout(cmd, ctx.renderTargets["normal"], LayoutState::ColorRead);
	changeLayout(cmd, ctx.renderTargets["srm"], LayoutState::ColorRead);
	changeLayout(cmd, ctx.renderTargets["emissive"], LayoutState::ColorRead);
	changeLayout(cmd, ctx.renderTargets["ssr"], LayoutState::ColorRead);
	changeLayout(cmd, ctx.renderTargets["ssaoBlur"], LayoutState::ColorRead);
	changeLayout(cmd, ctx.renderTargets["velocity"], LayoutState::ColorRead);
	changeLayout(cmd, ctx.renderTargets["taa"], LayoutState::ColorRead);
	for (auto& image : ctx.shadows.textures)
		changeLayout(cmd, image, LayoutState::DepthRead);
	
	// SCREEN SPACE AMBIENT OCCLUSION
	if (GUI::show_ssao) {
		ctx.metrics[3].start(cmd);
		changeLayout(cmd, ctx.renderTargets["ssaoBlur"], LayoutState::ColorWrite);
		const auto changeLayoutFunc = std::bind(&Renderer::changeLayout, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
		ctx.ssao.draw(cmd, imageIndex, changeLayoutFunc, ctx.renderTargets["ssao"]);
		changeLayout(cmd, ctx.renderTargets["ssaoBlur"], LayoutState::ColorRead);
		ctx.metrics[3].end(&GUI::metrics[3]);
	}
	
	// SCREEN SPACE REFLECTIONS
	if (GUI::show_ssr) {
		ctx.metrics[4].start(cmd);
		changeLayout(cmd, ctx.renderTargets["ssr"], LayoutState::ColorWrite);
		ctx.ssr.draw(cmd, imageIndex, ctx.renderTargets["ssr"].extent);
		changeLayout(cmd, ctx.renderTargets["ssr"], LayoutState::ColorRead);
		ctx.metrics[4].end(&GUI::metrics[4]);
	}
	
	// COMPOSITION
	ctx.metrics[5].start(cmd);
	ctx.deferred.draw(cmd, imageIndex, ctx.shadows, skybox, ctx.camera_main.invViewProjection, ctx.renderTargets["viewport"].extent);
	ctx.metrics[5].end(&GUI::metrics[5]);
	
	if (GUI::use_AntiAliasing) {
		// TAA
		if (GUI::use_TAA) {
			ctx.metrics[6].start(cmd);
			ctx.taa.copyFrameImage(cmd, ctx.renderTargets["viewport"]);
			const auto changeLayoutFunc = std::bind(&Renderer::changeLayout, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
			ctx.taa.draw(cmd, imageIndex, changeLayoutFunc, ctx.renderTargets);
			ctx.metrics[6].end(&GUI::metrics[6]);
		}
		// FXAA
		else if (GUI::use_FXAA) {
			ctx.metrics[6].start(cmd);
			ctx.fxaa.copyFrameImage(cmd, ctx.renderTargets["viewport"]);
			ctx.fxaa.draw(cmd, imageIndex, ctx.renderTargets["viewport"].extent);
			ctx.metrics[6].end(&GUI::metrics[6]);
		}
	}

	// BLOOM
	if (GUI::show_Bloom) {
		ctx.metrics[7].start(cmd);
		ctx.bloom.copyFrameImage(cmd, ctx.renderTargets["viewport"]);
		const auto changeLayoutFunc = std::bind(&Renderer::changeLayout, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
		ctx.bloom.draw(cmd, imageIndex, static_cast<uint32_t>(ctx.vulkan.swapchain->images.size()), changeLayoutFunc, ctx.renderTargets);
		ctx.metrics[7].end(&GUI::metrics[7]);
	}
	
	// MOTION BLUR
	if (GUI::show_motionBlur) {
		ctx.metrics[8].start(cmd);
		ctx.motionBlur.copyFrameImage(cmd, ctx.renderTargets["viewport"]);
		ctx.motionBlur.draw(cmd, imageIndex, ctx.renderTargets["viewport"].extent);
		ctx.metrics[8].end(&GUI::metrics[8]);
	}
	
	changeLayout(cmd, ctx.renderTargets["albedo"], LayoutState::ColorWrite);
	changeLayout(cmd, ctx.renderTargets["depth"], LayoutState::ColorWrite);
	changeLayout(cmd, ctx.renderTargets["normal"], LayoutState::ColorWrite);
	changeLayout(cmd, ctx.renderTargets["srm"], LayoutState::ColorWrite);
	changeLayout(cmd, ctx.renderTargets["emissive"], LayoutState::ColorWrite);
	changeLayout(cmd, ctx.renderTargets["ssr"], LayoutState::ColorWrite);
	changeLayout(cmd, ctx.renderTargets["ssaoBlur"], LayoutState::ColorWrite);
	changeLayout(cmd, ctx.renderTargets["velocity"], LayoutState::ColorWrite);
	changeLayout(cmd, ctx.renderTargets["taa"], LayoutState::ColorWrite);
	for (auto& image : ctx.shadows.textures)
		changeLayout(cmd, image, LayoutState::DepthWrite);

	// GUI
	ctx.metrics[9].start(cmd);
	ctx.gui.scaleToRenderArea(cmd, ctx.renderTargets["viewport"], imageIndex);
	ctx.gui.draw(cmd, imageIndex);
	ctx.metrics[9].end(&GUI::metrics[9]);

	ctx.metrics[0].end(&GUI::metrics[0]);

	cmd.end();
}

void Renderer::recordShadowsCmds(const uint32_t& imageIndex)
{
	// Render Pass (shadows mapping) (outputs the depth image with the light POV)

	const vk::DeviceSize offset = vk::DeviceSize();
	std::array<vk::ClearValue, 1> clearValuesShadows{};
	clearValuesShadows[0].depthStencil = vk::ClearDepthStencilValue{ 0.0f, 0 };

	vk::CommandBufferBeginInfo beginInfoShadows;
	beginInfoShadows.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	vk::RenderPassBeginInfo renderPassInfoShadows;
	renderPassInfoShadows.renderPass = ctx.shadows.renderPass;
	renderPassInfoShadows.renderArea = vk::Rect2D{ { 0, 0 },{ Shadows::imageSize, Shadows::imageSize } };
	renderPassInfoShadows.clearValueCount = static_cast<uint32_t>(clearValuesShadows.size());
	renderPassInfoShadows.pClearValues = clearValuesShadows.data();

	for (uint32_t i = 0; i < ctx.shadows.textures.size(); i++) {
		auto& cmd = ctx.vulkan.shadowCmdBuffers[i];
		cmd.begin(beginInfoShadows);
		ctx.metrics[10 + static_cast<size_t>(i)].start(cmd);
		cmd.setDepthBias(GUI::depthBias[0], GUI::depthBias[1], GUI::depthBias[2]);

		// depth[i] image ===========================================================
		renderPassInfoShadows.framebuffer = ctx.shadows.frameBuffers[i * ctx.vulkan.swapchain->images.size() + imageIndex]; // e.g. for 2 swapchain images - 1st(0,2,4) and 2nd(1,3,5)
		cmd.beginRenderPass(renderPassInfoShadows, vk::SubpassContents::eInline);
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, ctx.shadows.pipeline.pipeline);
		for (uint32_t m = 0; m < Model::models.size(); m++) {
			if (Model::models[m].render) {
				cmd.bindVertexBuffers(0, Model::models[m].vertexBuffer.buffer, offset);
				cmd.bindIndexBuffer(Model::models[m].indexBuffer.buffer, 0, vk::IndexType::eUint32);

				for (auto& node : Model::models[m].linearNodes) {
					if (node->mesh) {
						cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.shadows.pipeline.pipeinfo.layout, 0, { ctx.shadows.descriptorSets[i], node->mesh->descriptorSet, Model::models[m].descriptorSet }, nullptr);
						for (auto& primitive : node->mesh->primitives) {
							if (primitive.render)
								cmd.drawIndexed(primitive.indicesSize, 1, node->mesh->indexOffset + primitive.indexOffset, node->mesh->vertexOffset + primitive.vertexOffset, 0);
						}
					}
				}
			}
		}
		cmd.endRenderPass();
		ctx.metrics[10 + static_cast<size_t>(i)].end(&GUI::metrics[10 + static_cast<size_t>(i)]);
		// ==========================================================================
		cmd.end();
	}
}

void Renderer::present()
{
	FIRE_EVENT(Event::OnRender);

	if (GUI::use_compute) {
		//recordComputeCmds(2, 2, 1);
		//vk::SubmitInfo siCompute;
		//siCompute.commandBufferCount = 1;
		//siCompute.setPCommandBuffers = &ctx.vulkan.computeCmdBuffer;
		//ctx.vulkan.computeQueue.submit(siCompute, ctx.vulkan.fences[1]);
		//ctx.vulkan.device.waitForFences(ctx.vulkan.fences[1], VK_TRUE, UINT64_MAX);
		//ctx.vulkan.device.resetFences(ctx.vulkan.fences[1]);
	}

	// waitStage is a pipeline stage at which a semaphore wait will occur.
	const vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader };

	const uint32_t imageIndex = ctx.vulkan.swapchain->aquire(ctx.vulkan.semaphores[0], nullptr);

	ctx.vulkan.waitAndLockSubmits();

	if (GUI::shadow_cast) {
		// submit the shadow command buffers
		recordShadowsCmds(imageIndex);
		ctx.vulkan.submit(ctx.vulkan.shadowCmdBuffers, waitStages[0], ctx.vulkan.semaphores[0], ctx.vulkan.semaphores[1]);
	}

	// submit the main command buffer
	recordDeferredCmds(imageIndex);
	const vk::PipelineStageFlags waitStage = GUI::shadow_cast ? waitStages[1] : waitStages[0];
	const vk::Semaphore& waiSemaphore = GUI::shadow_cast ? ctx.vulkan.semaphores[1] : ctx.vulkan.semaphores[0];
	ctx.vulkan.submit(ctx.vulkan.dynamicCmdBuffer, waitStage, waiSemaphore, ctx.vulkan.semaphores[2], ctx.vulkan.fences[0]);

	// Presentation
	ctx.vulkan.swapchain->present(imageIndex, ctx.vulkan.semaphores[2]);

	std::chrono::high_resolution_clock::time_point startWait = std::chrono::high_resolution_clock::now();
	ctx.vulkan.waitFences(ctx.vulkan.fences[0]);
	std::chrono::duration<float> waitTime = std::chrono::high_resolution_clock::now() - startWait;
	Timer::waitingTime = waitTime.count();

	ctx.vulkan.unlockSubmits();
}