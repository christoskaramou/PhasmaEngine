#pragma once
#include "../Context/Context.h"

namespace vk
{
	class CommandBuffer;
}

namespace vm
{
	class Renderer
	{
	public:
		Renderer(SDL_Window* window);
		~Renderer();
		void update(double delta);
		void present();

		Context ctx;
		uint32_t previousImageIndex = 0;

	private:
		void checkQueue() const;
		static void recordComputeCmds(uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ);
		void recordDeferredCmds(const uint32_t& imageIndex);
		void recordShadowsCmds(const uint32_t& imageIndex);
	};
}