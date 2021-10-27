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
#include "Renderer/RHI.h"
#include "Model/Mesh.h"
#include "Systems/PostProcessSystem.h"

namespace pe
{
	std::string PresentModeToString(PresentMode presentModeKHR)
	{
		switch (presentModeKHR)
		{
		case VK_PRESENT_MODE_IMMEDIATE_KHR: return "Immediate";
		case VK_PRESENT_MODE_MAILBOX_KHR: return "Mailbox";
		case VK_PRESENT_MODE_FIFO_KHR: return "Fifo";
		default: return "";
		}
	}

	void RendererSystem::Init()
	{
		// SET WINDOW TITLE
		std::string title = "PhasmaEngine";
		title += " - Device: " + RHII.gpuName;
		title += " - API: Vulkan";
		title += " - Present Mode: " + PresentModeToString(RHII.surface.presentMode);
#ifdef _DEBUG
		title += " - Configuration: Debug";
#else
		title += " - Configuration: Release";
#endif // _DEBUG
		CONTEXT->GetSystem<EventSystem>()->DispatchEvent(EventType::SetWindowTitle, title);

		// INIT RENDERING
		AddRenderTarget("viewport", RHII.surface.format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
		AddRenderTarget("normal", VK_FORMAT_R32G32B32A32_SFLOAT);
		AddRenderTarget("albedo", RHII.surface.format);
		AddRenderTarget("srm", RHII.surface.format); // Specular Roughness Metallic
		AddRenderTarget("ssao", VK_FORMAT_R16_UNORM);
		AddRenderTarget("ssaoBlur", VK_FORMAT_R8_UNORM);
		AddRenderTarget("ssr", RHII.surface.format);
		AddRenderTarget("velocity", VK_FORMAT_R16G16_SFLOAT);
		AddRenderTarget("brightFilter", RHII.surface.format);
		AddRenderTarget("gaussianBlurHorizontal", RHII.surface.format);
		AddRenderTarget("gaussianBlurVertical", RHII.surface.format);
		AddRenderTarget("emissive", RHII.surface.format);
		AddRenderTarget("taa", RHII.surface.format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

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

		static VkPipelineStageFlags waitStages[] =
		{
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
		};

		// acquire the image
		auto& aquireSignalSemaphore = RHII.semaphores[frameIndex];
		uint32_t imageIndex = RHII.swapchain.Aquire(aquireSignalSemaphore.handle, {});

		static Timer timer;
		timer.Start();
		RHII.WaitFence(&RHII.fences[imageIndex]);
		FrameTimer::Instance().timestamps[0] = timer.Count();

		auto& cmd = RHII.dynamicCmdBuffers[imageIndex];

		RHII.WaitAndLockSubmits();

		if (GUI::shadow_cast)
		{

			// record the shadow command buffers
			RecordShadowsCmds(imageIndex);

			// submit the shadow command buffers
			auto& shadowWaitSemaphore = aquireSignalSemaphore;
			auto& shadowSignalSemaphore = RHII.semaphores[imageIndex * 3 + 1];
			auto& scb = RHII.shadowCmdBuffers;
			auto size = shadows.textures.size();
			auto i = size * imageIndex;
			std::vector<CommandBuffer> activeShadowCmdBuffers(scb.begin() + i, scb.begin() + i + size);
			RHII.Submit(
				static_cast<uint32_t>(activeShadowCmdBuffers.size()), activeShadowCmdBuffers.data(),
				&waitStages[0],
				1, &shadowWaitSemaphore,
				1, &shadowSignalSemaphore,
				nullptr);

			aquireSignalSemaphore = shadowSignalSemaphore;
		}

		// record the command buffers
		RecordDeferredCmds(imageIndex);

		// submit the command buffers
		auto& deferredWaitStage = GUI::shadow_cast ? waitStages[1] : waitStages[0];
		auto& deferredWaitSemaphore = aquireSignalSemaphore;
		auto& deferredSignalSemaphore = RHII.semaphores[imageIndex * 3 + 2];
		auto& deferredSignalFence = RHII.fences[imageIndex];
		RHII.Submit(
			1, &cmd,
			&deferredWaitStage,
			1, &deferredWaitSemaphore,
			1, &deferredSignalSemaphore,
			&deferredSignalFence);

		// Presentation
		auto& presentWaitSemaphore = deferredSignalSemaphore;
		RHII.Present(1, &RHII.swapchain, &imageIndex, 1, &presentWaitSemaphore);

		RHII.UnlockSubmits();

		gui.RenderViewPorts();

		frameIndex = (frameIndex + 1) % SWAPCHAIN_IMAGES;
	}

	void RendererSystem::Destroy()
	{
		RHII.WaitDeviceIdle();

		for (auto& rt : renderTargets)
			rt.second.Destroy();

		if (Model::models.empty())
		{
			if (Pipeline::getDescriptorSetLayoutModel())
			{
				vkDestroyDescriptorSetLayout(RHII.device, Pipeline::getDescriptorSetLayoutModel(), nullptr);
				Pipeline::getDescriptorSetLayoutModel() = {};
			}
			if (Pipeline::getDescriptorSetLayoutMesh())
			{
				vkDestroyDescriptorSetLayout(RHII.device, Pipeline::getDescriptorSetLayoutMesh(), nullptr);
				Pipeline::getDescriptorSetLayoutMesh() = {};
			}
			if (Pipeline::getDescriptorSetLayoutPrimitive())
			{
				vkDestroyDescriptorSetLayout(RHII.device, Pipeline::getDescriptorSetLayoutPrimitive(), nullptr);
				Pipeline::getDescriptorSetLayoutPrimitive() = {};
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
