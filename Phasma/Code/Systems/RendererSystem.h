#pragma once

#define TEST_RENDERER 0

#include "Renderer/Renderer.h"
#include "Renderer/TestRenderer.h"

namespace pe
{
    class CommandBuffer;

    class RendererSystem :
#if TEST_RENDERER
        public TestRenderer
#else
        public Renderer
#endif
    {
    };
}
