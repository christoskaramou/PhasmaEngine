#pragma once

namespace pe
{
    class Scene;

    struct RuntimePlaySessionStartDesc
    {
        bool callScriptInit = false;
        bool startPhysics = true;
        bool startAudio = true;
    };

    struct RuntimePlaySessionStopDesc
    {
        bool stopPhysics = true;
        bool stopAudio = true;
    };

    void StartRuntimePlaySession(Scene &scene, const RuntimePlaySessionStartDesc &desc = {});
    void StopRuntimePlaySession(const RuntimePlaySessionStopDesc &desc = {});
    void SetRuntimePlaySessionPaused(bool paused);
    void ClearRuntimeAnimationState();
} // namespace pe
