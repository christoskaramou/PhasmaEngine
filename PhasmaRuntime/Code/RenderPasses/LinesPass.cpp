#include "LinesPass.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Material.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    void LinesPass::Init()
    {
        SceneRendererHost *rs = &RequireActiveSceneRendererHost();

        m_viewportRT = rs->GetRenderTarget("viewport");
        m_depthRT = rs->GetDepthStencilTarget("depthStencil");
        m_scene = nullptr;

        m_attachments.resize(2);
        m_attachments[0] = {};
        m_attachments[0].image = m_viewportRT;
        m_attachments[0].loadOp = PE_LOAD_OP_LOAD;
        m_attachments[0].storeOp = PE_STORE_OP_STORE;
        m_attachments[1] = {};
        m_attachments[1].image = m_depthRT;
        m_attachments[1].loadOp = PE_LOAD_OP_LOAD;
        m_attachments[1].storeOp = PE_STORE_OP_STORE;
    }

    void LinesPass::UpdatePassInfo()
    {
        m_passInfo->name = "Lines_pipeline";
        m_passInfo->pVertShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Utilities/LinesVS.hlsl", .entryPoint = "mainVS", .stage = PE_SHADER_STAGE_VERTEX, .defines = std::vector<Define>{}});
        m_passInfo->pFragShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Utilities/LinesPS.hlsl", .entryPoint = "mainPS", .stage = PE_SHADER_STAGE_FRAGMENT, .defines = std::vector<Define>{}});
        m_passInfo->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        m_passInfo->topology = PE_TOPOLOGY_LINE_STRIP;
        m_passInfo->cullMode = PE_CULL_MODE_NONE;
        m_passInfo->colorBlendAttachments = {BlendState::Default};
        m_passInfo->colorFormats = {m_viewportRT->GetFormat()};
        m_passInfo->depthFormat = m_depthRT->GetFormat();
        m_passInfo->depthTestEnable = true;
        m_passInfo->depthWriteEnable = false;
        m_passInfo->Update();
    }

    void LinesPass::Update()
    {
        Scene &scene = *GetActiveScene();
        if (scene.HasLinesMeshes())
        {
            uint32_t frame = RHII.GetFrameIndex();
            const auto &sets = m_passInfo->GetDescriptors(frame);
            Descriptor *setUniforms = sets[0];
            setUniforms->SetBuffer(0, scene.GetUniforms(frame));
            setUniforms->Update();
        }
    }

    void LinesPass::ExecutePass(CommandBuffer *cmd)
    {
        PE_ERROR_IF(m_scene == nullptr, "Scene was not set");
        Scene *scene = m_scene;
        m_scene = nullptr;
        if (!scene->HasLinesMeshes())
            return;

        struct PushConstants_Lines
        {
            vec4 color;
            uint32_t meshIndex;
        } constants{};

        cmd->BeginDebugRegion("Lines");

        cmd->BeginPass(2, m_attachments.data(), "LinesPass");
        cmd->SetViewport(0.f, 0.f, m_viewportRT->GetWidth_f(), m_viewportRT->GetHeight_f());
        cmd->SetScissor(0, 0, m_viewportRT->GetWidth(), m_viewportRT->GetHeight());
        cmd->BindPipeline(*m_passInfo);
        cmd->BindIndexBuffer(scene->GetBuffer(), 0);
        cmd->BindVertexBuffer(scene->GetBuffer(), scene->GetPositionsOffset());

        for (uint32_t i = 0; i < scene->GetNodeCount(); i++)
        {
            NodeId *node = scene->GetNodeId(i);
            const NodeRuntime &rt = scene->GetNodeRuntime(node);
            if (rt.gpuPending)
                continue;

            const auto &refs = scene->GetMeshRefs(node);
            if (refs.empty())
                continue;

            for (int meshIdx : refs)
            {
                if (meshIdx < 0)
                    continue;

                const Mesh &mesh = scene->GetMesh(meshIdx);
                if (mesh.renderType != RenderType::Lines || mesh.indexCount == 0)
                    continue;

                vec3 emissive = mesh.materialInstance ? mesh.materialInstance->GetEmissiveFactor()
                                                      : (mesh.material ? mesh.material->emissiveFactor : vec3(1.f));
                vec4 baseColor = mesh.materialInstance ? mesh.materialInstance->GetBaseColorFactor()
                                                       : (mesh.material ? mesh.material->baseColorFactor : vec4(1.f));
                // Emissive is the line color when set (lit shading is meaningless for
                // guide lines); base_color is the fallback.
                constants.color = dot(emissive, emissive) > 0.f ? vec4(emissive, baseColor.a) : baseColor;
                constants.meshIndex = GetUboDataOffset(rt.dataOffset);

                cmd->SetConstants(constants);
                cmd->PushConstants();
                cmd->DrawIndexed(mesh.indexCount, 1, mesh.indexOffset, mesh.vertexOffset, 0);
            }
        }

        cmd->EndPass();

        cmd->EndDebugRegion();
    }

    void LinesPass::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void LinesPass::Destroy()
    {
    }
} // namespace pe
