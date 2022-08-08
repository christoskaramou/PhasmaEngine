#include "Systems/PostProcessSystem.h"
#include "Systems/RendererSystem.h"
#include "Systems/CameraSystem.h"
#include "PostProcess/Bloom.h"
#include "PostProcess/DOF.h"
#include "PostProcess/FXAA.h"
#include "PostProcess/MotionBlur.h"
#include "PostProcess/SSAO.h"
#include "PostProcess/SSR.h"
#include "PostProcess/SuperResolution.h"

namespace pe
{
    PostProcessSystem::PostProcessSystem()
    {
    }

    PostProcessSystem::~PostProcessSystem()
    {
    }

    void PostProcessSystem::Init(CommandBuffer *cmd)
    {
        m_effects[GetTypeID<Bloom>()] = WORLD_ENTITY->CreateComponent<Bloom>();
        m_effects[GetTypeID<DOF>()] = WORLD_ENTITY->CreateComponent<DOF>();
        m_effects[GetTypeID<FXAA>()] = WORLD_ENTITY->CreateComponent<FXAA>();
        m_effects[GetTypeID<MotionBlur>()] = WORLD_ENTITY->CreateComponent<MotionBlur>();
        m_effects[GetTypeID<SSAO>()] = WORLD_ENTITY->CreateComponent<SSAO>();
        m_effects[GetTypeID<SSR>()] = WORLD_ENTITY->CreateComponent<SSR>();
        m_effects[GetTypeID<SuperResolution>()] = WORLD_ENTITY->CreateComponent<SuperResolution>();

        for (auto &effect : m_effects)
        {
            effect.second->Init();
            effect.second->CreateUniforms(cmd);
            effect.second->UpdatePipelineInfo();
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
