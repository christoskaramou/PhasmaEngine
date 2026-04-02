#include "Scene/SceneNodeHandle.h"
#include "Script/ScriptSystem.h"
#include "Systems/AnimationSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    static struct AnimationBindings
    {
        AnimationBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table anim = lua.create_named_table("animation");

                anim.set_function("play", [](SceneNodeHandle &h, sol::object clipArg, sol::optional<bool> loop) {
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    auto *rs = GetGlobalSystem<RendererSystem>();
                    if (!as || !rs || !h.nodeId)
                        return;
                    bool l = loop.value_or(true);
                    if (clipArg.is<int>())
                        as->PlayAnimation(rs->GetScene(), h.nodeId, clipArg.as<int>() - 1, l); // Lua 1-based → C++ 0-based
                    else if (clipArg.is<std::string>())
                        as->PlayAnimation(rs->GetScene(), h.nodeId, clipArg.as<std::string>(), l);
                });

                anim.set_function("stop", [](SceneNodeHandle &h) {
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    if (as && h.nodeId)
                        as->StopAnimation(h.nodeId);
                });

                anim.set_function("set_speed", [](SceneNodeHandle &h, float speed) {
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    if (as && h.nodeId)
                        as->SetSpeed(h.nodeId, speed);
                });

                anim.set_function("is_playing", [](SceneNodeHandle &h) -> bool {
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    return as && h.nodeId && as->IsPlaying(h.nodeId);
                });

                anim.set_function("get_clips", [](sol::this_state ts) -> sol::table {
                    auto *rs = GetGlobalSystem<RendererSystem>();
                    sol::state_view lua(ts);
                    sol::table t = lua.create_table();
                    if (!rs)
                        return t;
                    const auto &clips = rs->GetScene().GetAnimationClips();
                    for (int i = 0; i < static_cast<int>(clips.size()); i++)
                        t[i + 1] = clips[i].name;
                    return t;
                }); });
        }
    } s_animationBindings;
} // namespace pe
