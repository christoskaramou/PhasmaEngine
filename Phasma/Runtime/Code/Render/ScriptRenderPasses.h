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
        // Render targets the pass samples (fragment stage) and renders into, by name. The graph issues the read
        // barriers and checks the pass's place against them.
        std::vector<std::string> reads;
        std::vector<std::string> writes;
    };

    // A Lua pass added without an order (and no render_pass_orders "script:<name>" saved with the scene) runs in its
    // own column after the built-in passes.
    inline constexpr uint32_t kScriptRenderPassUnplacedOrder = 9000;

    void RegisterScriptRenderPass(const std::string &name, uint32_t order, std::function<void(CommandBuffer *)> execute,
                                  const void *owner = nullptr, std::vector<std::string> reads = {},
                                  std::vector<std::string> writes = {});
    void UnregisterScriptRenderPass(const std::string &name);
    // Removes only the owner's passes, so a Lua reload keeps the C++ scripts' passes.
    void ClearScriptRenderPasses(const void *owner = nullptr);

    // The owner Lua's render_graph.add_pass / add_fullscreen_pass give their passes: nullptr (Lua's, cleared on a Lua
    // reload) unless the scene's pipeline script is running, whose passes are cleared with it.
    void SetLuaRenderPassOwner(const void *owner);
    const void *GetLuaRenderPassOwner();

    const std::vector<ScriptRenderPass> &GetScriptRenderPasses();
    const ScriptRenderPass *FindScriptRenderPass(const std::string &name);
    // Bumped on every registry mutation; hosts rebuild their graph when it changes.
    uint64_t GetScriptRenderPassesRevision();
} // namespace pe
