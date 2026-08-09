#include "Script/ScriptSystem.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneNodeHandle.h"

namespace pe
{
    namespace
    {
        Scene *GetScene()
        {
            return GetActiveScene();
        }

        NodeSpriteComponent *GetSprite(Scene *scene, SceneNodeHandle &handle)
        {
            if (!scene || !handle.IsValid(*scene))
                return nullptr;
            return scene->GetSpriteComponent(handle.nodeId);
        }

        float OptFloat(const sol::table &t, const char *key, float fallback)
        {
            sol::optional<float> v = t[key];
            return v ? *v : fallback;
        }

        std::string OptString(const sol::table &t, const char *key, const std::string &fallback = {})
        {
            sol::optional<std::string> v = t[key];
            return v ? *v : fallback;
        }

    } // namespace

    static struct SpriteBindings
    {
        SpriteBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table sprite = lua.create_named_table("sprite");

                sprite.set_function("create", [](sol::table opts, sol::this_state ts) -> sol::object {
                    sol::state_view luaView(ts);
                    Scene *scene = GetScene();
                    if (!scene)
                        return sol::make_object(luaView, sol::nil);

                    const std::string metadata = OptString(opts, "metadata");
                    if (metadata.empty())
                        return sol::make_object(luaView, sol::nil);

                    NodeId *parent = nullptr;
                    sol::optional<SceneNodeHandle> parentHandle = opts["parent"];
                    if (parentHandle && parentHandle->IsValid(*scene))
                        parent = parentHandle->nodeId;

                    const std::string name = OptString(opts, "name", "Sprite");
                    const float qw = OptFloat(opts, "quad_width", OptFloat(opts, "width", 1.6f));
                    const float qh = OptFloat(opts, "quad_height", OptFloat(opts, "height", 2.2f));

                    std::string error;
                    NodeId *node = scene->CreateSpriteNode(name, parent, metadata, qw, qh, &error);
                    if (!node)
                        return sol::make_object(luaView, sol::nil);
                    return sol::make_object(luaView, scene->MakeHandle(node));
                });

                sprite.set_function("setup", [](SceneNodeHandle &h, sol::object metadataOrOpts) -> bool {
                    Scene *scene = GetScene();
                    if (!scene || !h.IsValid(*scene))
                        return false;

                    std::string metadata;
                    int meshSlot = 0;
                    float whiten = 0.0f;
                    if (metadataOrOpts.is<std::string>())
                    {
                        metadata = metadataOrOpts.as<std::string>();
                    }
                    else if (metadataOrOpts.is<sol::table>())
                    {
                        sol::table opts = metadataOrOpts.as<sol::table>();
                        metadata = OptString(opts, "metadata");
                        sol::optional<int> slot = opts["mesh_slot"];
                        if (slot)
                            meshSlot = *slot;
                        whiten = OptFloat(opts, "whiten", 0.0f);
                    }
                    if (metadata.empty())
                        return false;

                    NodeSpriteComponent &component = scene->GetOrCreateSpriteComponent(h.nodeId);
                    component.whiten = std::isfinite(whiten) ? std::clamp(whiten, 0.0f, 1.0f) : 0.0f;
                    std::string error;
                    return scene->SetupSpriteFromMetadata(h.nodeId, metadata, meshSlot, &error);
                });

                sprite.set_function("reload", [](SceneNodeHandle &h) -> bool {
                    Scene *scene = GetScene();
                    return scene && h.IsValid(*scene) && scene->LoadSpriteMetadata(h.nodeId);
                });

                sprite.set_function("play", [](SceneNodeHandle &h, sol::optional<std::string> clipName, sol::optional<bool> restart) -> bool {
                    Scene *scene = GetScene();
                    if (!scene || !h.IsValid(*scene))
                        return false;
                    return scene->PlaySpriteClip(h.nodeId, clipName.value_or(""), restart.value_or(true));
                });

                sprite.set_function("pause", [](SceneNodeHandle &h, sol::optional<bool> paused) {
                    Scene *scene = GetScene();
                    if (!scene || !h.IsValid(*scene))
                        return;
                    scene->SetSpritePlaying(h.nodeId, !paused.value_or(true));
                });

