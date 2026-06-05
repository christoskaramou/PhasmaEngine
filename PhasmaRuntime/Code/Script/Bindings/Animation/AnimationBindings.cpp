#include "Scene/SceneNodeHandle.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Script/ScriptSystem.h"
#include "Systems/AnimationSystem.h"

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
                    Scene *scene = GetActiveScene();
                    if (!as || !scene || !h.IsValid(*scene))
                        return;
                    bool l = loop.value_or(true);
                    if (clipArg.is<int>())
                        as->PlayAnimation(*scene, h.nodeId, clipArg.as<int>() - 1, l); // Lua 1-based -> C++ 0-based
                    else if (clipArg.is<std::string>())
                        as->PlayAnimation(*scene, h.nodeId, clipArg.as<std::string>(), l);
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

                auto makeClipTable = [](sol::this_state ts, const std::vector<AnimationClip> &clips) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table t = lua.create_table();
                    for (int i = 0; i < static_cast<int>(clips.size()); i++)
                        t[i + 1] = clips[i].name;
                    return t;
                };

                anim.set_function("get_clips", sol::overload(
                    [makeClipTable](sol::this_state ts) -> sol::table {
                        static const std::vector<AnimationClip> empty;
                        Scene *scene = GetActiveScene();
                        if (!scene)
                            return makeClipTable(ts, empty);
                        return makeClipTable(ts, scene->GetAnimationClips());
                    },
                    [makeClipTable](SceneNodeHandle &h, sol::this_state ts) -> sol::table {
                        static const std::vector<AnimationClip> empty;
                        Scene *scene = GetActiveScene();
                        if (!scene || !h.IsValid(*scene))
                            return makeClipTable(ts, empty);
                        return makeClipTable(ts, scene->GetAnimationClipsForNode(h.nodeId));
                    }));

                anim.set_function("get_joint_count", sol::overload(
                    []() -> int {
                        Scene *scene = GetActiveScene();
                        return scene ? scene->GetMaxJointCount() : 0;
                    },
                    [](SceneNodeHandle &h) -> int {
                        Scene *scene = GetActiveScene();
                        return scene && h.IsValid(*scene) ? scene->GetJointCountForNode(h.nodeId) : 0;
                    }));

                anim.set_function("set_joint_rotations_z", [](SceneNodeHandle &h, sol::table rotations) -> bool {
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    Scene *scene = GetActiveScene();
                    if (!as || !scene || !h.IsValid(*scene))
                        return false;

                    std::vector<float> values;
                    values.reserve(rotations.size());
                    for (size_t i = 1; i <= rotations.size(); i++)
                    {
                        sol::object value = rotations.get<sol::object>(static_cast<int>(i));
                        if (value.is<double>())
                            values.push_back(static_cast<float>(value.as<double>()));
                        else if (value.is<int>())
                            values.push_back(static_cast<float>(value.as<int>()));
                        else
                            values.push_back(0.0f);
                    }

                    return as->SetJointLocalRotationsZ(*scene, h.nodeId, values);
                }); });
        }
    } s_animationBindings;
} // namespace pe
