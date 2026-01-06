#pragma once

namespace pe
{
    class CommandBuffer;
    class Scene;
    class Buffer;

    class ParticleComputePass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override;
        void Draw(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        void SetScene(Scene *scene) { m_scene = scene; }

    private:
        Scene *m_scene = nullptr;
        uint32_t m_lastBufferVersion = 0;
    };
} // namespace pe
