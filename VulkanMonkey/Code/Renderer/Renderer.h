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

	private:
		static void changeLayout(vk::CommandBuffer cmd, Image& image, LayoutState state);
		void checkQueue() const;
		static void recordComputeCmds(uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ);
		void recordDeferredCmds(const uint32_t& imageIndex);
		void recordShadowsCmds(const uint32_t& imageIndex);
	};
}