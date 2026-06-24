#include "Scene/SceneRuntimeHooks.h"
#include "Scene/Scene.h"
#include "Script/ScriptSystem.h"
#include "Render/SceneRendererHost.h"
#include "RenderPasses/ForwardPlusLightCullingPass.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/RayTracingPass.h"
#include "Systems/AnimationSystem.h"
#ifdef PE_AUDIO
#include "Systems/AudioSystem.h"
#endif
#ifdef PE_PHYSICS
#include "Systems/PhysicsSystem.h"
#endif
#ifdef PE_PHYSICS2D
#include "Systems/Physics2DSystem.h"
#endif

namespace pe
{
    namespace
    {
        // PhasmaRuntime is static-linked into the editor host and hot-reload module,
        // so this registration is per image. Register from the image that calls it.
        // TODO(shared-runtime): move this state behind the shared runtime image if
        // PhasmaRuntime stops being linked as a static library.
        SceneRuntimeHooks s_sceneRuntimeHooks;

        void DefaultClearSceneAnimations()
        {
            if (auto *animation = GetGlobalSystem<AnimationSystem>())
                animation->ClearAllAnimations();
        }

        void DefaultRemoveSceneAnimation(NodeId *node)
        {
            if (auto *animation = GetGlobalSystem<AnimationSystem>())
                animation->RemoveAnimation(node);
        }

        void DefaultInvokeSceneNodeScriptFunction(NodeId *node, const char *functionName)
        {
            if (auto *scripts = GetGlobalSystem<ScriptSystem>())
                scripts->InvokeNodeFunction(node, functionName ? functionName : "");
        }

        void DefaultPlaySceneAnimation(Scene &scene, NodeId *node, int clipIndex, bool loop)
        {
            if (auto *animation = GetGlobalSystem<AnimationSystem>())
                animation->PlayAnimation(scene, node, clipIndex, loop);
        }

        void DefaultApplySkinnedStrip2DPose(Scene &scene, NodeId *node)
        {
            const NodeSkinnedStrip2DComponent *state = scene.GetSkinnedStrip2DState(node);
            if (!state)
                return;
            if (auto *animation = GetGlobalSystem<AnimationSystem>())
                animation->SetJointLocalRotationsZ(scene,
                                                   node,
                                                   state->rotationsRadians,
                                                   state->stretchScale,
                                                   &state->widthScales);
        }

        void DefaultRefreshSceneRenderDescriptors()
        {
            if (Settings::Get<SceneSettings>().forward_plus)
            {
                if (auto *forwardPlus = GetGlobalComponent<ForwardPlusLightCullingPass>())
                {
                    if (forwardPlus->HasLiveResources())
                        forwardPlus->UpdateDescriptorSets();
                }
            }
            if (auto *lightOpaque = GetGlobalComponent<LightOpaquePass>())
                lightOpaque->UpdateDescriptorSets();
            if (auto *lightTransparent = GetGlobalComponent<LightTransparentPass>())
                lightTransparent->UpdateDescriptorSets();
            if (auto *rayTracing = GetGlobalComponent<RayTracingPass>())
                rayTracing->UpdateDescriptorSets();
        }

        void DefaultRefreshSceneSky()
        {
            if (auto *renderer = GetActiveSceneRendererHost())
            {
                renderer->ReloadSkyFromSettings();
                DefaultRefreshSceneRenderDescriptors();
            }
        }

#ifdef PE_PHYSICS
        bool DefaultIsScenePhysicsSimulating()
        {
            if (auto *physics = GetGlobalSystem<PhysicsSystem>())
                return physics->IsSimulating();
            return false;
        }

        void DefaultStopScenePhysicsSimulation()
        {
            if (auto *physics = GetGlobalSystem<PhysicsSystem>())
                physics->StopSimulation();
        }

        void DefaultClearScenePhysicsBodies()
        {
            if (auto *physics = GetGlobalSystem<PhysicsSystem>())
                physics->ClearAllBodies();
        }

        void DefaultRemoveScenePhysicsBody(NodeId *node)
        {
            if (auto *physics = GetGlobalSystem<PhysicsSystem>())
                physics->RemoveBody(node);
        }

        void DefaultAddScenePhysicsBody(Scene &scene, NodeId *node, const PhysicsBodyDesc &desc)
        {
            if (auto *physics = GetGlobalSystem<PhysicsSystem>())
                physics->AddBody(scene, node, desc);
        }

