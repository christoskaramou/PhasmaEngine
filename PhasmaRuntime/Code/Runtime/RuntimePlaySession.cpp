#include "Runtime/RuntimePlaySession.h"
#include "Scene/Scene.h"
#include "Script/ScriptSystem.h"
#include "Systems/AnimationSystem.h"
#include "Systems/AudioSystem.h"
#include "Systems/PhysicsSystem.h"

namespace pe
{
    void StartRuntimePlaySession(Scene &scene, const RuntimePlaySessionStartDesc &desc)
    {
        if (desc.callScriptInit)
        {
            if (auto *scripts = GetGlobalSystem<ScriptSystem>())
                scripts->CallInit();
        }

#ifdef PE_PHYSICS
        if (desc.startPhysics)
        {
            if (auto *physics = GetGlobalSystem<PhysicsSystem>())
                physics->StartSimulation(scene);
        }
#endif

#ifdef PE_AUDIO
        if (desc.startAudio)
        {
            if (auto *audio = GetGlobalSystem<AudioSystem>())
                audio->StartPlayMode(scene);
        }
#endif
    }

    void StopRuntimePlaySession(const RuntimePlaySessionStopDesc &desc)
    {
#ifdef PE_AUDIO
        if (desc.stopAudio)
        {
            if (auto *audio = GetGlobalSystem<AudioSystem>())
                audio->StopPlayMode();
        }
#endif

#ifdef PE_PHYSICS
        if (desc.stopPhysics)
        {
            if (auto *physics = GetGlobalSystem<PhysicsSystem>())
                physics->StopSimulation();
        }
#endif
    }

    void SetRuntimePlaySessionPaused(bool paused)
    {
#ifdef PE_PHYSICS
        if (auto *physics = GetGlobalSystem<PhysicsSystem>())
            physics->SetPaused(paused);
#else
        (void)paused;
#endif
    }

    void ClearRuntimeAnimationState()
    {
        if (auto *animation = GetGlobalSystem<AnimationSystem>())
            animation->ClearAllAnimations();
    }
} // namespace pe
