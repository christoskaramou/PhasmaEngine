#pragma once

namespace pe
{
    class Entity;

    class IComponent
    {
    public:
        IComponent() noexcept : m_entity{nullptr}, m_enabled{false} {}
        virtual ~IComponent() = default;

        [[nodiscard]] Entity *GetEntity() const noexcept { return m_entity; }
        void SetEntity(Entity *entity) noexcept { m_entity = entity; }
        [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }
        void SetEnabled(bool enabled) noexcept { m_enabled = enabled; }
        virtual void Destroy() {}

    private:
        Entity *m_entity;
        bool m_enabled;
    };

    class CommandBuffer;
    struct Attachment;
    class Descriptor;
    class PassInfo;

    class IRenderPassComponent : public IComponent
    {
    public:
        IRenderPassComponent();
        virtual ~IRenderPassComponent();

        virtual void Init() = 0;
        virtual void UpdatePassInfo() = 0;
        virtual void CreateUniforms(CommandBuffer *cmd) = 0;
        virtual void UpdateDescriptorSets() = 0;
        virtual void Update() = 0;
        virtual void Draw(CommandBuffer *cmd) = 0;
        virtual void Resize(uint32_t width, uint32_t height) = 0;
        virtual void Destroy() = 0;

        [[nodiscard]] std::vector<Attachment> &GetAttachments() noexcept { return m_attachments; }
        [[nodiscard]] std::shared_ptr<PassInfo> GetPassInfo() const noexcept { return m_passInfo; }

    protected:
        std::vector<Attachment> m_attachments;
        std::shared_ptr<PassInfo> m_passInfo;
    };
} // namespace pe
