#include "ECS/Context.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"

namespace pe
{
    Context::Context()
    {
        m_worldEntity = CreateEntity();
    }

    void Context::InitSystems()
    {
        Queue *queue = Queue::Get(QueueType::GraphicsBit | QueueType::TransferBit, 1);
        CommandBuffer *cmd = CommandBuffer::GetFree(queue);
        cmd->Begin();

        for (auto &system : m_systems)
        {
            if (system.second->IsEnabled())
                system.second->Init(cmd);
        }

        cmd->End();
        queue->Submit(1, &cmd);
        cmd->Wait();

        CommandBuffer::Return(cmd);
    }

    void Context::DestroySystems()
    {
        RHII.WaitDeviceIdle();

        for (auto &system : m_systems)
            system.second->Destroy();

        m_systems.clear();
    }

    void Context::UpdateSystems(double delta)
    {
        for (auto &system : m_systems)
        {
            if (system.second->IsEnabled())
                system.second->Update(delta);
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

    void UpdateGlobalSystems(double delta)
    {
        Context::Get()->UpdateSystems(delta);
    }

    void DrawGlobalSystems()
    {
        Context::Get()->DrawSystems();
    }
}
