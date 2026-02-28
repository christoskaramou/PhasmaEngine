#pragma once

#include "API/Image.h"

namespace pe
{
    class CommandBuffer;
    class IRenderPassComponent;

    class RGBuilder
    {
    public:
        RGBuilder()
        {
            m_barriers.reserve(16);
        }

        void Reset()
        {
            m_barriers.clear();
        }

        void Barrier(Image *image,
                     vk::ImageLayout layout,
                     vk::PipelineStageFlags2 stageFlags,
                     vk::AccessFlags2 accessMask);

        void Read(Image *image);
        void ReadCompute(Image *image);
        void WriteCompute(Image *image);
        void ReadRayTracing(Image *image);
        void WriteRayTracing(Image *image);

    private:
        std::vector<ImageBarrierInfo> m_barriers;
        friend class RenderGraph;
    };

    class RenderGraph
    {
    public:
        using PassID = uint32_t;

        struct Pass
        {
            PassID id;
            std::string name;
            std::function<bool()> condition;
            IRenderPassComponent *component;
        };

        void AddPass(PassID id, std::string name, std::function<bool()> condition, IRenderPassComponent *component);
        void Execute(CommandBuffer *cmd);
        void ExecuteBefore(CommandBuffer *cmd, PassID passID);
        void ExecuteFrom(CommandBuffer *cmd, PassID passID);
        bool ContainsPass(PassID passID) const;
        void Clear();

    private:
        size_t FindPassIndex(PassID passID) const;
        void ExecuteSinglePass(CommandBuffer *cmd, Pass &pass, RGBuilder &builder);
        std::vector<Pass> m_passes;
        std::unordered_map<PassID, size_t> m_passIndex;
        RGBuilder m_builderScratch;
    };
} // namespace pe
