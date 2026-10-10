#pragma once

#include "API/Image.h"

namespace pe
{
    class Buffer;
    class CommandBuffer;
    class IRenderPassComponent;

    using InputInfo = ImageBarrierInfo;

    struct OutputInfo
    {
        Image *image = nullptr;
        PeImageLayout finalLayout = PE_IMAGE_LAYOUT_UNDEFINED;
        PeBarrierSync stageFlags = PE_STAGE_NONE;
        PeBarrierAccess accessMask = PE_ACCESS_NONE;
        bool loads = false; // the pass keeps the image's contents (attachment LOAD): ordered after its writer
    };

    class RGBuilder
    {
    public:
        RGBuilder();

        void Barrier(Image *image,
                     PeImageLayout layout,
                     PeBarrierSync stageFlags,
                     PeBarrierAccess accessMask);

        void Read(Image *image);
        void ReadCompute(Image *image);
        void WriteCompute(Image *image);
        void ReadRayTracing(Image *image);
        void WriteRayTracing(Image *image);

        void OutputColor(Image *image, bool loads = false);
        void OutputDepth(Image *image, bool loads = false);
        void OutputCustom(Image *image, PeImageLayout layout, PeBarrierSync stage, PeBarrierAccess access);

        // Ordering only: no barrier is issued (passes record their own buffer barriers). Lets the
        // graph see GPU buffer hand-offs (cull draw lists, Forward+ tiles, particles) when it
        // checks that every read has an earlier writer.
        void ReadBuffer(Buffer *buffer);
        void WriteBuffer(Buffer *buffer);

        void Reset();

    private:
        friend class RenderGraph;

        std::vector<InputInfo> m_inputs;
        std::vector<OutputInfo> m_outputs;
        std::vector<Buffer *> m_bufferReads;
        std::vector<Buffer *> m_bufferWrites;
    };

    class RenderGraph
    {
    public:
        using PassID = uint32_t;
        using PassCallback = std::function<void(CommandBuffer *)>;

        // Passes with equal order form a column: they must not depend on each other, so the graph
        // issues their input barriers as one batch and records them in pass-id order.
        struct Pass
        {
            PassID id;
            uint32_t order;
            std::string name;
            std::function<bool()> condition;
            IRenderPassComponent *component;
            PassCallback callback;
            std::function<void(RGBuilder &)> declare; // a callback pass's reads/writes; empty = unknown
        };

        struct PassIO
        {
            std::vector<Image *> inputs;
            std::vector<Image *> outputs;
            std::vector<Buffer *> bufferReads;
            std::vector<Buffer *> bufferWrites;
        };

        // Found by Compile: a read with no earlier writer, two passes of one column sharing a resource that
        // one of them writes, or a pass sampling an image it renders into.
        struct Problem
        {
            PassID pass;
            const void *resource;
            std::string text;
        };

        void AddPass(PassID id, uint32_t order, std::string name, std::function<bool()> condition, IRenderPassComponent *component);
        void AddPass(PassID id, uint32_t order, std::string name, std::function<bool()> condition, PassCallback callback,
                     std::function<void(RGBuilder &)> declare = nullptr);
        void Compile(bool logProblems = true);
        void Execute(CommandBuffer *cmd);
        bool ContainsPass(PassID passID) const;
        std::span<const Pass> GetPasses() const { return m_passes; }
        // Changes the orders of the listed passes; Compile re-sorts.
        void SetPassOrders(const std::unordered_map<PassID, uint32_t> &orders);
        const std::vector<Problem> &GetProblems() const { return m_problems; }
        // The problems giving passes these orders would add; empty when the move is fine. Problems already
        // present at the current orders are not counted.
        std::vector<Problem> NewProblems(const std::unordered_map<PassID, uint32_t> &orders) const;
        void Clear();

    private:
        size_t FindPassIndex(PassID passID) const;
        void ExecuteSinglePass(CommandBuffer *cmd, Pass &pass, RGBuilder &builder, bool issueInputBarriers = true);
        void ExecuteColumn(CommandBuffer *cmd, size_t begin, size_t end);

        std::vector<Pass> m_passes;
        std::vector<Problem> m_problems;
        std::vector<InputInfo> m_columnBarriers;
        std::unordered_map<PassID, size_t> m_passIndex;
        std::vector<PassIO> m_passIO;
        std::vector<std::vector<size_t>> m_dependencies;
        RGBuilder m_builderScratch;
    };
} // namespace pe
