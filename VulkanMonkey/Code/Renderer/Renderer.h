#pragma once
#include "../Context/Context.h"
#include "../Node/Node.h"
#include "../Queue/Queue.h"
#include "../Math/Math.h"
#include "../Timer/Timer.h"

namespace vm {
	class Renderer
	{
	public:
		Renderer(SDL_Window* window);
		~Renderer();
		void update();
		void present();

		Context ctx;
		float waitingTime = 0.f; // ms
		bool prepared = false;
		bool overloadedGPU = false;
		bool useCompute = false;

	private:
		void recordComputeCmds(const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ);
		void recordForwardCmds(const uint32_t& imageIndex);
		void recordDeferredCmds(const uint32_t& imageIndex);
		void recordShadowsCmds(const uint32_t& imageIndex);
	};
}