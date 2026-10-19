#include "MaterialThinPass.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Render/SceneRendererHost.h"
#include "Render/SceneRenderTargets.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/OpenPBRMaterial.h"

namespace pe
{
    namespace
    {
        struct PushConstants_MaterialThin
        {
            vec4 cameraPosition;
            uint32_t verticesOffset;
            uint32_t moduleIndex;
            uint32_t binsCount;
            uint32_t binsTiles;
        };

        struct PushConstants_MaterialClassify
        {
            uint32_t slotCount;
            uint32_t mapSize;
            uint32_t capacity;
            uint32_t tilesOffset;
        };

        uint32_t BinTileCount(const Image *visibility)
        {
            return ((visibility->GetWidth() + 7u) / 8u) * ((visibility->GetHeight() + 7u) / 8u);
        }

        void RetireBuffer(Buffer *&buffer)
        {
            if (buffer)
            {
                RHII.AddToDeletionQueue([b = buffer]()
                                        { Buffer *old = b; Buffer::Destroy(old); });
            }

            buffer = nullptr;
        }
    } // namespace

    void MaterialThinPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();
        m_visibility = rs->GetRenderTarget("visibility");
        m_normal = rs->GetRenderTarget("normal");
        m_albedo = rs->GetRenderTarget("albedo");
        m_srm = rs->GetRenderTarget("srm");
        m_emissive = rs->GetRenderTarget("emissive");
    }

    void MaterialThinPass::BuildPipeline(PassInfo &info, uint32_t module)
    {
        std::vector<Define> defines;
        if (m_builtNormalFormat == PE_FORMAT_R32G32B32A32_SFLOAT)
            defines.push_back(Define{"PE_NORMAL_RGBA32F", "1"});

        if (m_builtEmissiveFormat == PE_FORMAT_R8G8B8A8_UNORM)
            defines.push_back(Define{"PE_EMISSIVE_RGBA8", "1"});

        info.name = "MaterialThin_pipeline_" + std::to_string(module);
        info.pCompShader = Shader::Create({.sourcePath = GetOpenPBRModule(module).thinSource,
                                           .entryPoint = "mainCS",
                                           .stage = PE_SHADER_STAGE_COMPUTE,
                                           .defines = defines});
        info.Update();
    }

    void MaterialThinPass::UpdatePassInfo()
    {
        m_builtNormalFormat = m_normal ? m_normal->GetFormat() : PE_FORMAT_UNDEFINED;
        m_builtEmissiveFormat = m_emissive ? m_emissive->GetFormat() : PE_FORMAT_UNDEFINED;

        m_passInfo->name = "MaterialClassify_pipeline";
        m_passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/MaterialX/MaterialClassify.slang",
                                                  .entryPoint = "mainCS",
                                                  .stage = PE_SHADER_STAGE_COMPUTE,
                                                  .defines = std::vector<Define>{}});
        m_passInfo->Update();

        for (const auto &[module, info] : m_modulePassInfos)
            BuildPipeline(*info, module);
    }

    void MaterialThinPass::SyncModules(const std::vector<uint32_t> &modules)
    {
        for (auto it = m_modulePassInfos.begin(); it != m_modulePassInfos.end();)
        {
            if (std::find(modules.begin(), modules.end(), it->first) != modules.end())
            {
                ++it;
                continue;
            }

            RHII.AddToDeletionQueue([retired = it->second] {});
            it = m_modulePassInfos.erase(it);
        }

        for (uint32_t module : modules)
        {
            if (m_modulePassInfos.contains(module))
                continue;

            auto info = std::make_shared<PassInfo>();
            BuildPipeline(*info, module);
            m_modulePassInfos.emplace(module, std::move(info));
        }

        SyncBins(modules);
    }

    // On the main thread before the passes' concurrent Update: a bin per scene module, each holding every tile.
    // ponytail: 4 bytes per tile per module (1080p: 127 KiB a module); a count / prefix-sum / scatter pass into one
    // shared list would size it to the tiles actually covered, once module counts make this memory matter.
    void MaterialThinPass::SyncBins(const std::vector<uint32_t> &modules)
    {
        m_slotCount = static_cast<uint32_t>(modules.size());
        const uint32_t mapSize = modules.empty() ? 1u : *std::max_element(modules.begin(), modules.end()) + 1u;
        m_moduleSlots.assign(mapSize, UINT32_MAX);
        for (uint32_t slot = 0; slot < m_slotCount; ++slot)
            m_moduleSlots[modules[slot]] = slot;

        const uint32_t capacity = m_visibility ? BinTileCount(m_visibility) : 0u;
        if (!m_bins || m_slotCount > m_binSlots || capacity != m_binCapacity)
        {
            RetireBuffer(m_bins);
            m_binSlots = std::max(m_slotCount, 1u);
            m_binCapacity = capacity;
            m_bins = Buffer::Create({
                .size = size_t(m_binSlots) * 16u + size_t(m_binSlots) * m_binCapacity * 4u,
                .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_INDIRECT_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
                .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY,
                .name = "MaterialClassify_bins",
            });
        }

        const size_t mapBytes = m_moduleSlots.size() * sizeof(uint32_t);
        if (m_slotMaps.size() != RHII.GetSwapchainImageCount() || m_slotMaps[0]->Size() < mapBytes)
        {
            for (Buffer *&map : m_slotMaps)
                RetireBuffer(map);

            m_slotMaps.assign(RHII.GetSwapchainImageCount(), nullptr);
            for (Buffer *&map : m_slotMaps)
            {
                map = Buffer::Create({
                    .size = mapBytes,
                    .usage = PE_BUFFER_USAGE_STORAGE_BUFFER,
                    .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                    .name = "MaterialClassify_module_slots",
                });
            }
        }
    }

    bool MaterialThinPass::BinsReady() const
    {
        return m_bins && m_binnedFrame == RHII.GetFrameCounter();
    }

    uint32_t MaterialThinPass::BinSlot(uint32_t module) const
    {
        return module < m_moduleSlots.size() ? m_moduleSlots[module] : UINT32_MAX;
    }

    std::vector<PassInfo *> MaterialThinPass::GetPassInfos() noexcept
    {
        std::vector<PassInfo *> infos{m_passInfo.get()};
        for (const auto &[module, info] : m_modulePassInfos)
            infos.push_back(info.get());

        return infos;
    }

    void MaterialThinPass::BindFrame(PassInfo &info, uint32_t frame)
    {
        Scene *scene = GetActiveScene();
        const auto &sets = info.GetDescriptors(frame);
        Descriptor *set = sets[0];

        if (set->HasBinding(0))
            set->SetImageView(0, m_visibility->GetSRV());
        if (set->HasBinding(1))
            set->SetBuffer(1, scene->GetBuffer());
        if (set->HasBinding(2))
            set->SetBuffer(2, scene->GetUniforms(frame));
        if (set->HasBinding(3))
            set->SetBuffer(3, scene->GetMeshConstants());
        if (set->HasBinding(4))
            set->SetBuffer(4, scene->GetMaterialByteBuffer());
        if (set->HasBinding(10))
            set->SetBuffer(10, scene->GetOpenPBRSkinningBinding(frame));
        if (set->HasBinding(11))
            set->SetBuffer(11, m_bins);

        if (set->HasBinding(5))
            set->SetImageView(5, m_normal->GetUAV(0));
        if (set->HasBinding(6))
            set->SetImageView(6, m_albedo->GetUAV(0));
        if (set->HasBinding(7))
            set->SetImageView(7, m_srm->GetUAV(0));
        if (set->HasBinding(8))
            set->SetImageView(8, m_emissive->GetUAV(0));

        if (set->HasBinding(9))
            set->SetSamplers(9, scene->GetOpenPBRSamplers());

        set->Update();
    }

    void MaterialThinPass::BindClassify(uint32_t frame)
    {
        BufferRange range{};
        range.data = m_moduleSlots.data();
        range.size = m_moduleSlots.size() * sizeof(uint32_t);
        range.offset = 0;
        m_slotMaps[frame]->Copy(1, &range, false);

        Scene *scene = GetActiveScene();
        Descriptor *set = m_passInfo->GetDescriptors(frame)[0];
        set->SetImageView(0, m_visibility->GetSRV());
        set->SetBuffer(1, scene->GetMeshConstants());
        set->SetBuffer(2, scene->GetMaterialByteBuffer());
        set->SetBuffer(3, m_slotMaps[frame]);
        set->SetBuffer(4, m_bins);
        set->Update();
    }

    void MaterialThinPass::Update()
    {
        m_bound = false;

        Scene *scene = GetActiveScene();
        if (!scene ||
            !m_visibility ||
            !m_normal ||
            !m_albedo ||
            !m_srm ||
            !m_emissive ||
            !scene->GetBuffer() ||
            !scene->GetMaterialByteBuffer() ||
            !m_bins ||
            m_slotMaps.size() != RHII.GetSwapchainImageCount())
        {
            return;
        }

        BindClassify(RHII.GetFrameIndex());
        for (const auto &[module, info] : m_modulePassInfos)
            BindFrame(*info, RHII.GetFrameIndex());

        m_bound = !m_modulePassInfos.empty();
    }

    void MaterialThinPass::DeclareInputs(RGBuilder &builder)
    {
        if (!m_visibility)
            return;

        builder.ReadCompute(m_visibility);
        builder.WriteBuffer(m_bins);
        if (Scene *scene = GetActiveScene())
            builder.ReadBuffer(scene->GetOpenPBRSkinningBuffer(RHII.GetFrameIndex()));

        // WriteCompute keeps the contents (layout change from the raster attachment): only MaterialX pixels change.
        for (Image *image : {m_normal, m_albedo, m_emissive})
            builder.WriteCompute(image);

        builder.Barrier(m_srm, PE_IMAGE_LAYOUT_GENERAL, PE_STAGE_COMPUTE_SHADER,
                        PE_ACCESS_SHADER_STORAGE_READ | PE_ACCESS_SHADER_STORAGE_WRITE);
    }

    void MaterialThinPass::DeclareOutputs(RGBuilder &builder)
    {
        if (!m_visibility)
            return;

        for (Image *image : {m_normal, m_albedo, m_srm, m_emissive})
            builder.OutputCustom(image, PE_IMAGE_LAYOUT_GENERAL, PE_STAGE_COMPUTE_SHADER, PE_ACCESS_SHADER_WRITE);
    }

    void MaterialThinPass::Classify(CommandBuffer *cmd)
    {
        cmd->BeginDebugRegion("MaterialClassifyPass");
        cmd->FillBuffer(m_bins, 0, BinArgsOffset(m_slotCount), 0);

        BufferBarrierInfo barrier{};
        barrier.buffer = m_bins;
        barrier.stageMask = PE_STAGE_COMPUTE_SHADER;
        barrier.accessMask = PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE;
        cmd->BufferBarrier(barrier);

        PushConstants_MaterialClassify pc{};
        pc.slotCount = m_slotCount;
        pc.mapSize = static_cast<uint32_t>(m_moduleSlots.size());
        pc.capacity = m_binCapacity;
        pc.tilesOffset = BinTilesOffset(0);
        cmd->BindPipeline(*m_passInfo);
        cmd->SetConstants(pc);
        cmd->PushConstants();
        cmd->Dispatch((m_visibility->GetWidth() + 7u) / 8u, (m_visibility->GetHeight() + 7u) / 8u, 1);

        // Both material passes read the bins as indirect arguments and as tile lists.
        barrier.stageMask = PE_STAGE_DRAW_INDIRECT | PE_STAGE_COMPUTE_SHADER;
        barrier.accessMask = PE_ACCESS_INDIRECT_COMMAND_READ | PE_ACCESS_SHADER_READ;
        cmd->BufferBarrier(barrier);
        cmd->EndDebugRegion();

        m_binnedFrame = RHII.GetFrameCounter();
    }

    void MaterialThinPass::ExecutePass(CommandBuffer *cmd)
    {
        Scene *scene = GetActiveScene();
        if (!scene || !m_visibility || !m_bound || BinTileCount(m_visibility) > m_binCapacity)
            return;

        Classify(cmd);

        PushConstants_MaterialThin pc{};
        pc.cameraPosition = vec4(scene->GetActiveCamera()->GetPosition(), 1.0f);
        pc.verticesOffset = static_cast<uint32_t>(scene->GetVerticesOffset());

        cmd->BeginDebugRegion("MaterialThinPass");
        for (const auto &[module, info] : m_modulePassInfos)
        {
            const uint32_t slot = BinSlot(module);
            if (slot == UINT32_MAX)
                continue;

            pc.moduleIndex = module;
            pc.binsCount = BinArgsOffset(slot) + 12u;
            pc.binsTiles = BinTilesOffset(slot);
            cmd->BindPipeline(*info);
            cmd->SetConstants(pc);
            cmd->PushConstants();
            cmd->DispatchIndirect(m_bins, BinArgsOffset(slot));
        }

        cmd->EndDebugRegion();
    }

    void MaterialThinPass::Resize(uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;
        Init();

        // normal_format and HDR change the storage formats: rebuild (only graphics passes are rebuilt for us).
        const ::PeFormat normal = m_normal ? m_normal->GetFormat() : PE_FORMAT_UNDEFINED;
        const ::PeFormat emissive = m_emissive ? m_emissive->GetFormat() : PE_FORMAT_UNDEFINED;
        if (normal != m_builtNormalFormat || emissive != m_builtEmissiveFormat)
        {
            m_passInfo->DestroyShaders();
            for (const auto &[module, info] : m_modulePassInfos)
                info->DestroyShaders();

            UpdatePassInfo();
        }
    }

    void MaterialThinPass::Destroy()
    {
        m_modulePassInfos.clear();
        if (m_passInfo)
            Shader::Destroy(m_passInfo->pCompShader);

        Buffer::Destroy(m_bins);
        for (Buffer *&map : m_slotMaps)
            Buffer::Destroy(map);

        m_slotMaps.clear();
        m_moduleSlots.clear();
        m_slotCount = m_binSlots = m_binCapacity = 0;
        m_binnedFrame = UINT32_MAX;

        m_visibility = m_normal = m_albedo = m_srm = m_emissive = nullptr;
    }
} // namespace pe
