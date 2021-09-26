#include "RendererSystem.h"
#include "ECS/Context.h"
#include "Systems/EventSystem.h"
#include "Renderer/Vulkan/Vulkan.h"
#include "Model/Mesh.h"

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

		taa.Init();
		bloom.Init();
		fxaa.Init();
		motionBlur.Init();
		dof.Init();

		// render passes
		shadows.createRenderPass();
		ssao.createRenderPasses(renderTargets);
		ssr.createRenderPass(renderTargets);
		deferred.createRenderPasses(renderTargets);
		fxaa.createRenderPass(renderTargets);
		taa.createRenderPasses(renderTargets);
		bloom.createRenderPasses(renderTargets);
		dof.createRenderPass(renderTargets);
		motionBlur.createRenderPass(renderTargets);
		gui.createRenderPass();

		// frame buffers
		shadows.createFrameBuffers();
		ssao.createFrameBuffers(renderTargets);
		ssr.createFrameBuffers(renderTargets);
		deferred.createFrameBuffers(renderTargets);
		fxaa.createFrameBuffers(renderTargets);
		taa.createFrameBuffers(renderTargets);
		bloom.createFrameBuffers(renderTargets);
		dof.createFrameBuffers(renderTargets);
		motionBlur.createFrameBuffers(renderTargets);
		gui.createFrameBuffers();

		// pipelines
		shadows.createPipeline();
		ssao.createPipelines(renderTargets);
		ssr.createPipeline(renderTargets);
		deferred.createPipelines(renderTargets);
		fxaa.createPipeline(renderTargets);
		taa.createPipelines(renderTargets);
		bloom.createPipelines(renderTargets);
		dof.createPipeline(renderTargets);
		motionBlur.createPipeline(renderTargets);
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

		// SSAO
		auto updateSSAO = [this, camera_main]() { ssao.update(*camera_main); };
		Queue<0>::Request(Launch::Async, updateSSAO);

		// SSR
		auto updateSSR = [this, camera_main]() { ssr.update(*camera_main); };
		Queue<0>::Request(Launch::Async, updateSSR);

		// TAA
		auto updateTAA = [this, camera_main]() { taa.update(*camera_main); };
		Queue<0>::Request(Launch::Async, updateTAA);

		// MOTION BLUR
		auto updateMotionBlur = [this, camera_main]() { motionBlur.update(*camera_main); };
		Queue<0>::Request(Launch::Async, updateMotionBlur);

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
		Queue<0>::ExecuteRequests();
		Queue<1>::ExecuteRequests();

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
		ssao.destroy();
		ssr.destroy();
		fxaa.destroy();
		taa.destroy();
		bloom.destroy();
		dof.destroy();
		motionBlur.destroy();
		skyBoxDay.destroy();
		skyBoxNight.destroy();
		gui.destroy();
		for (auto& metric : metrics)
			metric.destroy();
	}
}
