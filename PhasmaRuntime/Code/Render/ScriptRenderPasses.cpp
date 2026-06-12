#include "Render/ScriptRenderPasses.h"

#include <algorithm>
#include <utility>

namespace pe
{
    namespace
    {
        std::vector<ScriptRenderPass> s_scriptRenderPasses;
        uint64_t s_scriptRenderPassesRevision = 0;
    } // namespace

    void RegisterScriptRenderPass(const std::string &name, uint32_t order, std::function<void(CommandBuffer *)> execute)
    {
        for (auto &pass : s_scriptRenderPasses)
        {
            if (pass.name == name)
            {
                pass.order = order;
                pass.execute = std::move(execute);
                s_scriptRenderPassesRevision++;
                return;
            }
        }
        s_scriptRenderPasses.push_back({name, order, std::move(execute)});
        s_scriptRenderPassesRevision++;
    }

    void UnregisterScriptRenderPass(const std::string &name)
    {
        auto it = std::remove_if(s_scriptRenderPasses.begin(), s_scriptRenderPasses.end(),
                                 [&name](const ScriptRenderPass &pass)
                                 { return pass.name == name; });
        if (it == s_scriptRenderPasses.end())
            return;
        s_scriptRenderPasses.erase(it, s_scriptRenderPasses.end());
        s_scriptRenderPassesRevision++;
    }

    void ClearScriptRenderPasses()
    {
        if (s_scriptRenderPasses.empty())
            return;
        s_scriptRenderPasses.clear();
        s_scriptRenderPassesRevision++;
    }

    const std::vector<ScriptRenderPass> &GetScriptRenderPasses()
    {
        return s_scriptRenderPasses;
    }

    const ScriptRenderPass *FindScriptRenderPass(const std::string &name)
    {
        for (const auto &pass : s_scriptRenderPasses)
        {
            if (pass.name == name)
                return &pass;
        }
        return nullptr;
    }

    uint64_t GetScriptRenderPassesRevision()
    {
        return s_scriptRenderPassesRevision;
    }
} // namespace pe
