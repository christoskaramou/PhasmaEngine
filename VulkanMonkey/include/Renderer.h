#pragma once
#include "Context.h"

class Renderer
{
    public:
        Renderer(SDL_Window* window);
        ~Renderer();
        void update(float delta);
		void present();

		Context ctx;
		bool prepared = false;
		bool overloadedGPU = false;
		bool useCompute = false;
		bool useDeferredRender = true;
		bool useSSAO = true;
		bool useSSR = true;
    private:
		void recordComputeCmds(const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ);
		void recordForwardCmds(const uint32_t& imageIndex);
		void recordDeferredCmds(const uint32_t& imageIndex);
		void recordShadowsCmds(const uint32_t& imageIndex);
};