#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Command.h"
#include "API/Image.h"
#include "Render/SceneRendererHost.h"
#include "Render/ScriptRenderPasses.h"

#include <memory>

namespace pe
{
    static constexpr int kScriptRenderPassMaxErrors = 3;

    static struct RenderGraphBindings
    {
        RenderGraphBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table rg = lua.create_named_table("render_graph");

                // render_graph.add_pass(name, order, fn)
                // fn(cmd) records into the frame's command buffer at `order` (same
                // space as the built-in pass orders, e.g. LightTransparent=700,
                // TAA=1300, editor GUI=10000). Re-adding a name replaces the pass.
                // After 3 callback errors the pass stops executing until re-added.
                rg.set_function("add_pass", [](const std::string &name, uint32_t order, sol::protected_function fn) {
                    auto errorCount = std::make_shared<int>(0);
                    RegisterScriptRenderPass(name, order,
                                             [name, fn, errorCount](CommandBuffer *cmd)
                                             {
                                                 if (*errorCount >= kScriptRenderPassMaxErrors)
                                                     return;
                                                 auto result = fn(cmd);
                                                 if (!result.valid())
                                                 {
                                                     sol::error err = result;
                                                     (*errorCount)++;
                                                     Log::Error(PeFormat("[Lua] render_graph pass '%s' error (%d/%d)%s: %s",
                                                                         name.c_str(),
                                                                         *errorCount,
                                                                         kScriptRenderPassMaxErrors,
                                                                         *errorCount >= kScriptRenderPassMaxErrors ? " - pass disabled" : "",
                                                                         err.what()));
                                                 }
                                             });
                });

                rg.set_function("remove_pass", [](const std::string &name) {
                    UnregisterScriptRenderPass(name);
                });

                // render_graph.get_target(name) -> image ("viewport", "display",
                // "depthStencil", ...). Resolve inside the pass callback each frame;
                // render targets are recreated on resize, so never cache the result.
                rg.set_function("get_target", [](const std::string &name) -> std::shared_ptr<LuaImage> {
                    SceneRendererHost *host = GetActiveSceneRendererHost();
                    if (!host)
                        return nullptr;
                    Image *img = host->GetRenderTarget(name);
                    if (!img)
                        img = host->GetDepthStencilTarget(name);
                    if (!img)
                        return nullptr;
                    auto luaImg = std::make_shared<LuaImage>();
                    luaImg->ptr = img;
                    luaImg->owned = false;
                    luaImg->rtName = name;
                    return luaImg;
                }); });
        }
    } s_renderGraphBindings;
} // namespace pe
