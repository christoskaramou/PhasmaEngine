#ifdef PE_AUDIO

#include "Audio/AudioTypes.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneNodeHandle.h"
#include "Script/ScriptSystem.h"
#include "Systems/AudioSystem.h"

namespace pe
{
    static struct AudioBindings
    {
        AudioBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table audio = lua.create_named_table("audio");

                // Fire-and-forget
                audio.set_function("play", [](const std::string &path) {
                    if (auto *as = GetGlobalSystem<AudioSystem>())
                        as->PlaySound(path);
                });

                audio.set_function("play_3d", [](const std::string &path, float x, float y, float z) {
                    if (auto *as = GetGlobalSystem<AudioSystem>())
                        as->PlaySound3D(path, vec3(x, y, z));
                });

                // Music
                audio.set_function("play_music", [](const std::string &path) {
                    if (auto *as = GetGlobalSystem<AudioSystem>())
                        as->PlayMusic(path);
                });

                audio.set_function("stop_music", []() {
                    if (auto *as = GetGlobalSystem<AudioSystem>())
                        as->StopMusic();
                });

                // Volume
                audio.set_function("set_master_volume", [](float v) {
                    if (auto *as = GetGlobalSystem<AudioSystem>())
                        as->SetMasterVolume(v);
                });

                audio.set_function("set_music_volume", [](float v) {
                    if (auto *as = GetGlobalSystem<AudioSystem>())
                        as->SetMusicVolume(v);
                });

                audio.set_function("set_sfx_volume", [](float v) {
                    if (auto *as = GetGlobalSystem<AudioSystem>())
                        as->SetSFXVolume(v);
                });

                // Per-node sources
                audio.set_function("add_source", [](SceneNodeHandle &h, const std::string &path, sol::optional<sol::table> params) {
                    auto *as = GetGlobalSystem<AudioSystem>();
                    Scene *scene = GetActiveScene();
                    if (!as || !scene || !h.nodeId)
                        return;

                    AudioSourceDesc desc;
                    desc.filePath = path;
                    if (params)
                    {
                        sol::table p = *params;
                        if (p["volume"].valid()) desc.volume = p["volume"];
                        if (p["pitch"].valid()) desc.pitch = p["pitch"];
                        if (p["loop"].valid()) desc.loop = p["loop"];
                        if (p["spatial"].valid()) desc.spatial = p["spatial"];
                        if (p["autoplay"].valid()) desc.autoplay = p["autoplay"];
                        if (p["min_distance"].valid()) desc.minDistance = p["min_distance"];
                        if (p["max_distance"].valid()) desc.maxDistance = p["max_distance"];
                    }
                    as->AddSource(*scene, h.nodeId, desc);
                });

                audio.set_function("remove_source", [](SceneNodeHandle &h) {
                    if (auto *as = GetGlobalSystem<AudioSystem>())
                        if (h.nodeId)
                            as->RemoveSource(h.nodeId);
                });

                audio.set_function("has_source", [](SceneNodeHandle &h) -> bool {
                    auto *as = GetGlobalSystem<AudioSystem>();
                    return as && h.nodeId && as->HasSource(h.nodeId);
                });

                audio.set_function("play_source", [](SceneNodeHandle &h) {
                    if (auto *as = GetGlobalSystem<AudioSystem>())
                        if (h.nodeId)
                            as->PlaySource(h.nodeId);
                });

                audio.set_function("stop_source", [](SceneNodeHandle &h) {
                    if (auto *as = GetGlobalSystem<AudioSystem>())
                        if (h.nodeId)
                            as->StopSource(h.nodeId);
                }); });
        }
    } s_audioBindings;
} // namespace pe

#endif // PE_AUDIO
