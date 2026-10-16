#include "MaterialThinPass.h"
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
        };
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
    }

    std::vector<PassInfo *> MaterialThinPass::GetPassInfos() noexcept
    {
        std::vector<PassInfo *> infos;
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

        if (sets.size() > 1 && sets[1]->HasBinding(0))
        {
            sets[1]->SetImageViews(0, scene->GetImageViews(), {});
            sets[1]->Update();
        }
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
            !scene->GetMaterialByteBuffer())
        {
            return;
        }

        for (const auto &[module, info] : m_modulePassInfos)
            BindFrame(*info, RHII.GetFrameIndex());

        m_bound = !m_modulePassInfos.empty();
    }

    void MaterialThinPass::DeclareInputs(RGBuilder &builder)
    {
        if (!m_visibility)
            return;

        builder.ReadCompute(m_visibility);

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

    void MaterialThinPass::ExecutePass(CommandBuffer *cmd)
    {
        Scene *scene = GetActiveScene();
        if (!scene || !m_visibility || !m_bound)
            return;

        PushConstants_MaterialThin pc{};
        pc.cameraPosition = vec4(scene->GetActiveCamera()->GetPosition(), 1.0f);
        pc.verticesOffset = static_cast<uint32_t>(scene->GetVerticesOffset());

        cmd->BeginDebugRegion("MaterialThinPass");
        for (const auto &[module, info] : m_modulePassInfos)
        {
            pc.moduleIndex = module;
            cmd->BindPipeline(*info);
            cmd->SetConstants(pc);
            cmd->PushConstants();
            cmd->Dispatch((m_visibility->GetWidth() * 2u + 7u) / 8u, (m_visibility->GetHeight() * 2u + 7u) / 8u, 1);
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
            for (const auto &[module, info] : m_modulePassInfos)
                info->DestroyShaders();

            UpdatePassInfo();
        }
    }

    void MaterialThinPass::Destroy()
    {
        m_modulePassInfos.clear();

        m_visibility = m_normal = m_albedo = m_srm = m_emissive = nullptr;
    }
} // namespace pe
