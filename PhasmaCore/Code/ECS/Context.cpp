#include "ECS/Context.h"
#include "API/RHI.h"
#include "API/Queue.h"
#include "API/Command.h"

namespace pe
{
    std::unique_ptr<Context> &Context::InstanceStorage()
    {
        static std::unique_ptr<Context> instance{};
        return instance;
    }

    Context *Context::Get()
    {
        auto &instance = InstanceStorage();
        if (!instance)
            instance.reset(new Context());
        return instance.get();
    }

    void Context::Remove()
    {
        auto &instance = InstanceStorage();
        if (!instance)
            return;

        instance.reset();
    }

    Context::Context()
    {
        m_worldEntity = CreateEntity();
    }

    void Context::InitSystems()
    {
        if (m_systems.empty())
            return;

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
        m_drawSystems.clear();
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
        auto entity = std::make_unique<Entity>();
        entity->SetContext(this);
        entity->SetEnabled(true);

        const size_t key = entity->GetID();
        auto [it, inserted] = m_entities.emplace(key, std::move(entity));
        if (!inserted)
        {
            PE_ERROR("Entity with duplicate id detected");
            return it->second.get();
        }

        return it->second.get();
    }

    Entity *Context::GetEntity(size_t id) const
    {
        auto iterator = m_entities.find(id);
        return iterator != m_entities.end() ? iterator->second.get() : nullptr;
    }

    void Context::RemoveEntity(size_t id)
    {
        auto it = m_entities.find(id);
        if (it == m_entities.end())
            return;

        if (m_worldEntity == it->second.get())
        {
            PE_ERROR("Cannot remove the world entity");
            return;
        }

        m_entities.erase(it);
    }

    const std::unordered_map<size_t, std::unique_ptr<ISystem>> &Context::GetSystems() const noexcept
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
