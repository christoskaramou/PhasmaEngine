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
		Context::Get()->GetSystem<EventSystem>()->DispatchEvent(EventType::SetWindowTitle, title);

		// INIT RENDERING
		AddRenderTarget("viewport", vulkan->surface.formatKHR->format, vk::ImageUsageFlagBits::eTransferSrc);
		AddRenderTarget("depth", vk::Format::eR32Sfloat, vk::ImageUsageFlags());
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
		gui.createRenderPass();

		// frame buffers
		shadows.createFrameBuffers();
		deferred.createFrameBuffers(renderTargets);
		gui.createFrameBuffers();

		// pipelines
		shadows.createPipeline();
		deferred.createPipelines(renderTargets);
		gui.createPipeline();

		//transformsCompute = Compute::Create("Shaders/Compute/shader.comp", 64, 64);

		metrics.resize(20);
		//LOAD RESOURCES
		LoadResources();
		// CREATE UNIFORMS AND DESCRIPTOR SETS
		CreateUniforms();
	}


	void RendererSystem::Update(double delta)
	{
		static Timer timer;
		timer.Start();

#ifndef IGNORE_SCRIPTS
		// universal scripts
		for (auto& s : scripts)
			s->update(static_cast<float>(delta));
#endif

		CameraSystem* cameraSystem = Context::Get()->GetSystem<CameraSystem>();
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
			Queue<0>::Request(Launch::Async, updateModel);
		}

		// GUI
		auto updateGUI = [this]() { gui.update(); };
		Queue<0>::Request(Launch::Async, updateGUI);

		// SHADOWS
		auto updateShadows = [this, camera_main]() { shadows.update(*camera_main); };
		Queue<0>::Request(Launch::Async, updateShadows);

		// COMPOSITION UNIFORMS
		auto updateDeferred = [this, camera_main]() { deferred.update(camera_main->invViewProjection); };
		Queue<0>::Request(Launch::Async, updateDeferred);

		static Timer timerFenceWait;
		timerFenceWait.Start();
		VulkanContext::Get()->waitFences((*VulkanContext::Get()->fences)[previousImageIndex]);
		FrameTimer::Instance().timestamps[0] = timerFenceWait.Count();

		GUI::updatesTimeCount = static_cast<float>(timer.Count());
	}

	void RendererSystem::Destroy()
	{
		VulkanContext::Get()->device->waitIdle();

		for (auto& rt : renderTargets)
			rt.second.destroy();

		if (Model::models.empty())
		{
			if (Pipeline::getDescriptorSetLayoutModel())
			{
				VulkanContext::Get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutModel());
				Pipeline::getDescriptorSetLayoutModel() = nullptr;
			}
			if (Pipeline::getDescriptorSetLayoutMesh())
			{
				VulkanContext::Get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutMesh());
				Pipeline::getDescriptorSetLayoutMesh() = nullptr;
			}
			if (Pipeline::getDescriptorSetLayoutPrimitive())
			{
				VulkanContext::Get()->device->destroyDescriptorSetLayout(Pipeline::getDescriptorSetLayoutPrimitive());
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
			texture.second.destroy();
		Mesh::uniqueTextures.clear();

		Compute::DestroyResources();
		shadows.destroy();
		deferred.destroy();
		skyBoxDay.destroy();
		skyBoxNight.destroy();
		gui.destroy();
		for (auto& metric : metrics)
			metric.destroy();
	}
}
