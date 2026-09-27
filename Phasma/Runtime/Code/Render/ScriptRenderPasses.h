#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pe
{
    class CommandBuffer;

    // Script-authored render passes. Lua registers a named callback with an order in
    // the same space as SceneRenderGraphPassDesc orders; both renderer hosts re-add
    // the registered passes whenever they (re)build their render graph, so a pass can
    // be inserted anywhere relative to the built-in passes. Callbacks record into the
    // frame's command buffer during RenderGraph::Execute - no extra queue submissions.
    inline constexpr uint32_t kScriptRenderPassIdBase = 0x53520000u; // clear of SceneRenderGraphPassId and editor GUI ids

    struct ScriptRenderPass
    {
        std::string name;
        uint32_t order = 0;
        std::function<void(CommandBuffer *)> execute;
        const void *owner = nullptr; // nullptr: Lua's; the C++ script system passes itself
    };

    void RegisterScriptRenderPass(const std::string &name, uint32_t order, std::function<void(CommandBuffer *)> execute,
                                  const void *owner = nullptr);
    void UnregisterScriptRenderPass(const std::string &name);
    // Removes only the owner's passes, so a Lua reload keeps the C++ scripts' passes.
    void ClearScriptRenderPasses(const void *owner = nullptr);

    const std::vector<ScriptRenderPass> &GetScriptRenderPasses();
    const ScriptRenderPass *FindScriptRenderPass(const std::string &name);
    // Bumped on every registry mutation; hosts rebuild their graph when it changes.
    uint64_t GetScriptRenderPassesRevision();
} // namespace pe
