#ifdef PE_AUDIO

#include "AudioSystem.h"
#include "Camera/Camera.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Systems/RendererSystem.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

namespace pe
{
    AudioSystem::~AudioSystem()
    {
        Destroy();
    }

    void AudioSystem::Init(CommandBuffer *)
    {
        m_engine = new ma_engine();
        ma_engine_config config = ma_engine_config_init();
        config.listenerCount = 1;

        ma_result result = ma_engine_init(&config, m_engine);
        if (result != MA_SUCCESS)
        {
            PE_ERROR("[Audio] Failed to initialize audio engine: %d", result);
            delete m_engine;
            m_engine = nullptr;
            return;
        }

        SetEnabled(true);
        PE_INFO("[Audio] Audio engine initialized");
    }

    void AudioSystem::Update()
    {
        if (!m_engine)
            return;

        CleanupFinishedSounds();
        SyncListenerToCamera();

        auto *rs = GetGlobalSystem<RendererSystem>();
        if (rs)
            SyncSourcePositions(rs->GetScene());
    }

    void AudioSystem::Destroy()
    {
        if (!m_engine)
            return;

        StopPlayMode();
        ClearAllSources();

        if (m_musicSound)
        {
            ma_sound_uninit(m_musicSound);
            delete m_musicSound;
            m_musicSound = nullptr;
        }

        for (auto *s : m_fireAndForget)
        {
            ma_sound_uninit(s);
            delete s;
        }
        m_fireAndForget.clear();

        ma_engine_uninit(m_engine);
        delete m_engine;
        m_engine = nullptr;
    }

    // --- Fire-and-forget ---

    std::string AudioSystem::ResolvePath(const std::string &path) const
    {
        if (std::filesystem::path(path).is_absolute())
            return path;
        return Path::Assets + "Audio/" + path;
    }

    void AudioSystem::PlaySound(const std::string &path)
    {
        if (!m_engine)
            return;

        std::string fullPath = ResolvePath(path);
        auto *sound = new ma_sound();
        if (ma_sound_init_from_file(m_engine, fullPath.c_str(), 0, nullptr, nullptr, sound) != MA_SUCCESS)
        {
            PE_WARN("[Audio] Failed to load: %s", fullPath.c_str());
            delete sound;
            return;
        }
        ma_sound_set_volume(sound, m_sfxVolume);
        ma_sound_start(sound);
        m_fireAndForget.push_back(sound);
    }

    void AudioSystem::PlaySound3D(const std::string &path, const vec3 &pos)
    {
        if (!m_engine)
            return;

        std::string fullPath = ResolvePath(path);
        auto *sound = new ma_sound();
        if (ma_sound_init_from_file(m_engine, fullPath.c_str(), 0, nullptr, nullptr, sound) != MA_SUCCESS)
        {
            PE_WARN("[Audio] Failed to load: %s", fullPath.c_str());
            delete sound;
            return;
        }
        ma_sound_set_volume(sound, m_sfxVolume);
        ma_sound_set_spatialization_enabled(sound, MA_TRUE);
        ma_sound_set_position(sound, pos.x, pos.y, pos.z);
        ma_sound_start(sound);
        m_fireAndForget.push_back(sound);
    }

    // --- Music ---

    void AudioSystem::PlayMusic(const std::string &path)
    {
        if (!m_engine)
            return;

        StopMusic();

        std::string fullPath = ResolvePath(path);
        m_musicSound = new ma_sound();
        if (ma_sound_init_from_file(m_engine, fullPath.c_str(), 0, nullptr, nullptr, m_musicSound) != MA_SUCCESS)
        {
            PE_WARN("[Audio] Failed to load music: %s", fullPath.c_str());
            delete m_musicSound;
            m_musicSound = nullptr;
            return;
        }
        ma_sound_set_looping(m_musicSound, MA_TRUE);
        ma_sound_set_volume(m_musicSound, m_musicVolume);
        ma_sound_set_spatialization_enabled(m_musicSound, MA_FALSE);
        ma_sound_start(m_musicSound);
    }

    void AudioSystem::StopMusic()
    {
        if (m_musicSound)
        {
            ma_sound_uninit(m_musicSound);
            delete m_musicSound;
            m_musicSound = nullptr;
        }
    }

    // --- Volume ---

    void AudioSystem::SetMasterVolume(float v)
    {
        m_masterVolume = v;
        if (m_engine)
            ma_engine_set_volume(m_engine, v);
    }

    void AudioSystem::SetMusicVolume(float v)
    {
        m_musicVolume = v;
        if (m_musicSound)
            ma_sound_set_volume(m_musicSound, v);
    }

    void AudioSystem::SetSFXVolume(float v)
    {
        m_sfxVolume = v;
    }

    // --- Per-node audio sources ---

    void AudioSystem::AddSource(Scene &scene, NodeId *node, const AudioSourceDesc &desc)
    {
        if (m_nodeToIndex.count(node))
            return;

        scene.AddComponentFlag(node, Component_Audio);

        AudioNodeState state;
        state.nodeId = node;
        state.desc = desc;

        size_t idx = m_states.size();
        m_states.push_back(std::move(state));
        m_nodeToIndex[node] = idx;
    }

