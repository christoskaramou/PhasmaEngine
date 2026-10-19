#pragma once

namespace pe
{
    class Buffer;
    class CommandBuffer;
    class Image;

    // Material pass 1 (desktop OpenPBR): writes MaterialX's normal, albedo, roughness and metalness into the G-buffer
    // for visibility pixels of MaterialX materials, before SSAO/GI/reflections read it. It first bins the visibility
    // buffer's 8x8-pixel tiles per module (MaterialClassify.slang); passes 1 and 2 dispatch each module over its bin.
    class MaterialThinPass : public IRenderPassComponent
    {
    public:
        void Init() override;
        void UpdatePassInfo() override;
        void CreateUniforms(CommandBuffer *cmd) override {}
        void UpdateDescriptorSets() override { Update(); }
        void Update() override;
        void DeclareInputs(RGBuilder &builder) override;
        void DeclareOutputs(RGBuilder &builder) override;
        void ExecutePass(CommandBuffer *cmd) override;
        void Resize(uint32_t width, uint32_t height) override;
        void Destroy() override;
        void SyncModules(const std::vector<uint32_t> &modules);
        std::vector<PassInfo *> GetPassInfos() noexcept override;

        // The bins, filled this frame once BinsReady(): per bin uint4(group counts, tile count) at BinArgsOffset, its
        // tiles at BinTilesOffset (VisibilityCommon.slang PeBinThread).
        Buffer *GetBins() const { return m_bins; }
        bool BinsReady() const;
        uint32_t BinSlot(uint32_t module) const;
        uint32_t BinArgsOffset(uint32_t slot) const { return slot * 16u; }
        uint32_t BinTilesOffset(uint32_t slot) const { return m_binSlots * 16u + slot * m_binCapacity * 4u; }

    private:
        void BuildPipeline(PassInfo &info, uint32_t module);
        void BindFrame(PassInfo &info, uint32_t frame);
        void SyncBins(const std::vector<uint32_t> &modules);
        void BindClassify(uint32_t frame);
        void Classify(CommandBuffer *cmd);

        std::map<uint32_t, std::shared_ptr<PassInfo>> m_modulePassInfos;
        Buffer *m_bins = nullptr;
        std::vector<Buffer *> m_slotMaps;    // per frame: m_moduleSlots for MaterialClassify
        std::vector<uint32_t> m_moduleSlots; // runtime module index -> bin, UINT32_MAX for none
        uint32_t m_slotCount = 0;            // bins in use: the scene's modules
        uint32_t m_binSlots = 0;             // bins allocated
        uint32_t m_binCapacity = 0;          // tiles per bin
        uint32_t m_binnedFrame = UINT32_MAX; // RHI frame counter of the last Classify
        Image *m_visibility = nullptr;
        Image *m_normal = nullptr;
        Image *m_albedo = nullptr;
        Image *m_srm = nullptr;
        Image *m_emissive = nullptr;
        ::PeFormat m_builtNormalFormat = PE_FORMAT_UNDEFINED;
        ::PeFormat m_builtEmissiveFormat = PE_FORMAT_UNDEFINED;
        bool m_bound = false; // this frame's descriptors were written: ExecutePass may dispatch
    };
} // namespace pe
