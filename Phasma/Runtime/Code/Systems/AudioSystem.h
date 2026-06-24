#pragma once

#ifdef PE_AUDIO

#include "Audio/AudioTypes.h"

// Forward-declare miniaudio types
struct ma_engine;
struct ma_sound;

namespace pe
{
    struct NodeId;
    class Scene;

    struct AudioNodeState
    {
        NodeId *nodeId = nullptr;
        AudioSourceDesc desc;
        ma_sound *sound = nullptr;
        bool playing = false;
    };

    class AudioSystem : public ISystem
    {
    public:
        ~AudioSystem() override;

        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;

        // Fire-and-forget
        void PlaySound(const std::string &path);
        void PlaySound3D(const std::string &path, const vec3 &pos);

        // Music (single looping track)
        void PlayMusic(const std::string &path);
        void StopMusic();

        // Volume
        void SetMasterVolume(float v);
        void SetMusicVolume(float v);
        void SetSFXVolume(float v);
        float GetMasterVolume() const { return m_masterVolume; }
        float GetMusicVolume() const { return m_musicVolume; }
        float GetSFXVolume() const { return m_sfxVolume; }
        // Trigger-zone audio: drive a zone's OWN source (its NodeTriggerZoneTag::audioSource, separate
        // from the node's Component_Audio) by a 0..1 gain from the zone's distance blend. gain > 0 ->
        // ensure loaded + playing + scale volume; gain == 0 -> stop. Reloads if the file path changes.
        void SetZoneAudio(NodeId *node, const AudioSourceDesc &desc, float gain);

        // Per-node audio sources
        void AddSource(Scene &scene, NodeId *node, const AudioSourceDesc &desc);
        void RemoveSource(NodeId *node);
        AudioSourceDesc *GetSourceDesc(NodeId *node);
        const AudioSourceDesc *GetSourceDesc(const NodeId *node) const;
        bool HasSource(const NodeId *node) const;
        void ClearAllSources();

        // Source playback control
        void PlaySource(NodeId *node);
        void StopSource(NodeId *node);
        void PauseSource(NodeId *node);

        // Play mode hooks
        void StartPlayMode(Scene &scene);
        void StopPlayMode();

    private:
        void CleanupFinishedSounds();
        void SyncListenerToCamera();
        void SyncSourcePositions(Scene &scene);
        std::string ResolvePath(const std::string &path) const;

        ma_engine *m_engine = nullptr;
        ma_sound *m_musicSound = nullptr;

        std::unordered_map<const NodeId *, size_t> m_nodeToIndex;
        std::vector<AudioNodeState> m_states;

        // Trigger-zone owned sounds (separate from per-node Component_Audio sources above), keyed by
        // the zone node. path tracks the loaded file so we reload when the zone's audioSource changes.
        struct ZoneSound
        {
            ma_sound *sound = nullptr;
            std::string path;
        };
        std::unordered_map<const NodeId *, ZoneSound> m_zoneSounds;

        // Fire-and-forget sounds (cleaned up when finished)
        std::vector<ma_sound *> m_fireAndForget;

        float m_masterVolume = 1.0f;
        float m_musicVolume = 1.0f;
        float m_sfxVolume = 1.0f;
    };
} // namespace pe

#endif // PE_AUDIO
