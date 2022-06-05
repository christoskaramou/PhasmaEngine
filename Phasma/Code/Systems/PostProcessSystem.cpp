/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "Systems/PostProcessSystem.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"
#include "PostProcess/Bloom.h"
#include "PostProcess/DOF.h"
#include "PostProcess/FXAA.h"
#include "PostProcess/MotionBlur.h"
#include "PostProcess/SSAO.h"
#include "PostProcess/SSR.h"
#include "PostProcess/TAA.h"
#include "PostProcess/SSGI.h"

namespace pe
{
    PostProcessSystem::PostProcessSystem()
    {
    }

    PostProcessSystem::~PostProcessSystem()
    {
    }

    void PostProcessSystem::Init()
    {
        m_effects[GetTypeID<Bloom>()] = WORLD_ENTITY->CreateComponent<Bloom>();
        m_effects[GetTypeID<DOF>()] = WORLD_ENTITY->CreateComponent<DOF>();
        m_effects[GetTypeID<FXAA>()] = WORLD_ENTITY->CreateComponent<FXAA>();
        m_effects[GetTypeID<MotionBlur>()] = WORLD_ENTITY->CreateComponent<MotionBlur>();
        m_effects[GetTypeID<SSAO>()] = WORLD_ENTITY->CreateComponent<SSAO>();
        m_effects[GetTypeID<SSR>()] = WORLD_ENTITY->CreateComponent<SSR>();
        m_effects[GetTypeID<TAA>()] = WORLD_ENTITY->CreateComponent<TAA>();
        m_effects[GetTypeID<SSGI>()] = WORLD_ENTITY->CreateComponent<SSGI>();

        for (auto &effect : m_effects)
        {
            effect.second->Init();
            effect.second->CreateRenderPass();
            effect.second->CreateFrameBuffers();
            effect.second->CreatePipeline();
            effect.second->CreateUniforms();
        }
    }

    void PostProcessSystem::Update(double delta)
    {
        Camera *camera_main = CONTEXT->GetSystem<CameraSystem>()->GetCamera(0);

        for (auto &effect : m_effects)
            effect.second->Update(camera_main);
        // SyncQueue<Launch::Async>::Request([camera_main, effect]() { effect.second->Update(camera_main); });
    }

    void PostProcessSystem::Destroy()
    {
        for (auto &effect : m_effects)
            effect.second->Destroy();
    }

    void PostProcessSystem::Resize(uint32_t width, uint32_t height)
    {
        for (auto &effect : m_effects)
            effect.second->Resize(width, height);
    }
}
