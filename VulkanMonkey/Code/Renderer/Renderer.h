#pragma once
#include "../Context/Context.h"
#include "../Node/Node.h"
#include "../Queue/Queue.h"
#include "../Math/Math.h"
#include "../Timer/Timer.h"
#include <iostream>

namespace vm {
	class Renderer
	{
	public:
		Renderer(SDL_Window* window);
		~Renderer();
		void update(float delta);
		void present();

		Context ctx;
		float waitingTime = 0.f; // ms
		bool prepared = false;

	private:
		void changeLayout(vk::CommandBuffer cmd, Image& image, LayoutState state);
		void checkQueue();
		void recordComputeCmds(const uint32_t sizeX, const uint32_t sizeY, const uint32_t sizeZ);
		void recordDeferredCmds(const uint32_t& imageIndex);
		void recordShadowsCmds(const uint32_t& imageIndex);
	};
}