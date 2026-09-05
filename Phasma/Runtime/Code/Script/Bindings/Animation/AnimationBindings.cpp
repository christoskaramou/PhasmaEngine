#include "Scene/SceneNodeHandle.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Script/ScriptSystem.h"
#include "Systems/AnimationSystem.h"

namespace pe
{
    static std::vector<float> ReadFloatTable(const sol::table &table, float fallback)
    {
        std::vector<float> values;
        const size_t valueCount = table.size();
        values.reserve(valueCount);
        for (size_t i = 1; i <= valueCount; i++)
        {
            sol::object value = table.get<sol::object>(static_cast<int>(i));
            if (value.is<double>())
                values.push_back(static_cast<float>(value.as<double>()));
            else if (value.is<int>())
                values.push_back(static_cast<float>(value.as<int>()));
            else
                values.push_back(fallback);
        }
        return values;
    }

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
                    Scene *scene = GetActiveScene();
                    if (as && scene && h.IsValid(*scene))
                        as->StopAnimation(h.nodeId);
                });

                anim.set_function("set_speed", [](SceneNodeHandle &h, float speed) {
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    Scene *scene = GetActiveScene();
                    if (as && scene && h.IsValid(*scene))
                        as->SetSpeed(h.nodeId, speed);
                });

                anim.set_function("play_layer", [](SceneNodeHandle &h, const std::string &clip,
                                                     sol::table mask, sol::optional<bool> loop,
                                                     sol::optional<float> speed) -> bool {
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    Scene *scene = GetActiveScene();
                    if (!as || !scene || !h.IsValid(*scene) || mask.size() > static_cast<size_t>(scene->GetJointCountForNode(h.nodeId)))
                        return false;
                    std::vector<std::string> bones;
                    for (size_t i = 1; i <= mask.size(); ++i)
                    {
                        sol::object name = mask.get<sol::object>(i);
                        if (!name.is<std::string>())
                            return false;
                        bones.push_back(name.as<std::string>());
                    }
                    return as->PlayLayer(*scene, h.nodeId, clip, bones, loop.value_or(true), speed.value_or(1.f));
                });

                anim.set_function("set_layer_speed", [](SceneNodeHandle &h, float speed) -> bool {
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    Scene *scene = GetActiveScene();
                    return as && scene && h.IsValid(*scene) && as->SetLayerSpeed(h.nodeId, speed);
                });

                anim.set_function("stop_layer", [](SceneNodeHandle &h) {
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    Scene *scene = GetActiveScene();
                    if (as && scene && h.IsValid(*scene))
                        as->StopLayer(*scene, h.nodeId);
                });

                anim.set_function("get_layer_state", [](SceneNodeHandle &h, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table result = lua.create_table();
                    result["active"] = false;
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    Scene *scene = GetActiveScene();
                    if (!as || !scene || !h.IsValid(*scene))
                        return result;
                    const auto *state = as->GetAnimationState(h.nodeId);
                    const auto &clips = scene->GetAnimationClipsForNode(h.nodeId);
                    if (!state || state->layer.clipIndex < 0 || state->layer.clipIndex >= static_cast<int>(clips.size()))
                        return result;
                    const auto &layer = state->layer;
                    const auto &clip = clips[layer.clipIndex];
                    result["active"] = true;
                    result["clip"] = clip.name;
                    result["time"] = layer.time / clip.ticksPerSecond;
                    result["elapsed"] = layer.elapsed;
                    result["duration"] = clip.duration / clip.ticksPerSecond;
                    result["speed"] = layer.speed;
                    result["loop"] = layer.loop;
                    result["playing"] = state->playing && layer.speed != 0.f &&
                        (layer.loop || (layer.speed > 0.f ? layer.time < clip.duration : layer.time > 0.f));
                    return result;
                });

                // Root motion: a clip whose travel the Animator extracted moves this node as it plays (default on).
                anim.set_function("set_root_motion", [](SceneNodeHandle &h, bool enabled) {
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    Scene *scene = GetActiveScene();
                    if (as && scene && h.IsValid(*scene))
                        as->SetRootMotion(h.nodeId, enabled);
                });

                anim.set_function("get_root_motion", [](SceneNodeHandle &h) -> bool {
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    Scene *scene = GetActiveScene();
                    return as && scene && h.IsValid(*scene) && as->GetRootMotion(h.nodeId);
                });

                anim.set_function("is_playing", [](SceneNodeHandle &h) -> bool {
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    Scene *scene = GetActiveScene();
                    return as && scene && h.IsValid(*scene) && as->IsPlaying(h.nodeId);
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

                // Ruler markers of a clip (1-based index or name; none = the clip playing) as {name, time} with time
                // in seconds, and the playhead in seconds, so a script fires footsteps or hits on them.
                anim.set_function("get_markers", [](SceneNodeHandle &h, sol::optional<sol::object> clipArg, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table t = lua.create_table();
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    Scene *scene = GetActiveScene();
                    if (!scene || !h.IsValid(*scene))
                        return t;
                    const auto &clips = scene->GetAnimationClipsForNode(h.nodeId);
                    int index = as ? as->GetCurrentClip(h.nodeId) : -1;
                    if (clipArg && clipArg->is<int>())
                        index = clipArg->as<int>() - 1;
                    else if (clipArg && clipArg->is<std::string>())
                    {
                        const std::string name = clipArg->as<std::string>();
                        index = -1;
                        for (int i = 0; i < static_cast<int>(clips.size()); i++)
                            if (clips[i].name == name)
                                index = i;
                    }
                    if (index < 0 || index >= static_cast<int>(clips.size()))
                        return t;
                    const AnimationClip &clip = clips[index];
                    const float ticksPerSecond = clip.ticksPerSecond > 0.f ? clip.ticksPerSecond : 25.f;
                    int n = 0;
                    for (const ClipMarker &marker : clip.markers)
                    {
                        sol::table entry = lua.create_table();
                        entry["name"] = marker.name;
                        entry["time"] = marker.time / ticksPerSecond;
                        t[++n] = entry;
                    }
                    return t;
                });

                anim.set_function("get_time", [](SceneNodeHandle &h) -> float {
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    Scene *scene = GetActiveScene();
                    if (!as || !scene || !h.IsValid(*scene))
                        return 0.f;
                    const int index = as->GetCurrentClip(h.nodeId);
                    const auto &clips = scene->GetAnimationClipsForNode(h.nodeId);
                    if (index < 0 || index >= static_cast<int>(clips.size()) || clips[index].ticksPerSecond <= 0.f)
                        return 0.f;
                    return as->GetPlaybackTime(h.nodeId) / clips[index].ticksPerSecond;
                });

                anim.set_function("get_joint_count", sol::overload(
                    []() -> int {
                        Scene *scene = GetActiveScene();
                        return scene ? scene->GetMaxJointCount() : 0;
                    },
                    [](SceneNodeHandle &h) -> int {
                        Scene *scene = GetActiveScene();
                        return scene && h.IsValid(*scene) ? scene->GetJointCountForNode(h.nodeId) : 0;
                    }));

                anim.set_function("set_joint_rotations_z", [](SceneNodeHandle &h,
                                                               sol::table rotations,
                                                               sol::optional<float> stretchScale,
                                                               sol::optional<sol::table> widthScales) -> bool {
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    Scene *scene = GetActiveScene();
                    if (!as || !scene || !h.IsValid(*scene))
                        return false;

                    std::vector<float> values = ReadFloatTable(rotations, 0.0f);
                    std::vector<float> widths = widthScales ? ReadFloatTable(*widthScales, 1.0f) : std::vector<float>{};

                    return as->SetJointLocalRotationsZ(*scene,
                                                       h.nodeId,
                                                       values,
                                                       stretchScale.value_or(1.0f),
                                                       widthScales ? &widths : nullptr);
                });

                anim.set_function("solve_strip_ik_2d", [](SceneNodeHandle &h,
                                                           const vec2 &targetLocal,
                                                           sol::optional<int> iterations,
                                                           sol::optional<float> maxBendDegrees,
                                                           sol::optional<float> maxStretchScale,
                                                           sol::optional<sol::table> jointInfluences,
                                                           sol::optional<sol::table> widthScales) -> bool {
                    auto *as = GetGlobalSystem<AnimationSystem>();
                    Scene *scene = GetActiveScene();
                    if (!as || !scene || !h.IsValid(*scene))
                        return false;

                    std::vector<float> influences = jointInfluences ? ReadFloatTable(*jointInfluences, 1.0f) : std::vector<float>{};
                    std::vector<float> widths = widthScales ? ReadFloatTable(*widthScales, 1.0f) : std::vector<float>{};

                    return as->SolveStripIk2D(*scene,
                                              h.nodeId,
                                              targetLocal,
                                              iterations.value_or(8),
                                              nullptr,
                                              glm::radians(maxBendDegrees.value_or(60.0f)),
                                              0.0f,
                                              maxStretchScale.value_or(1.5f),
                                              nullptr,
                                              jointInfluences ? &influences : nullptr,
                                              widthScales ? &widths : nullptr);
                }); });
        }
    } s_animationBindings;
} // namespace pe
