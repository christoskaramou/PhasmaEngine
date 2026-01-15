#pragma once

namespace pe
{
    class Scene;
    class Image;
    class AccelerationStructure;

    class RayTracingPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

        void SetScene(Scene *scene) { m_scene = scene; }

    private:
        friend class Scene;
        Scene *m_scene = nullptr;
        Image *m_display = nullptr;
        AccelerationStructure *m_tlas = nullptr; // used to check if tlas is updated
    };
} // namespace pe
