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

#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"

namespace pe
{
    Entity *Context::WorldEntity = CONTEXT->CreateEntity();

    void Context::InitSystems()
    {
        Queue *queue = Queue::GetNext(QueueType::GraphicsBit | QueueType::TransferBit, 1);
        CommandBuffer *cmd = CommandBuffer::GetNext(queue->GetFamilyId());
        cmd->Begin();

        for (auto &system : m_systems)
        {
            if (system.second->IsEnabled())
                system.second->Init(cmd);
        }

        cmd->End();
        cmd->Submit(queue, nullptr, 0, nullptr, 0, nullptr);
        cmd->Wait();
        
        CommandBuffer::Return(cmd);
        Queue::Return(queue);
    }

    void Context::DestroySystems()
    {
        RHII.WaitDeviceIdle();

        for (auto &system : m_systems)
            system.second->Destroy();
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
        if (m_entities.find(id) != m_entities.end())
            m_entities.erase(id);
    }

    std::unordered_map<size_t, std::shared_ptr<ISystem>> Context::GetSystems()
    {
        return m_systems;
    }
}
