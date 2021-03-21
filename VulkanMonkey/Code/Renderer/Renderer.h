#pragma once
#include "../ECS/System.h"
#include "../Core/Image.h"
#include "../Core/Surface.h"
#include "../Swapchain/Swapchain.h"
#include "../GUI/GUI.h"
#include "../Skybox/Skybox.h"
#include "../Shadows/Shadows.h"
#include "../Core/Light.h"
#include "../Model/Model.h"
#include "../Camera/Camera.h"
#include "../Deferred/Deferred.h"
#include "../Compute/Compute.h"
#include "../Core/Timer.h"
#include "../Script/Script.h"
#include "../PostProcess/Bloom.h"
#include "../PostProcess/DOF.h"
#include "../PostProcess/FXAA.h"
#include "../PostProcess/MotionBlur.h"
#include "../PostProcess/SSAO.h"
#include "../PostProcess/SSR.h"
#include "../PostProcess/TAA.h"

namespace vk
{
	class CommandBuffer;
}

namespace vm
{
	enum class RenderQueue
	{
		Opaque = 1,
		AlphaCut = 2,
		AlphaBlend = 3
	};

	class Renderer final : public ISystem
	{
		Shadows shadows;
		Deferred deferred;
		SSAO ssao;
		SSR ssr;
		FXAA fxaa;
		TAA taa;
		Bloom bloom;
		DOF dof;
		MotionBlur motionBlur;
		SkyBox skyBoxDay;
		SkyBox skyBoxNight;
		GUI gui;
		LightUniforms lightUniforms;

		std::vector<GPUTimer> metrics{};

#ifdef USE_SCRIPTS
		std::vector<Script*> scripts{};
#endif
	public:
		Renderer(Context* ctx, SDL_Window* window);
		~Renderer();

		void Init() override;
		void Update(double delta) override;
		void Destroy() override;
		void Draw();
		void AddRenderTarget(const std::string& name, vk::Format format, const vk::ImageUsageFlags& additionalFlags);
		void LoadResources();
		void CreateUniforms();
		void ResizeViewport(uint32_t width, uint32_t height);
		void RecreatePipelines();
		inline SDL_Window* GetWindow() { return window; }
		inline Context* GetContext() { return ctx; }

		uint32_t previousImageIndex = 0;
		std::map<std::string, Image> renderTargets{};

	private:
		Context* ctx;
		SDL_Window* window;
		void CheckQueue() const;
		static void RecordComputeCmds(uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ);
		void RecordDeferredCmds(const uint32_t& imageIndex);
		void RecordShadowsCmds(const uint32_t& imageIndex);
	};
}