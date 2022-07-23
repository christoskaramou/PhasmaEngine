#pragma once

namespace pe
{
    class Entity;

    class IComponent
    {
    public:
        IComponent() : m_entity(nullptr), m_enabled(false)
        {
        }

        virtual ~IComponent()
        {
        }

        Entity *GetEntity()
        {
            return m_entity;
        }

        void SetEntity(Entity *entity)
        {
            m_entity = entity;
        }

        bool IsEnabled()
        {
            return m_enabled;
        }

        void SetEnabled(bool enabled)
        {
            m_enabled = enabled;
        }

        virtual void Destroy()
        {
        }

    private:
        Entity *m_entity;
        bool m_enabled;
    };

    class Image;
    class CommandBuffer;
    class Camera;
    class Pipeline;
    class PipelineCreateInfo;
    class Descriptor;

    class IRenderComponent : public IComponent
    {
    public:
        virtual void Init() = 0;

        virtual void UpdatePipelineInfo() = 0;

        virtual void CreateUniforms(CommandBuffer *cmd) = 0;

        virtual void UpdateDescriptorSets() = 0;

        virtual void Update(Camera *camera) = 0;

        virtual void Draw(CommandBuffer *cmd, uint32_t imageIndex) = 0;

        virtual void Resize(uint32_t width, uint32_t height) = 0;

        virtual void Destroy() = 0;
    };
}