    void AudioSystem::RemoveSource(NodeId *node)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end())
            return;

        size_t idx = it->second;

        // Stop and clean up the sound if playing
        if (m_states[idx].sound)
        {
            ma_sound_uninit(m_states[idx].sound);
            delete m_states[idx].sound;
        }

        if (auto *rs = GetGlobalSystem<RendererSystem>())
            rs->GetScene().RemoveComponentFlag(node, Component_Audio);

        // Swap-and-pop
        if (idx < m_states.size() - 1)
        {
            m_states[idx] = std::move(m_states.back());
            m_nodeToIndex[m_states[idx].nodeId] = idx;
        }
        m_states.pop_back();
        m_nodeToIndex.erase(it);
    }

    AudioSourceDesc *AudioSystem::GetSourceDesc(NodeId *node)
    {
        auto it = m_nodeToIndex.find(node);
        return (it != m_nodeToIndex.end()) ? &m_states[it->second].desc : nullptr;
    }

    const AudioSourceDesc *AudioSystem::GetSourceDesc(const NodeId *node) const
    {
        auto it = m_nodeToIndex.find(node);
        return (it != m_nodeToIndex.end()) ? &m_states[it->second].desc : nullptr;
    }

    bool AudioSystem::HasSource(const NodeId *node) const
    {
        return m_nodeToIndex.count(node) > 0;
    }

    void AudioSystem::ClearAllSources()
    {
        for (auto &state : m_states)
        {
            if (state.sound)
            {
                ma_sound_uninit(state.sound);
                delete state.sound;
                state.sound = nullptr;
            }
        }
        m_states.clear();
        m_nodeToIndex.clear();
    }

    // --- Source playback ---

    void AudioSystem::PlaySource(NodeId *node)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || !m_engine)
            return;

        auto &state = m_states[it->second];
        const auto &desc = state.desc;

        // Create sound if not yet created
        if (!state.sound)
        {
            std::string fullPath = ResolvePath(desc.filePath);
            state.sound = new ma_sound();
            if (ma_sound_init_from_file(m_engine, fullPath.c_str(), 0, nullptr, nullptr, state.sound) != MA_SUCCESS)
            {
                PE_WARN("[Audio] Failed to load: %s", fullPath.c_str());
                delete state.sound;
                state.sound = nullptr;
                return;
            }
        }

        ma_sound_set_volume(state.sound, desc.volume * m_sfxVolume);
        ma_sound_set_pitch(state.sound, desc.pitch);
        ma_sound_set_looping(state.sound, desc.loop ? MA_TRUE : MA_FALSE);
        ma_sound_set_spatialization_enabled(state.sound, desc.spatial ? MA_TRUE : MA_FALSE);
        if (desc.spatial)
        {
            ma_sound_set_min_distance(state.sound, desc.minDistance);
            ma_sound_set_max_distance(state.sound, desc.maxDistance);
        }
        ma_sound_start(state.sound);
        state.playing = true;
    }

    void AudioSystem::StopSource(NodeId *node)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end())
            return;

        auto &state = m_states[it->second];
        if (state.sound)
        {
            ma_sound_stop(state.sound);
            ma_sound_seek_to_pcm_frame(state.sound, 0);
        }
        state.playing = false;
    }

    void AudioSystem::PauseSource(NodeId *node)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end())
            return;

        auto &state = m_states[it->second];
        if (state.sound)
            ma_sound_stop(state.sound);
        state.playing = false;
    }

    // --- Play mode ---

    void AudioSystem::StartPlayMode(Scene &scene)
    {
        for (auto &state : m_states)
        {
            if (state.desc.autoplay)
                PlaySource(state.nodeId);
        }
    }

    void AudioSystem::StopPlayMode()
    {
        for (auto &state : m_states)
        {
            if (state.sound)
            {
                ma_sound_stop(state.sound);
                ma_sound_uninit(state.sound);
                delete state.sound;
                state.sound = nullptr;
            }
            state.playing = false;
        }
    }

    // --- Internal ---

    void AudioSystem::CleanupFinishedSounds()
    {
        auto it = m_fireAndForget.begin();
        while (it != m_fireAndForget.end())
        {
            if (ma_sound_at_end(*it))
            {
                ma_sound_uninit(*it);
                delete *it;
                it = m_fireAndForget.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void AudioSystem::SyncListenerToCamera()
    {
        auto *rs = GetGlobalSystem<RendererSystem>();
        if (!rs || !m_engine)
            return;

        Camera *cam = rs->GetScene().GetActiveCamera();
        if (!cam)
            return;

        vec3 pos = cam->GetPosition();
        vec3 fwd = cam->GetFront();
        vec3 up = cam->GetUp();

        // Negate direction to match miniaudio's handedness convention —
        // without this, left/right spatial sounds are swapped
        ma_engine_listener_set_position(m_engine, 0, pos.x, pos.y, pos.z);
        ma_engine_listener_set_direction(m_engine, 0, -fwd.x, -fwd.y, -fwd.z);
        ma_engine_listener_set_world_up(m_engine, 0, up.x, up.y, up.z);
    }

    void AudioSystem::SyncSourcePositions(Scene &scene)
    {
        for (auto &state : m_states)
        {
            if (!state.sound || !state.playing || !state.desc.spatial)
                continue;

            mat4 world = scene.GetWorldMatrix(state.nodeId);
            vec3 pos = vec3(world[3]);
            ma_sound_set_position(state.sound, pos.x, pos.y, pos.z);
        }
    }
} // namespace pe

#endif // PE_AUDIO
