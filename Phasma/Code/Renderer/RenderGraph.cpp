#include "Renderer/RenderGraph.h"
#include "Renderer/RenderPass.h"

namespace pe
{
    void RenderGraph::AddPass(const Func &func, const std::vector<Dependency> &dependancies)
    {
        m_passes.push_back(std::make_pair(func, dependancies));
    }

    void RenderGraph::Execute()
    {
        for (auto &pass : m_passes)
        {
            for (auto &dep : pass.second)
            {
            }
        }
    }

    void RenderGraph::ClearPasses()
    {
        m_passes.clear();
    }
}
