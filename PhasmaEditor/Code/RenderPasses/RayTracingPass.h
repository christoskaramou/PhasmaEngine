#pragma once

namespace pe
{
    class Scene;
    class Image;
    class Buffer; // Forward declaration
    class AccelerationStructure;

    struct RayTracingPassUBO
    {
        mat4 invViewProj;
        mat4 invView;
        mat4 invProj;
        float lights_intensity;
        float lights_range;
        uint32_t shadows;
        float pad;
    };

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
        std::vector<Buffer *> m_uniforms;
    };
} // namespace pe