        bool DefaultHasScenePhysicsBody(const NodeId *node)
        {
            if (auto *physics = GetGlobalSystem<PhysicsSystem>())
                return physics->HasBody(node);
            return false;
        }

        PhysicsBodyDesc *DefaultGetScenePhysicsBodyDesc(NodeId *node)
        {
            if (auto *physics = GetGlobalSystem<PhysicsSystem>())
                return physics->GetBodyDesc(node);
            return nullptr;
        }

        const PhysicsBodyDesc *DefaultGetScenePhysicsBodyDescConst(const NodeId *node)
        {
            if (auto *physics = GetGlobalSystem<PhysicsSystem>())
                return physics->GetBodyDesc(node);
            return nullptr;
        }
#endif

#ifdef PE_PHYSICS2D
        void DefaultClearScenePhysics2DBodies()
        {
            if (auto *physics = GetGlobalSystem<Physics2DSystem>())
                physics->ClearWorld();
        }

        void DefaultRemoveScenePhysics2DBody(NodeId *node)
        {
            if (auto *physics = GetGlobalSystem<Physics2DSystem>())
                physics->RemoveBody(node);
        }

        void DefaultAddScenePhysics2DBody(Scene &scene, NodeId *node, const Physics2DBodyDesc &desc)
        {
            if (auto *physics = GetGlobalSystem<Physics2DSystem>())
                physics->AddBody(scene, node, desc);
        }

        bool DefaultHasScenePhysics2DBody(const NodeId *node)
        {
            if (auto *physics = GetGlobalSystem<Physics2DSystem>())
                return physics->HasBody(node);
            return false;
        }

        Physics2DBodyDesc *DefaultGetScenePhysics2DBodyDesc(NodeId *node)
        {
            if (auto *physics = GetGlobalSystem<Physics2DSystem>())
                return physics->GetBodyDesc(node);
            return nullptr;
        }

        const Physics2DBodyDesc *DefaultGetScenePhysics2DBodyDescConst(const NodeId *node)
        {
            if (auto *physics = GetGlobalSystem<Physics2DSystem>())
                return physics->GetBodyDesc(node);
            return nullptr;
        }
#endif

#ifdef PE_AUDIO
        void DefaultClearSceneAudioSources()
        {
            if (auto *audio = GetGlobalSystem<AudioSystem>())
                audio->ClearAllSources();
        }

        void DefaultRemoveSceneAudioSource(NodeId *node)
        {
            if (auto *audio = GetGlobalSystem<AudioSystem>())
                audio->RemoveSource(node);
        }

        void DefaultAddSceneAudioSource(Scene &scene, NodeId *node, const AudioSourceDesc &desc)
        {
            if (auto *audio = GetGlobalSystem<AudioSystem>())
                audio->AddSource(scene, node, desc);
        }

        bool DefaultHasSceneAudioSource(const NodeId *node)
        {
            if (auto *audio = GetGlobalSystem<AudioSystem>())
                return audio->HasSource(node);
            return false;
        }

        AudioSourceDesc *DefaultGetSceneAudioSourceDesc(NodeId *node)
        {
            if (auto *audio = GetGlobalSystem<AudioSystem>())
                return audio->GetSourceDesc(node);
            return nullptr;
        }

        const AudioSourceDesc *DefaultGetSceneAudioSourceDescConst(const NodeId *node)
        {
            if (auto *audio = GetGlobalSystem<AudioSystem>())
                return audio->GetSourceDesc(node);
            return nullptr;
        }
#endif
    } // namespace

