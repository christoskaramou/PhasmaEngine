#pragma once

struct FfxFsr2Context;
struct FfxFsr2ContextDescription;
struct FfxFsr2DispatchDescription;

namespace pe
{
    class CommandBuffer;
    class Image;
    class Camera;

    class SuperResolution : public IRenderComponent
    {
    public:
        SuperResolution();

        ~SuperResolution();

        void Init() override;

        void UpdatePassInfo() override;

        void CreateUniforms(CommandBuffer *cmd) override;

        void UpdateDescriptorSets() override;

        void Update(Camera *camera) override;

        void Draw(CommandBuffer *cmd, uint32_t imageIndex) override;

        void Resize(uint32_t width, uint32_t height) override;

        void Destroy() override;

        void GenerateJitter();

        const vec2 &GetJitter() { return m_jitter; }

        const vec2 &GetProjectionJitter() { return m_projectionJitter; }

        Image *GetOutput() { return m_display; }

    private:
        std::shared_ptr<FfxFsr2Context> m_context;
        std::shared_ptr<FfxFsr2ContextDescription> m_contextDescription;
        float m_memoryUsageInMegabytes;
        Image *m_display;
        Image *m_viewportRT;
        Image *m_velocityRT;
        Image *m_depth;
        vec2 m_jitter;
        vec2 m_projectionJitter;
    };
}
