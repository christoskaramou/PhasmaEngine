#pragma once

namespace pe
{
    class Image;
    class CommandBuffer;
    class Scene;

    class AabbsPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {};
        void UpdateDescriptorSets() override {};
        void Update() override {};
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        void SetScene(Scene *scene) { m_scene = scene; }

    private:
        friend class Scene;

        Image *m_viewportRT;
        Image *m_depthRT;
        Scene *m_scene = nullptr;
    };
} // namespace pe
