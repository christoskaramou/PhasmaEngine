#include "CullingPass.h"
#include "API/Pipeline.h"
#include "API/Command.h"
#include "API/Shader.h"
#include "Scene/Scene.h"

namespace pe
{
    void CullingPass::Init()
    {
    }

    void CullingPass::UpdatePassInfo()
    {
        Shader *oldComp = m_passInfo->pCompShader;
        m_passInfo->name = "Culling_pipeline";
        try
        {
            m_passInfo->pCompShader = Shader::Create(Path::Assets + "Shaders/Compute/CullingCS.hlsl", vk::ShaderStageFlagBits::eCompute, "mainCS", std::vector<Define>{}, ShaderCodeType::HLSL);
            m_passInfo->Update();
            if (oldComp != m_passInfo->pCompShader)
                Shader::Destroy(oldComp);
        }
        catch (...)
        {
            if (m_passInfo->pCompShader != oldComp)
                Shader::Destroy(m_passInfo->pCompShader);
            m_passInfo->pCompShader = oldComp;
            throw;
        }

        Shader *oldSortComp = m_sortPassInfo->pCompShader;
        m_sortPassInfo->name = "BitonicSort_pipeline";
        try
        {
            m_sortPassInfo->pCompShader = Shader::Create(Path::Assets + "Shaders/Compute/BitonicSortCS.hlsl", vk::ShaderStageFlagBits::eCompute, "mainCS", std::vector<Define>{}, ShaderCodeType::HLSL);
            m_sortPassInfo->Update();
            if (oldSortComp != m_sortPassInfo->pCompShader)
                Shader::Destroy(oldSortComp);
        }
        catch (...)
        {
            if (m_sortPassInfo->pCompShader != oldSortComp)
                Shader::Destroy(m_sortPassInfo->pCompShader);
            m_sortPassInfo->pCompShader = oldSortComp;
            throw;
        }
    }

    void CullingPass::ExecutePass(CommandBuffer *cmd)
    {
        if (!m_scene || m_scene->GetMeshCount() == 0)
            return;

        cmd->BeginDebugRegion("CullingPass");

        m_scene->DispatchCulling(cmd, m_passInfo.get(), m_sortPassInfo.get());

        cmd->EndDebugRegion();
    }

    void CullingPass::Destroy()
    {
        Shader::Destroy(m_passInfo->pCompShader);
        Shader::Destroy(m_sortPassInfo->pCompShader);
        m_sortPassInfo.reset();
    }
} // namespace pe
