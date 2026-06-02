#pragma once
#include "API/Pipeline.h"
#include "API/RenderPass.h"

namespace pe
{
    class Descriptor;
    class Image;
    class CommandBuffer;
    class PassInfo;
    class Queue;

    class BloomBrightFilterPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override {};
        void DeclareInputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

    private:
        Image *m_brightFilterRT = nullptr; // Render target
        Image *m_displayRT = nullptr;      // Shader Input
    };

    class BloomGaussianBlurHorizontalPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override {};
        void DeclareInputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

    private:
        Image *m_gaussianBlurHorizontalRT = nullptr; // Render target
        Image *m_brightFilterRT = nullptr;           // Shader Input
    };

    class BloomGaussianBlurVerticalPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override;
        void UpdateDescriptorSets() override;
        void Update() override {};
        void DeclareInputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;

    private:
        Image *m_displayRT = nullptr;                // Render target
        Image *m_gaussianBlurHorizontalRT = nullptr; // Shader Input
    };
} // namespace pe
