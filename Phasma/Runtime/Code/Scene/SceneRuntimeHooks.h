#pragma once

#include "Audio/AudioTypes.h"
#include "Physics/PhysicsTypes.h"

namespace pe
{
    class Scene;
    struct NodeId;
#ifdef PE_PHYSICS2D
    struct Physics2DBodyDesc;
#endif

    struct SceneRuntimeHooks
    {
        void (*clearSelection)() = nullptr;
        bool (*isNodeSelected)(const NodeId *node) = nullptr;
        void (*clearAnimations)() = nullptr;
        void (*removeAnimation)(NodeId *node) = nullptr;
        void (*playAnimation)(Scene &scene, NodeId *node, int clipIndex, bool loop) = nullptr;
        void (*applySkinnedStrip2DPose)(Scene &scene, NodeId *node) = nullptr;
        void (*refreshRenderDescriptors)() = nullptr;
        void (*refreshSceneSky)() = nullptr;
        // Call a named function in a node's own script environment (trigger zone on_enter/on_exit).
        void (*invokeNodeScriptFunction)(NodeId *node, const char *functionName) = nullptr;
        // Drive a trigger zone's own audio source (its NodeTriggerZoneTag::audioSource) by a 0..1 gain
        // from its distance blend; gain > 0 plays + scales, gain == 0 stops.
        void (*applyAudioZoneSource)(NodeId *node, const AudioSourceDesc &desc, float gain) = nullptr;
        bool (*isPhysicsSimulating)() = nullptr;
        // Wire a trigger-zone sensor body's enter/exit so overlap fires the Physics-section script
        // (scriptPath) onEnter/onExit, optionally filtered to bodies whose node name contains filterTag.
        void (*setZonePhysicsScriptTrigger)(NodeId *node, const char *scriptPath, const char *onEnter,
                                            const char *onExit, const char *filterTag) = nullptr;
        void (*stopPhysicsSimulation)() = nullptr;
        void (*clearPhysicsBodies)() = nullptr;
        void (*removePhysicsBody)(NodeId *node) = nullptr;
        void (*addPhysicsBody)(Scene &scene, NodeId *node, const PhysicsBodyDesc &desc) = nullptr;
        bool (*hasPhysicsBody)(const NodeId *node) = nullptr;
        PhysicsBodyDesc *(*getPhysicsBodyDesc)(NodeId *node) = nullptr;
        const PhysicsBodyDesc *(*getPhysicsBodyDescConst)(const NodeId *node) = nullptr;
        // Force field: apply a continuous world-space force to a body this frame (3D). 2D analogue below.
        void (*applyPhysicsForce)(NodeId *node, const vec3 &force) = nullptr;
#ifdef PE_PHYSICS2D
        void (*clearPhysics2DBodies)() = nullptr;
        void (*removePhysics2DBody)(NodeId *node) = nullptr;
        void (*addPhysics2DBody)(Scene &scene, NodeId *node, const Physics2DBodyDesc &desc) = nullptr;
        bool (*hasPhysics2DBody)(const NodeId *node) = nullptr;
        Physics2DBodyDesc *(*getPhysics2DBodyDesc)(NodeId *node) = nullptr;
        const Physics2DBodyDesc *(*getPhysics2DBodyDescConst)(const NodeId *node) = nullptr;
        void (*applyPhysics2DForce)(NodeId *node, const vec3 &force) = nullptr;
        // 2D analogue of setZonePhysicsScriptTrigger (Box2D sensor enter/exit -> Physics-section script).
        void (*setZonePhysics2DScriptTrigger)(NodeId *node, const char *scriptPath, const char *onEnter,
                                              const char *onExit, const char *filterTag) = nullptr;
#endif
        void (*clearAudioSources)() = nullptr;
        void (*removeAudioSource)(NodeId *node) = nullptr;
        void (*addAudioSource)(Scene &scene, NodeId *node, const AudioSourceDesc &desc) = nullptr;
        bool (*hasAudioSource)(const NodeId *node) = nullptr;
        AudioSourceDesc *(*getAudioSourceDesc)(NodeId *node) = nullptr;
        const AudioSourceDesc *(*getAudioSourceDescConst)(const NodeId *node) = nullptr;
    };

    [[nodiscard]] SceneRuntimeHooks CreateDefaultSceneRuntimeHooks();
    void SetSceneRuntimeHooks(SceneRuntimeHooks hooks);
    // Feed for the default isNodeSelected hook: marks one node for the selection-outline pass in
    // hosts without a SelectionManager (PhasmaPlayer). -1 = nothing selected.
    void SetRuntimeSelectedNodeIndex(int nodeIndex);
    void ClearSceneSelection();
    [[nodiscard]] bool IsSceneNodeSelected(const NodeId *node);
    void ClearSceneAnimations();
    void RemoveSceneAnimation(NodeId *node);
    void PlaySceneAnimation(Scene &scene, NodeId *node, int clipIndex, bool loop);
    void ApplySceneSkinnedStrip2DPose(Scene &scene, NodeId *node);
    void RefreshSceneRenderDescriptors();
    void RefreshSceneSky();
    void InvokeSceneNodeScriptFunction(NodeId *node, const char *functionName);
    void ApplySceneAudioZoneSource(NodeId *node, const AudioSourceDesc &desc, float gain);
    [[nodiscard]] bool IsScenePhysicsSimulating();
    void SetSceneZonePhysicsScriptTrigger(NodeId *node, const char *scriptPath, const char *onEnter,
                                          const char *onExit, const char *filterTag);
    void StopScenePhysicsSimulation();
    void ClearScenePhysicsBodies();
    void RemoveScenePhysicsBody(NodeId *node);
    void AddScenePhysicsBody(Scene &scene, NodeId *node, const PhysicsBodyDesc &desc);
    [[nodiscard]] bool HasScenePhysicsBody(const NodeId *node);
    [[nodiscard]] PhysicsBodyDesc *GetScenePhysicsBodyDesc(NodeId *node);
    [[nodiscard]] const PhysicsBodyDesc *GetScenePhysicsBodyDesc(const NodeId *node);
    void ApplyScenePhysicsForce(NodeId *node, const vec3 &force);
#ifdef PE_PHYSICS2D
    void ClearScenePhysics2DBodies();
    void RemoveScenePhysics2DBody(NodeId *node);
    void AddScenePhysics2DBody(Scene &scene, NodeId *node, const Physics2DBodyDesc &desc);
    [[nodiscard]] bool HasScenePhysics2DBody(const NodeId *node);
    [[nodiscard]] Physics2DBodyDesc *GetScenePhysics2DBodyDesc(NodeId *node);
    [[nodiscard]] const Physics2DBodyDesc *GetScenePhysics2DBodyDesc(const NodeId *node);
    void ApplyScenePhysics2DForce(NodeId *node, const vec3 &force);
    void SetSceneZonePhysics2DScriptTrigger(NodeId *node, const char *scriptPath, const char *onEnter,
                                            const char *onExit, const char *filterTag);
#endif
    void ClearSceneAudioSources();
    void RemoveSceneAudioSource(NodeId *node);
    void AddSceneAudioSource(Scene &scene, NodeId *node, const AudioSourceDesc &desc);
    [[nodiscard]] bool HasSceneAudioSource(const NodeId *node);
    [[nodiscard]] AudioSourceDesc *GetSceneAudioSourceDesc(NodeId *node);
    [[nodiscard]] const AudioSourceDesc *GetSceneAudioSourceDesc(const NodeId *node);
} // namespace pe
