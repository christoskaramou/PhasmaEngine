#pragma once

#include "Renderer/Renderer.h"

namespace pe
{
    class CommandBuffer;

    class TestRenderer : public Renderer
    {
    public:
        TestRenderer();

        virtual ~TestRenderer();

        void Init(CommandBuffer *cmd) override;

        void Update(double delta) override;

        void Destroy() override;

        void Draw() override;
    };
}