                sprite.set_function("stop", [](SceneNodeHandle &h) {
                    Scene *scene = GetScene();
                    if (scene && h.IsValid(*scene))
                        scene->StopSprite(h.nodeId);
                });

                sprite.set_function("set_frame", sol::overload(
                    [](SceneNodeHandle &h, int frameIndex) -> bool {
                        Scene *scene = GetScene();
                        return scene && h.IsValid(*scene) && scene->SetSpriteFrame(h.nodeId, frameIndex - 1);
                    },
                    [](SceneNodeHandle &h, const std::string &frameName) -> bool {
                        Scene *scene = GetScene();
                        return scene && h.IsValid(*scene) && scene->SetSpriteFrame(h.nodeId, frameName);
                    }));

                sprite.set_function("is_playing", [](SceneNodeHandle &h) -> bool {
                    Scene *scene = GetScene();
                    const NodeSpriteComponent *component = GetSprite(scene, h);
                    return component && component->playing;
                });

                sprite.set_function("set_speed", [](SceneNodeHandle &h, float speed) {
                    Scene *scene = GetScene();
                    NodeSpriteComponent *component = GetSprite(scene, h);
                    if (component)
                        component->playbackSpeed = std::max(0.0f, speed);
                });

                sprite.set_function("set_loop", [](SceneNodeHandle &h, bool loop) {
                    Scene *scene = GetScene();
                    NodeSpriteComponent *component = GetSprite(scene, h);
                    if (component)
                        component->loop = loop;
                });

                sprite.set_function("set_interpolate", [](SceneNodeHandle &h, bool interpolate) {
                    Scene *scene = GetScene();
                    if (scene && h.IsValid(*scene))
                        scene->SetSpriteInterpolation(h.nodeId, interpolate);
                });

                sprite.set_function("get_interpolate", [](SceneNodeHandle &h) -> bool {
                    Scene *scene = GetScene();
                    const NodeSpriteComponent *component = GetSprite(scene, h);
                    return component && component->interpolate;
                });

                sprite.set_function("set_outline_color", [](SceneNodeHandle &h, const vec4 &color) -> bool {
                    Scene *scene = GetScene();
                    return scene && h.IsValid(*scene) && scene->SetSpriteOutlineColor(h.nodeId, color);
                });

                sprite.set_function("get_frame", [](SceneNodeHandle &h) -> int {
                    Scene *scene = GetScene();
                    const NodeSpriteComponent *component = GetSprite(scene, h);
                    return component ? component->frameIndex + 1 : 0;
                });

                sprite.set_function("get_frame_name", [](SceneNodeHandle &h) -> std::string {
                    Scene *scene = GetScene();
                    const NodeSpriteComponent *component = GetSprite(scene, h);
                    return component ? component->frameName : "";
                });

                sprite.set_function("get_clips", [](SceneNodeHandle &h, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table result = lua.create_table();
                    Scene *scene = GetScene();
                    NodeSpriteComponent *component = GetSprite(scene, h);
                    if (!scene || !component)
                        return result;
                    if ((component->clips.empty() || !component->metadataLoaded) && !component->metadataPath.empty())
                        scene->LoadSpriteMetadata(h.nodeId);
                    for (int i = 0; i < static_cast<int>(component->clips.size()); ++i)
                        result[i + 1] = component->clips[i].name;
                    return result;
                });

                sprite.set_function("get_frames", [](SceneNodeHandle &h, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table result = lua.create_table();
                    Scene *scene = GetScene();
                    NodeSpriteComponent *component = GetSprite(scene, h);
                    if (!scene || !component)
                        return result;
                    if ((component->frames.empty() || !component->metadataLoaded) && !component->metadataPath.empty())
                        scene->LoadSpriteMetadata(h.nodeId);
                    for (int i = 0; i < static_cast<int>(component->frames.size()); ++i)
                        result[i + 1] = component->frames[i].name;
                    return result;
                }); });
        }
    } s_spriteBindings;
} // namespace pe