    SceneRuntimeHooks CreateDefaultSceneRuntimeHooks()
    {
        SceneRuntimeHooks hooks{};
        hooks.clearAnimations = DefaultClearSceneAnimations;
        hooks.removeAnimation = DefaultRemoveSceneAnimation;
        hooks.playAnimation = DefaultPlaySceneAnimation;
        hooks.applySkinnedStrip2DPose = DefaultApplySkinnedStrip2DPose;
        hooks.refreshRenderDescriptors = DefaultRefreshSceneRenderDescriptors;
        hooks.refreshSceneSky = DefaultRefreshSceneSky;
        hooks.invokeNodeScriptFunction = DefaultInvokeSceneNodeScriptFunction;
#ifdef PE_PHYSICS
        hooks.isPhysicsSimulating = DefaultIsScenePhysicsSimulating;
        hooks.stopPhysicsSimulation = DefaultStopScenePhysicsSimulation;
        hooks.clearPhysicsBodies = DefaultClearScenePhysicsBodies;
        hooks.removePhysicsBody = DefaultRemoveScenePhysicsBody;
        hooks.addPhysicsBody = DefaultAddScenePhysicsBody;
        hooks.hasPhysicsBody = DefaultHasScenePhysicsBody;
        hooks.getPhysicsBodyDesc = DefaultGetScenePhysicsBodyDesc;
        hooks.getPhysicsBodyDescConst = DefaultGetScenePhysicsBodyDescConst;
#endif
#ifdef PE_PHYSICS2D
        hooks.clearPhysics2DBodies = DefaultClearScenePhysics2DBodies;
        hooks.removePhysics2DBody = DefaultRemoveScenePhysics2DBody;
        hooks.addPhysics2DBody = DefaultAddScenePhysics2DBody;
        hooks.hasPhysics2DBody = DefaultHasScenePhysics2DBody;
        hooks.getPhysics2DBodyDesc = DefaultGetScenePhysics2DBodyDesc;
        hooks.getPhysics2DBodyDescConst = DefaultGetScenePhysics2DBodyDescConst;
#endif
#ifdef PE_AUDIO
        hooks.clearAudioSources = DefaultClearSceneAudioSources;
        hooks.removeAudioSource = DefaultRemoveSceneAudioSource;
        hooks.addAudioSource = DefaultAddSceneAudioSource;
        hooks.hasAudioSource = DefaultHasSceneAudioSource;
        hooks.getAudioSourceDesc = DefaultGetSceneAudioSourceDesc;
        hooks.getAudioSourceDescConst = DefaultGetSceneAudioSourceDescConst;
#endif
        return hooks;
    }

    void SetSceneRuntimeHooks(SceneRuntimeHooks hooks)
    {
        s_sceneRuntimeHooks = hooks;
    }

    void ClearSceneSelection()
    {
        if (s_sceneRuntimeHooks.clearSelection)
            s_sceneRuntimeHooks.clearSelection();
    }

    bool IsSceneNodeSelected(const NodeId *node)
    {
        return s_sceneRuntimeHooks.isNodeSelected ? s_sceneRuntimeHooks.isNodeSelected(node) : false;
    }

    void ClearSceneAnimations()
    {
        if (s_sceneRuntimeHooks.clearAnimations)
            s_sceneRuntimeHooks.clearAnimations();
    }

    void RemoveSceneAnimation(NodeId *node)
    {
        if (s_sceneRuntimeHooks.removeAnimation)
            s_sceneRuntimeHooks.removeAnimation(node);
    }

    void PlaySceneAnimation(Scene &scene, NodeId *node, int clipIndex, bool loop)
    {
        if (s_sceneRuntimeHooks.playAnimation)
            s_sceneRuntimeHooks.playAnimation(scene, node, clipIndex, loop);
    }

    void ApplySceneSkinnedStrip2DPose(Scene &scene, NodeId *node)
    {
        if (s_sceneRuntimeHooks.applySkinnedStrip2DPose)
            s_sceneRuntimeHooks.applySkinnedStrip2DPose(scene, node);
    }

    void RefreshSceneRenderDescriptors()
    {
        if (s_sceneRuntimeHooks.refreshRenderDescriptors)
            s_sceneRuntimeHooks.refreshRenderDescriptors();
    }

    void RefreshSceneSky()
    {
        if (s_sceneRuntimeHooks.refreshSceneSky)
            s_sceneRuntimeHooks.refreshSceneSky();
    }

    void InvokeSceneNodeScriptFunction(NodeId *node, const char *functionName)
    {
        if (s_sceneRuntimeHooks.invokeNodeScriptFunction)
            s_sceneRuntimeHooks.invokeNodeScriptFunction(node, functionName);
    }

    bool IsScenePhysicsSimulating()
    {
        return s_sceneRuntimeHooks.isPhysicsSimulating ? s_sceneRuntimeHooks.isPhysicsSimulating() : false;
    }

    void StopScenePhysicsSimulation()
    {
        if (s_sceneRuntimeHooks.stopPhysicsSimulation)
            s_sceneRuntimeHooks.stopPhysicsSimulation();
    }

