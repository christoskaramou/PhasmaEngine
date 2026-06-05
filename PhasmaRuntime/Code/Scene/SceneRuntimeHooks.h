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
        void (*refreshRenderDescriptors)() = nullptr;
        void (*refreshSceneSky)() = nullptr;
        bool (*isPhysicsSimulating)() = nullptr;
        void (*stopPhysicsSimulation)() = nullptr;
        void (*clearPhysicsBodies)() = nullptr;
        void (*removePhysicsBody)(NodeId *node) = nullptr;
        void (*addPhysicsBody)(Scene &scene, NodeId *node, const PhysicsBodyDesc &desc) = nullptr;
        bool (*hasPhysicsBody)(const NodeId *node) = nullptr;
        PhysicsBodyDesc *(*getPhysicsBodyDesc)(NodeId *node) = nullptr;
        const PhysicsBodyDesc *(*getPhysicsBodyDescConst)(const NodeId *node) = nullptr;
#ifdef PE_PHYSICS2D
        void (*clearPhysics2DBodies)() = nullptr;
        void (*removePhysics2DBody)(NodeId *node) = nullptr;
        void (*addPhysics2DBody)(Scene &scene, NodeId *node, const Physics2DBodyDesc &desc) = nullptr;
        bool (*hasPhysics2DBody)(const NodeId *node) = nullptr;
        Physics2DBodyDesc *(*getPhysics2DBodyDesc)(NodeId *node) = nullptr;
        const Physics2DBodyDesc *(*getPhysics2DBodyDescConst)(const NodeId *node) = nullptr;
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
    void ClearSceneSelection();
    [[nodiscard]] bool IsSceneNodeSelected(const NodeId *node);
    void ClearSceneAnimations();
    void RemoveSceneAnimation(NodeId *node);
    void PlaySceneAnimation(Scene &scene, NodeId *node, int clipIndex, bool loop);
    void RefreshSceneRenderDescriptors();
    void RefreshSceneSky();
    [[nodiscard]] bool IsScenePhysicsSimulating();
    void StopScenePhysicsSimulation();
    void ClearScenePhysicsBodies();
    void RemoveScenePhysicsBody(NodeId *node);
    void AddScenePhysicsBody(Scene &scene, NodeId *node, const PhysicsBodyDesc &desc);
    [[nodiscard]] bool HasScenePhysicsBody(const NodeId *node);
    [[nodiscard]] PhysicsBodyDesc *GetScenePhysicsBodyDesc(NodeId *node);
    [[nodiscard]] const PhysicsBodyDesc *GetScenePhysicsBodyDesc(const NodeId *node);
#ifdef PE_PHYSICS2D
    void ClearScenePhysics2DBodies();
    void RemoveScenePhysics2DBody(NodeId *node);
    void AddScenePhysics2DBody(Scene &scene, NodeId *node, const Physics2DBodyDesc &desc);
    [[nodiscard]] bool HasScenePhysics2DBody(const NodeId *node);
    [[nodiscard]] Physics2DBodyDesc *GetScenePhysics2DBodyDesc(NodeId *node);
    [[nodiscard]] const Physics2DBodyDesc *GetScenePhysics2DBodyDesc(const NodeId *node);
#endif
    void ClearSceneAudioSources();
    void RemoveSceneAudioSource(NodeId *node);
    void AddSceneAudioSource(Scene &scene, NodeId *node, const AudioSourceDesc &desc);
    [[nodiscard]] bool HasSceneAudioSource(const NodeId *node);
    [[nodiscard]] AudioSourceDesc *GetSceneAudioSourceDesc(NodeId *node);
    [[nodiscard]] const AudioSourceDesc *GetSceneAudioSourceDesc(const NodeId *node);
} // namespace pe
