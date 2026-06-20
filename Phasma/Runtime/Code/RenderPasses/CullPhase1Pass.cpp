#include "CullPhase1Pass.h"
#include "API/Command.h"
#include "API/Pipeline.h"
#include "API/Shader.h"
#include "Scene/Scene.h"

namespace pe
{
    void CullPhase1Pass::UpdatePassInfo()
    {
        Shader *oldComp = m_passInfo->pCompShader;
        m_passInfo->name = "CullPhase1_pipeline";
        try
        {
            m_passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Compute/CullingCS.hlsl",
                                                      .entryPoint = "mainCS",
                                                      .stage = PE_SHADER_STAGE_COMPUTE,
                                                      .defines = std::vector<Define>{Define{"PHASE1", "1"}}});
            m_passInfo->Update();
        }
        catch (...)
        {
            if (m_passInfo->pCompShader != oldComp)
                Shader::Destroy(m_passInfo->pCompShader);
            m_passInfo->pCompShader = oldComp;
            throw;
        }
    }

    void CullPhase1Pass::ExecutePass(CommandBuffer *cmd)
    {
        if (!m_scene || m_scene->GetMeshCount() == 0)
            return;

        cmd->BeginDebugRegion("CullPhase1Pass");
        m_scene->DispatchCullingPhase(cmd, m_passInfo.get(), Scene::CullPhase::Phase1);
        cmd->EndDebugRegion();
    }

    void CullPhase1Pass::Destroy()
    {
        if (m_passInfo)
            Shader::Destroy(m_passInfo->pCompShader);
    }
} // namespace pe
