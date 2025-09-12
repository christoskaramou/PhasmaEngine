#include "ECS/Context.h"
#include "API/RHI.h"
#include "API/Queue.h"
#include "API/Command.h"

namespace pe
{
    Context::Context()
    {
        m_worldEntity = CreateEntity();
    }

    void Context::InitSystems()
    {
        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();

        for (auto &system : m_systems)
        {
            if (system.second->IsEnabled())
                system.second->Init(cmd);
        }

        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();
    }

    void Context::DestroySystems()
    {
        RHII.WaitDeviceIdle();

        for (auto &system : m_systems)
            system.second->Destroy();

        m_systems.clear();
    }

    void Context::UpdateSystems()
    {
        for (auto &system : m_systems)
        {
            if (system.second->IsEnabled())
                system.second->Update();
        }
    }

    void Context::DrawSystems()
    {
        for (auto &system : m_drawSystems)
        {
            if (system.second->IsEnabled())
                system.second->Draw();
        }
    }

    Entity *Context::CreateEntity()
    {
        Entity *entity = new Entity();
        entity->SetContext(this);
        entity->SetEnabled(true);

        size_t key = entity->GetID();
        m_entities[key] = std::shared_ptr<Entity>(entity);

        return m_entities[key].get();
    }

    Entity *Context::GetEntity(size_t id)
    {
        auto iterator = m_entities.find(id);
        return iterator != m_entities.end() ? iterator->second.get() : nullptr;
    }

    void Context::RemoveEntity(size_t id)
    {
        auto it = m_entities.find(id);
        if (it != m_entities.end())
            m_entities.erase(it);
    }

    std::unordered_map<size_t, std::shared_ptr<ISystem>> Context::GetSystems()
    {
        return m_systems;
    }

    void UpdateGlobalSystems()
    {
        Context::Get()->UpdateSystems();
    }

    void DrawGlobalSystems()
    {
        Context::Get()->DrawSystems();
    }
}
