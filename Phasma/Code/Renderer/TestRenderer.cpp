#include "Renderer/TestRenderer.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Image.h"
#include "Renderer/RenderPass.h"
#include "Renderer/FrameBuffer.h"
#include "Renderer/Queue.h"
#include "Renderer/Surface.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Shadows.h"
#include "Renderer/Deferred.h"
#include "Renderer/Compute.h"
#include "Systems/CameraSystem.h"
#include "Systems/PostProcessSystem.h"
#include "PostProcess/Bloom.h"
#include "PostProcess/DOF.h"
#include "PostProcess/FXAA.h"
#include "PostProcess/MotionBlur.h"
#include "PostProcess/SSAO.h"
#include "PostProcess/SSR.h"
#include "PostProcess/SuperResolution.h"
#include "Model/Model.h"
#include "Model/Mesh.h"
#include "Script/Script.h"

namespace pe
{
    TestRenderer::TestRenderer()
    {
    }

    TestRenderer::~TestRenderer()
    {
    }

    void TestRenderer::Init(CommandBuffer *cmd)
    {
        
    }

    void TestRenderer::Update(double delta)
    {
    }

    void TestRenderer::Draw()
    {
    }

    void TestRenderer::Destroy()
    {
    }
}
