#pragma once
#include "Context.h"

class Renderer
{
    public:
        Renderer(SDL_Window* window);
        ~Renderer();
        void update(float delta);
		void present();
		static float rand(float x1, float x2);

		Context ctx;
		bool prepared = false;
		bool overloadedGPU = false;
		bool deferredRender = true;
    private:
		void recordForwardCmds(const uint32_t& imageIndex);
		void recordDeferredCmds(const uint32_t& imageIndex);
		void recordShadowsCmds(const uint32_t& imageIndex);

		float frustum[6][4];
		void ExtractFrustum(vm::mat4& projection_view_model);
		bool SphereInFrustum(vm::vec4& boundingSphere);
};