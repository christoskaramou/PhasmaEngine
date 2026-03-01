#pragma once

#include "API/Image.h"

namespace pe
{
    class CommandBuffer;
    class IRenderPassComponent;

    using InputInfo = ImageBarrierInfo;

    struct OutputInfo
    {
        Image *image = nullptr;
        vk::ImageLayout finalLayout = vk::ImageLayout::eUndefined;
        vk::PipelineStageFlags2 stageFlags = vk::PipelineStageFlagBits2::eNone;
        vk::AccessFlags2 accessMask = vk::AccessFlagBits2::eNone;
    };

    class RGBuilder
    {
    public:
        RGBuilder();

        void Barrier(Image *image,
                     vk::ImageLayout layout,
                     vk::PipelineStageFlags2 stageFlags,
                     vk::AccessFlags2 accessMask);

        void Read(Image *image);
        void ReadCompute(Image *image);
        void WriteCompute(Image *image);
        void ReadRayTracing(Image *image);
        void WriteRayTracing(Image *image);

        void OutputColor(Image *image);
        void OutputDepth(Image *image);
        void OutputCustom(Image *image, vk::ImageLayout layout, vk::PipelineStageFlags2 stage, vk::AccessFlags2 access);

        void Reset();

    private:
        friend class RenderGraph;

        std::vector<InputInfo> m_inputs;
        std::vector<OutputInfo> m_outputs;
    };

    class RenderGraph
    {
    public:
        using PassID = uint32_t;
        using PassCallback = std::function<void(CommandBuffer *)>;

        struct Pass
        {
            PassID id;
            std::string name;
            std::function<bool()> condition;
            IRenderPassComponent *component;
            PassCallback callback;
        };

        struct PassIO
        {
            std::vector<Image *> inputs;
            std::vector<Image *> outputs;
        };

        void AddPass(PassID id, std::string name, std::function<bool()> condition, IRenderPassComponent *component);
        void AddPass(PassID id, std::string name, std::function<bool()> condition, PassCallback callback);
        void Compile();
        void Execute(CommandBuffer *cmd);
        bool ContainsPass(PassID passID) const;
        void Clear();

    private:
        size_t FindPassIndex(PassID passID) const;
        void ExecuteSinglePass(CommandBuffer *cmd, Pass &pass, RGBuilder &builder);

        std::vector<Pass> m_passes;
        std::unordered_map<PassID, size_t> m_passIndex;
        std::vector<PassIO> m_passIO;
        std::vector<std::vector<size_t>> m_dependencies;
        RGBuilder m_builderScratch;
    };
} // namespace pe
