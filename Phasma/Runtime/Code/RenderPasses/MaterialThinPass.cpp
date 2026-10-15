#include "MaterialThinPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/RenderGraph.h"
#include "API/Shader.h"
#include "Render/SceneRendererHost.h"
#include "Render/SceneRenderTargets.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    namespace
    {
        struct PushConstants_MaterialThin
        {
            uint32_t verticesOffset;
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

    void MaterialThinPass::UpdatePassInfo()
    {
        m_builtNormalFormat = m_normal ? m_normal->GetFormat() : PE_FORMAT_UNDEFINED;
        m_builtEmissiveFormat = m_emissive ? m_emissive->GetFormat() : PE_FORMAT_UNDEFINED;

        std::vector<Define> defines;
        if (m_builtNormalFormat == PE_FORMAT_R32G32B32A32_SFLOAT)
            defines.push_back(Define{"PE_NORMAL_RGBA32F", "1"});
        if (m_builtEmissiveFormat == PE_FORMAT_R8G8B8A8_UNORM)
            defines.push_back(Define{"PE_EMISSIVE_RGBA8", "1"});

        m_passInfo->name = "MaterialThin_pipeline";
        m_passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/MaterialX/MaterialThin.slang",
                                                  .entryPoint = "mainCS",
                                                  .stage = PE_SHADER_STAGE_COMPUTE,
                                                  .defines = defines});
        m_passInfo->Update();
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

        const uint32_t frame = RHII.GetFrameIndex();
        Descriptor *set = m_passInfo->GetDescriptors(frame)[0];

        set->SetImageView(0, m_visibility->GetSRV());
        set->SetBuffer(1, scene->GetBuffer());
        set->SetBuffer(2, scene->GetUniforms(frame));
        set->SetBuffer(3, scene->GetMeshConstants());
        set->SetBuffer(4, scene->GetMaterialByteBuffer());

        set->SetImageView(5, m_normal->GetUAV(0));
        set->SetImageView(6, m_albedo->GetUAV(0));
        set->SetImageView(7, m_srm->GetUAV(0));
        set->SetImageView(8, m_emissive->GetUAV(0));
        set->Update();

        m_bound = true;
    }

    void MaterialThinPass::DeclareInputs(RGBuilder &builder)
    {
        if (!m_visibility)
            return;

        builder.ReadCompute(m_visibility);

        // WriteCompute keeps the contents (layout change from the raster attachment): only MaterialX pixels change.
        for (Image *image : {m_normal, m_albedo, m_srm, m_emissive})
            builder.WriteCompute(image);
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
        pc.verticesOffset = static_cast<uint32_t>(scene->GetVerticesOffset());

        cmd->BeginDebugRegion("MaterialThinPass");
        cmd->BindPipeline(*m_passInfo);
        cmd->SetConstants(pc);
        cmd->PushConstants();
        cmd->Dispatch((m_visibility->GetWidth() * 2u + 7u) / 8u, (m_visibility->GetHeight() * 2u + 7u) / 8u, 1);
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
        if (m_passInfo->pCompShader && (normal != m_builtNormalFormat || emissive != m_builtEmissiveFormat))
        {
            m_passInfo->DestroyShaders();
            UpdatePassInfo();
        }
    }

    void MaterialThinPass::Destroy()
    {
        if (m_passInfo)
            Shader::Destroy(m_passInfo->pCompShader);

        m_visibility = m_normal = m_albedo = m_srm = m_emissive = nullptr;
    }
} // namespace pe
