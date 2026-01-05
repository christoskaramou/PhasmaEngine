#pragma once

namespace pe
{
    class Scene;

    class Buffer;
    class Image;
    class Sampler;

    class ParticlePass : public IRenderPassComponent
    {
    public:
        ~ParticlePass() override;;

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
        std::vector<Image *> m_fireTextures;
        Sampler *m_sampler = nullptr;
    };
} // namespace pe
