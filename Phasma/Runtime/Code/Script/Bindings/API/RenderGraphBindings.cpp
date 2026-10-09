#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Command.h"
#include "API/Image.h"
#include "Render/FullscreenPasses.h"
#include "Render/SceneRendererHost.h"
#include "Render/ScriptRenderPasses.h"

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

                // render_graph.add_pass{name = "...", order = 550, reads = {"depthStencil", ...}, writes = {"viewport", ...},
                //                       fn = function(cmd) ... end}
                // fn(cmd) records into the frame's command buffer at order among the built-in passes (their sort
                // numbers; without one the pass runs after them). reads are render targets sampled in the fragment
                // stage (the graph issues their barriers), writes the targets the pass renders into; both are checked
                // against the pass's place. Re-adding a name replaces the pass. After 3 callback errors the pass stops
                // until re-added. The scene's pipeline script adds its passes from init().
                rg.set_function("add_pass", [](sol::variadic_args args) {
                    if (args.size() != 1 || !args[0].is<sol::table>())
                    {
                        Log::Error("[Lua] render_graph.add_pass takes one table: {name = ..., order = ..., reads = {...}, writes = {...}, fn = function(cmd) end}");
                        return false;
                    }
                    const sol::table desc = args[0].as<sol::table>();
                    const std::string name = desc.get_or<std::string>("name", "");
                    sol::protected_function fn = desc["fn"];
                    if (name.empty() || !fn.valid())
                    {
                        Log::Error("[Lua] render_graph.add_pass needs a name and fn");
                        return false;
                    }
                    const auto names = [&desc](const char *key)
                    {
                        std::vector<std::string> list;
                        if (const sol::optional<sol::table> entries = desc[key])
                            for (const auto &entry : *entries)
                                if (entry.second.is<std::string>())
                                    list.push_back(entry.second.as<std::string>());
                        return list;
                    };
                    auto errorCount = std::make_shared<int>(0);
                    RegisterScriptRenderPass(name, desc.get_or("order", kScriptRenderPassUnplacedOrder),
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
                                             },
                                             GetLuaRenderPassOwner(), names("reads"), names("writes"));
                    return true;
                });

                // render_graph.add_fullscreen_pass{name = "...", shader = "Shaders/...hlsl", order = 550,
                //                                  params = {a, b, c, d}, thickness_at_1080 = 1, min_thickness = 0}
                // A fullscreen shader pass recorded natively every frame (no Lua per frame): the shader's mainVS /
                // mainPS read the depth at binding 0 and the normals at binding 1 and blend onto the viewport; push
                // constants vec4(1/width, 1/height, thickness, camera near) and vec4 params (Render/FullscreenPasses.h).
                rg.set_function("add_fullscreen_pass", [](const sol::table &desc) {
                    const std::string name = desc.get_or<std::string>("name", "");
                    FullscreenPassDesc pass;
                    pass.shader = desc.get_or<std::string>("shader", "");
                    if (name.empty() || pass.shader.empty())
                    {
                        Log::Error("[Lua] render_graph.add_fullscreen_pass needs a name and a shader");
                        return false;
                    }
                    pass.order = desc.get_or("order", pass.order);
                    pass.thicknessAt1080 = desc.get_or("thickness_at_1080", pass.thicknessAt1080);
                    pass.minThickness = desc.get_or("min_thickness", pass.minThickness);
                    if (const sol::optional<sol::table> params = desc["params"])
                        for (int i = 0; i < 4; ++i)
                            pass.params[i] = params->get_or(i + 1, 0.0f);
                    AddFullscreenPass(name, pass, GetLuaRenderPassOwner());
                    return true;
                });

                rg.set_function("remove_pass", [](const std::string &name) {
                    if (!RemoveFullscreenPass(name))
                        UnregisterScriptRenderPass(name);
                });

                // render_graph.has_pass(name): registered by Lua or a C++ script.
                rg.set_function("has_pass", [](const std::string &name) { return FindScriptRenderPass(name) != nullptr; });

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