    void ClearScenePhysicsBodies()
    {
        if (s_sceneRuntimeHooks.clearPhysicsBodies)
            s_sceneRuntimeHooks.clearPhysicsBodies();
    }

    void RemoveScenePhysicsBody(NodeId *node)
    {
        if (s_sceneRuntimeHooks.removePhysicsBody)
            s_sceneRuntimeHooks.removePhysicsBody(node);
    }

    void AddScenePhysicsBody(Scene &scene, NodeId *node, const PhysicsBodyDesc &desc)
    {
        if (s_sceneRuntimeHooks.addPhysicsBody)
            s_sceneRuntimeHooks.addPhysicsBody(scene, node, desc);
    }

    bool HasScenePhysicsBody(const NodeId *node)
    {
        return s_sceneRuntimeHooks.hasPhysicsBody ? s_sceneRuntimeHooks.hasPhysicsBody(node) : false;
    }

    PhysicsBodyDesc *GetScenePhysicsBodyDesc(NodeId *node)
    {
        return s_sceneRuntimeHooks.getPhysicsBodyDesc ? s_sceneRuntimeHooks.getPhysicsBodyDesc(node) : nullptr;
    }

    const PhysicsBodyDesc *GetScenePhysicsBodyDesc(const NodeId *node)
    {
        return s_sceneRuntimeHooks.getPhysicsBodyDescConst ? s_sceneRuntimeHooks.getPhysicsBodyDescConst(node) : nullptr;
    }

#ifdef PE_PHYSICS2D
    void ClearScenePhysics2DBodies()
    {
        if (s_sceneRuntimeHooks.clearPhysics2DBodies)
            s_sceneRuntimeHooks.clearPhysics2DBodies();
    }

    void RemoveScenePhysics2DBody(NodeId *node)
    {
        if (s_sceneRuntimeHooks.removePhysics2DBody)
            s_sceneRuntimeHooks.removePhysics2DBody(node);
    }

    void AddScenePhysics2DBody(Scene &scene, NodeId *node, const Physics2DBodyDesc &desc)
    {
        if (s_sceneRuntimeHooks.addPhysics2DBody)
            s_sceneRuntimeHooks.addPhysics2DBody(scene, node, desc);
    }

    bool HasScenePhysics2DBody(const NodeId *node)
    {
        return s_sceneRuntimeHooks.hasPhysics2DBody ? s_sceneRuntimeHooks.hasPhysics2DBody(node) : false;
    }

    Physics2DBodyDesc *GetScenePhysics2DBodyDesc(NodeId *node)
    {
        return s_sceneRuntimeHooks.getPhysics2DBodyDesc ? s_sceneRuntimeHooks.getPhysics2DBodyDesc(node) : nullptr;
    }

    const Physics2DBodyDesc *GetScenePhysics2DBodyDesc(const NodeId *node)
    {
        return s_sceneRuntimeHooks.getPhysics2DBodyDescConst ? s_sceneRuntimeHooks.getPhysics2DBodyDescConst(node) : nullptr;
    }
#endif

    void ClearSceneAudioSources()
    {
        if (s_sceneRuntimeHooks.clearAudioSources)
            s_sceneRuntimeHooks.clearAudioSources();
    }

    void RemoveSceneAudioSource(NodeId *node)
    {
        if (s_sceneRuntimeHooks.removeAudioSource)
            s_sceneRuntimeHooks.removeAudioSource(node);
    }

    void AddSceneAudioSource(Scene &scene, NodeId *node, const AudioSourceDesc &desc)
    {
        if (s_sceneRuntimeHooks.addAudioSource)
            s_sceneRuntimeHooks.addAudioSource(scene, node, desc);
    }

    bool HasSceneAudioSource(const NodeId *node)
    {
        return s_sceneRuntimeHooks.hasAudioSource ? s_sceneRuntimeHooks.hasAudioSource(node) : false;
    }

    AudioSourceDesc *GetSceneAudioSourceDesc(NodeId *node)
    {
        return s_sceneRuntimeHooks.getAudioSourceDesc ? s_sceneRuntimeHooks.getAudioSourceDesc(node) : nullptr;
    }

    const AudioSourceDesc *GetSceneAudioSourceDesc(const NodeId *node)
    {
        return s_sceneRuntimeHooks.getAudioSourceDescConst ? s_sceneRuntimeHooks.getAudioSourceDescConst(node) : nullptr;
    }
} // namespace pe
