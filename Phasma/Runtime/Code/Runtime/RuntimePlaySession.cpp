#include "Runtime/RuntimePlaySession.h"
#include "Scene/Scene.h"
#include "Script/ScriptSystem.h"
#include "Systems/AnimationSystem.h"
#include "Systems/AudioSystem.h"
#include "Systems/Physics2DSystem.h"
#include "Systems/PhysicsSystem.h"
#include "UI/RuntimeUi.h"
#include "Terrain/TerrainSystem.h"
#include "Voxel/VoxelSystem.h"

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
#ifdef PE_PHYSICS2D
        if (desc.startPhysics)
        {
            if (auto *physics2d = GetGlobalSystem<Physics2DSystem>())
                physics2d->SetPaused(false);
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
#ifdef PE_PHYSICS2D
        if (desc.stopPhysics)
        {
            if (auto *physics2d = GetGlobalSystem<Physics2DSystem>())
                physics2d->ClearWorld();
        }
#endif

        // Tear down script-created runtime UI (HUD overlays, menus). Scene-authored UI is left
        // for the scene snapshot/sync to restore; script overlays have no restore path and
        // would otherwise persist after the editor returns to edit mode.
        if (RuntimeUiSystem *runtimeUi = GetActiveRuntimeUi())
            runtimeUi->ClearScriptScreens();

        // Tear down the play-scoped voxel world (built by the play script via voxel.create) while its
        // geometry arena is still valid. The editor Stop path runs this BEFORE RestoreSnapshot rebuilds
        // the scene buffers; without it the world's VoxelSystem keeps streaming AddArenaMesh into the
        // arena that RestoreSnapshot just wiped (warn spam, and pre-fix a CopyBufferStaged overflow).
        if (auto *voxels = GetGlobalSystem<voxel::VoxelSystem>())
            voxels->Destroy();
        // Terrain host mesh lives in the same scene buffers RestoreSnapshot wipes; drop it so reconcile
        // rebuilds cleanly instead of holding a stale host node.
        if (auto *terr = GetGlobalSystem<terrain::TerrainSystem>())
            terr->Destroy();
    }

    void SetRuntimePlaySessionPaused(bool paused)
    {
#ifdef PE_PHYSICS
        if (auto *physics = GetGlobalSystem<PhysicsSystem>())
            physics->SetPaused(paused);
#endif
#ifdef PE_PHYSICS2D
        if (auto *physics2d = GetGlobalSystem<Physics2DSystem>())
            physics2d->SetPaused(paused);
#endif
#if !defined(PE_PHYSICS) && !defined(PE_PHYSICS2D)
        (void)paused;
#endif
    }

    void ClearRuntimeAnimationState()
    {
        if (auto *animation = GetGlobalSystem<AnimationSystem>())
            animation->ClearAllAnimations();
    }
} // namespace pe
