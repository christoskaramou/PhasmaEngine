#pragma once
#include "../Context/ContextTemp.h"
#include "../ECS/System.h"

namespace vk
{
	class CommandBuffer;
}

namespace vm
{
	class Renderer final : public System
	{
	public:
		Renderer(Context* ctxx, SDL_Window* window);
		~Renderer();

		void Init() override;
		void Update(double delta) override;
		void Destroy() override;
		void Present();
		inline SDL_Window* GetWindow() { return window; }

		ContextTemp ctx;
		uint32_t previousImageIndex = 0;

	private:
		SDL_Window* window;
		void CheckQueue() const;
		static void RecordComputeCmds(uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ);
		void RecordDeferredCmds(const uint32_t& imageIndex);
		void RecordShadowsCmds(const uint32_t& imageIndex);
	};
}