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

#include "RendererSystem.h"
#include "ECS/Context.h"
#include "Systems/EventSystem.h"
#include "Renderer/Vulkan/Vulkan.h"
#include "Model/Mesh.h"
#include "Systems/PostProcessSystem.h"

namespace pe
{
	void RendererSystem::Init()
	{
		auto vulkan = VulkanContext::Get();

		// SET WINDOW TITLE
		std::string title = "PhasmaEngine";
		title += " - Device: " + std::string(vulkan->gpuProperties->deviceName.data());
		title += " - API: Vulkan";
		title += " - Present Mode: " + vk::to_string(*vulkan->surface.presentModeKHR);
#ifdef _DEBUG
		title += " - Configuration: Debug";
#else
		title += " - Configuration: Release";
#endif // _DEBUG
		CONTEXT->GetSystem<EventSystem>()->DispatchEvent(EventType::SetWindowTitle, title);

		// INIT RENDERING
		AddRenderTarget("viewport", vulkan->surface.formatKHR->format, vk::ImageUsageFlagBits::eTransferSrc);
		AddRenderTarget("normal", vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlags());
		AddRenderTarget("albedo", vulkan->surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("srm", vulkan->surface.formatKHR->format, vk::ImageUsageFlags()); // Specular Roughness Metallic
		AddRenderTarget("ssao", vk::Format::eR16Unorm, vk::ImageUsageFlags());
		AddRenderTarget("ssaoBlur", vk::Format::eR8Unorm, vk::ImageUsageFlags());
		AddRenderTarget("ssr", vulkan->surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("velocity", vk::Format::eR16G16Sfloat, vk::ImageUsageFlags());
		AddRenderTarget("brightFilter", vulkan->surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("gaussianBlurHorizontal", vulkan->surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("gaussianBlurVertical", vulkan->surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("emissive", vulkan->surface.formatKHR->format, vk::ImageUsageFlags());
		AddRenderTarget("taa", vulkan->surface.formatKHR->format, vk::ImageUsageFlagBits::eTransferSrc);

		// render passes
		shadows.createRenderPass();
		deferred.createRenderPasses(renderTargets);

		// frame buffers
		shadows.createFrameBuffers();
		deferred.createFrameBuffers(renderTargets);

		// pipelines
		shadows.createPipeline();
		deferred.createPipelines(renderTargets);

		//transformsCompute = Compute::Create("Shaders/Compute/shader.comp", 64, 64);

		//LOAD RESOURCES
		LoadResources();
		// CREATE UNIFORMS AND DESCRIPTOR SETS
		CreateUniforms();

		// GUI LOAD
		gui.CreateRenderPass();
		gui.CreateFrameBuffers();
		gui.InitGUI();

		renderArea.Update(0.0f, 0.0f, WIDTH_f, HEIGHT_f);
	}

	void RendererSystem::Update(double delta)
	{
#ifndef IGNORE_SCRIPTS
		// universal scripts
		for (auto& s : scripts)
			s->update(static_cast<float>(delta));
#endif

		CameraSystem* cameraSystem = CONTEXT->GetSystem<CameraSystem>();
		Camera* camera_main = cameraSystem->GetCamera(0);

		// Model updates + 8(the rest updates)

		// MODELS
		if (GUI::modelItemSelected > -1)
		{
			Model::models[GUI::modelItemSelected].scale = vec3(GUI::model_scale[GUI::modelItemSelected].data());
			Model::models[GUI::modelItemSelected].pos = vec3(GUI::model_pos[GUI::modelItemSelected].data());
			Model::models[GUI::modelItemSelected].rot = vec3(GUI::model_rot[GUI::modelItemSelected].data());
		}
		for (auto& model : Model::models)
		{
			auto updateModel = [&model, camera_main, delta]() { model.update(*camera_main, delta); };
			Queue<Launch::Async>::Request(updateModel);
		}

		// GUI
		auto updateGUI = [this]() { gui.Update(); };
		Queue<Launch::Async>::Request(updateGUI);

		// SHADOWS
		auto updateShadows = [this, camera_main]() { shadows.update(*camera_main); };
		Queue<Launch::Async>::Request(updateShadows);

		// COMPOSITION UNIFORMS
		auto updateDeferred = [this, camera_main]() { deferred.update(camera_main->invViewProjection); };
		Queue<Launch::Async>::Request(updateDeferred);
	}

	void RendererSystem::Draw()
	{
		static int frameIndex = 0;
		auto& vCtx = *VulkanContext::Get();

		static const vk::PipelineStageFlags waitStages[] =
		{
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::PipelineStageFlagBits::eFragmentShader
		};

		//FIRE_EVENT(Event::OnRender);

		// acquire the image
		auto& aquireSignalSemaphore = (*vCtx.semaphores)[frameIndex];
		const uint32_t imageIndex = vCtx.swapchain.Aquire(aquireSignalSemaphore, nullptr);

		static Timer timer;
		timer.Start();
		vCtx.waitFences((*vCtx.fences)[imageIndex]);
		FrameTimer::Instance().timestamps[0] = timer.Count();

		const auto& cmd = (*vCtx.dynamicCmdBuffers)[imageIndex];

		vCtx.waitAndLockSubmits();

		if (GUI::shadow_cast)
		{

			// record the shadow command buffers
			RecordShadowsCmds(imageIndex);

			// submit the shadow command buffers
			const auto& shadowWaitSemaphore = aquireSignalSemaphore;
			const auto& shadowSignalSemaphore = (*vCtx.semaphores)[imageIndex * 3 + 1];
			const auto& scb = vCtx.shadowCmdBuffers;
			const auto size = shadows.textures.size();
			const auto i = size * imageIndex;
			const std::vector<vk::CommandBuffer> activeShadowCmdBuffers(scb->begin() + i, scb->begin() + i + size);
			vCtx.submit(activeShadowCmdBuffers, waitStages[0], shadowWaitSemaphore, shadowSignalSemaphore, nullptr);

			aquireSignalSemaphore = shadowSignalSemaphore;
		}

		// record the command buffers
		RecordDeferredCmds(imageIndex);

		// submit the command buffers
		const auto& deferredWaitStage = GUI::shadow_cast ? waitStages[1] : waitStages[0];
		const auto& deferredWaitSemaphore = aquireSignalSemaphore;
		const auto& deferredSignalSemaphore = (*vCtx.semaphores)[imageIndex * 3 + 2];
		const auto& deferredSignalFence = (*vCtx.fences)[imageIndex];
		vCtx.submit(cmd, deferredWaitStage, deferredWaitSemaphore, deferredSignalSemaphore, deferredSignalFence);

		// Presentation
		const auto& presentWaitSemaphore = deferredSignalSemaphore;
		vCtx.Present(*vCtx.swapchain.handle, imageIndex, presentWaitSemaphore);

		vCtx.unlockSubmits();

		gui.RenderViewPorts();



		frameIndex = (frameIndex + 1) % SWAPCHAIN_IMAGES;
	}

	void RendererSystem::Destroy()
	{
		VULKAN.device->waitIdle();

		for (auto& rt : renderTargets)
			rt.second.Destroy();

		if (Model::models.empty())
		{
			if (Pipeline::getDescriptorSetLayoutModel())
			{
				VULKAN.device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutModel());
				Pipeline::getDescriptorSetLayoutModel() = nullptr;
			}
			if (Pipeline::getDescriptorSetLayoutMesh())
			{
				VULKAN.device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutMesh());
				Pipeline::getDescriptorSetLayoutMesh() = nullptr;
			}
			if (Pipeline::getDescriptorSetLayoutPrimitive())
			{
				VULKAN.device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutPrimitive());
				Pipeline::getDescriptorSetLayoutPrimitive() = nullptr;
			}
		}

#ifndef IGNORE_SCRIPTS
		for (auto& script : scripts)
			delete script;
#endif
		for (auto& model : Model::models)
			model.destroy();
		for (auto& texture : Mesh::uniqueTextures)
			texture.second.Destroy();
		Mesh::uniqueTextures.clear();

		Compute::DestroyResources();
		shadows.destroy();
		deferred.destroy();
		skyBoxDay.destroy();
		skyBoxNight.destroy();
		gui.Destroy();
	}
}
