#pragma once

#include "Renderer/Renderer.h"

namespace pe
{
    class CommandBuffer;

    class RendererSystem : public Renderer, public IDrawSystem
    {
    public:
        void Init(CommandBuffer *cmd) override;

        void Update(double delta) override;

        void Destroy() override;

        void Draw() override;
    };
}
