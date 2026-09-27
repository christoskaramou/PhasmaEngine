#include "CppScript.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Base/GamePack.h"
#include "Base/Log.h"
#include "Base/Path.h"
#include "Camera/Camera.h"
#include "Particles/ParticleManager.h"
#include "Render/ScriptRenderPasses.h"
#include "Render/SceneRendererHost.h"
#include "Scene/Material.h"
#include "Scene/Primitives.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Script/Bindings/Filesystem/FilesystemBindings.h"
#include "Script/Bindings/Material/MaterialBindings.h"
#include "Script/NativeScriptGuard.h"
#include "Script/NativeTrs.h"
#include "Script/ScriptRuntimeHooks.h"
#include "Script/Bindings/Input/InputState.h"
#include "Systems/AnimationSystem.h"
#ifdef PE_AUDIO
#include "Systems/AudioSystem.h"
#endif
#include "UI/RuntimeUi.h"
#include <cmath>
#include <unordered_set>

#if defined(PE_PROJECT_NATIVE_STATIC)
extern "C" const phasma::ScriptModule *PhasmaGetScriptModule(uint32_t version) noexcept;
#endif

namespace pe
{
    namespace
    {
        template <class F>
        auto GuardApi(F &&call) noexcept -> decltype(call())
        {
            try
            {
                return call();
            }
            catch (...)
            {
                return {};
            }
        }

        bool Finite(phasma::Vec3 v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        // Runs fn on the audio system; false when the build has no audio (fn is never instantiated then).
        template <class Fn>
        uint32_t WithAudio(Fn &&fn)
        {
#ifdef PE_AUDIO
            if (AudioSystem *audio = GetGlobalSystem<AudioSystem>())
            {
                fn(*audio);
                return 1;
            }
#endif
            return 0;
        }

        // Rebuilds the local TRS, replacing rotation (degrees) or scale.
        void SetLocalTrs(Scene &scene, NodeId *node, const vec3 *rotationDegrees, const vec3 *scale)
        {
            scene.SetLocalMatrix(node, ReplaceTrs(scene.GetLocalMatrix(node), rotationDegrees, scale));
        }

        bool PlayTree(Scene &scene, AnimationSystem &animation, NodeId *node, const std::string &clip, bool loop)
        {
            bool played = false;
            for (const auto &candidate : scene.GetAnimationClipsForNode(node))
                if (candidate.name == clip)
                {
                    animation.PlayAnimation(scene, node, clip, loop);
                    played = true;
                    break;
                }
            for (NodeId *child : scene.GetChildren(node))
                played |= PlayTree(scene, animation, child, clip, loop);
            return played;
        }

        bool SetSpeedTree(Scene &scene, AnimationSystem &animation, NodeId *node, float speed)
        {
            bool animated = animation.GetAnimationState(node) != nullptr;
            animation.SetSpeed(node, speed);
            for (NodeId *child : scene.GetChildren(node))
                animated |= SetSpeedTree(scene, animation, child, speed);
            return animated;
        }

        bool GetClipDurationTree(Scene &scene, NodeId *node, const std::string &clip, float &seconds)
        {
            for (const auto &candidate : scene.GetAnimationClipsForNode(node))
                if (candidate.name == clip)
                {
                    seconds = candidate.duration / (candidate.ticksPerSecond > 0.0f ? candidate.ticksPerSecond : 25.0f);
                    return true;
                }
            for (NodeId *child : scene.GetChildren(node))
                if (GetClipDurationTree(scene, child, clip, seconds))
                    return true;
            return false;
        }

        NodeId *FindChildTree(Scene &scene, NodeId *node, const char *name)
        {
            if (scene.GetNodeName(node) == name)
                return node;
            for (NodeId *child : scene.GetChildren(node))
                if (NodeId *found = FindChildTree(scene, child, name))
                    return found;
            return nullptr;
        }

        void ScriptError(const char *name, uint32_t fault = 0)
        {
            if (!fault)
            {
                Log::Error(std::string("[CppScript] callback failed; instance disabled: ") + name);
                return;
            }
            char code[16];
            std::snprintf(code, sizeof(code), "0x%08X", fault);
            Log::Error(std::string("[CppScript] fault ") + code + " in " + name + "; instance disabled and its state leaked");
        }

        bool MatchesScript(const phasma::ScriptDesc &script, const std::string &path)
        {
            return MatchesCppScript(path, script.name, script.sourceFile);
        }
    } // namespace

    CppScriptSystem::~CppScriptSystem()
    {
        Destroy();
    }

    bool CppScriptSystem::LiveNode::operator()(const SceneNodeHandle &handle) const
    {
        Scene *scene = GetActiveScene();
        return scene && handle.IsValid(*scene);
    }

    phasma::Node CppScriptSystem::Handle(NodeId *node)
    {
        Scene *scene = GetActiveScene();
        if (!scene || !node || !scene->IsNodeAlive(node))
            return 0;
        return m_handles.Issue(node, scene->MakeHandle(node));
    }

    NodeId *CppScriptSystem::Resolve(phasma::Node handle)
    {
        const SceneNodeHandle *node = m_handles.Resolve(handle);
        return node ? node->nodeId : nullptr;
    }

    void CppScriptSystem::Init()
    {
        // ScriptSystem::Reload re-runs Init for Lua; the native module and its instances must survive that.
        if (m_initialized)
            return;
        m_initialized = true;
#if defined(PE_PROJECT_NATIVE_STATIC)
        if (m_module.StageLinked(PhasmaGetScriptModule(phasma::ScriptAbiVersion)))
            m_module.Commit();
        else
            Log::Error("[CppScript] linked module rejected: " + m_module.Error());
        m_loggedAttempts = m_module.Status().attempts;
#endif
        m_api = {phasma::ScriptAbiVersion, sizeof(phasma::ScriptApi), this,
                 [](void *, const char *message) noexcept
                 { try { if (message) Log::Info(std::string("[CppScript] ") + message); } catch (...) {} },
                 [](void *ctx, const char *name) noexcept -> phasma::Node
                 { return GuardApi([&]
                                   { Scene *scene = GetActiveScene(); return scene && name ? static_cast<CppScriptSystem *>(ctx)->Handle(scene->FindNodeByName(name)) : 0; }); },
                 [](void *ctx, const char *name, float size) noexcept -> phasma::Node
                 {
                     return GuardApi([&]() -> phasma::Node
                                     {
                         Scene *scene = GetActiveScene();
                         if (!scene || !name || !*name || !std::isfinite(size) || size <= 0.0f)
                             return 0;
                         NodeId *node = scene->CreateNode(name);
                         try { scene->AttachPrimitiveToNode(node, Primitives::CreateCube(size)); }
                         catch (...) { scene->DeleteNode(node); throw; }
                         return static_cast<CppScriptSystem *>(ctx)->Handle(node); });
                 },
                 [](void *ctx, phasma::Node node) noexcept -> uint32_t
                 { return GuardApi([&]() -> uint32_t
                                   { return static_cast<CppScriptSystem *>(ctx)->Resolve(node) != nullptr; }); },
                 [](void *ctx, phasma::Node handle, phasma::Vec3 *value) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         if (!node || !value) return 0;
                         const auto &pos = GetActiveScene()->GetLocalMatrix(node)[3];
                         *value = {pos.x, pos.y, pos.z};
                         return 1; });
                 },
                 [](void *ctx, phasma::Node handle, phasma::Vec3 value) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         if (!node || !std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) return 0;
                         Scene *scene = GetActiveScene();
                         // Cameras own their transform and overwrite the node every frame.
                         if (Camera *camera = scene->GetCameraForNode(node))
                             camera->SetPosition(vec3(value.x, value.y, value.z));
                         mat4 matrix = scene->GetLocalMatrix(node);
                         matrix[3] = vec4(value.x, value.y, value.z, 1.0f);
                         scene->SetLocalMatrix(node, matrix);
                         return 1; });
                 },
                 [](void *, const char *name) noexcept -> uint32_t
                 { return GuardApi([&]() -> uint32_t
                                   { return InputState::IsKeyDown(name); }); },
                 [](void *ctx, const char *path) noexcept -> phasma::Node
                 {
                     return GuardApi([&]() -> phasma::Node
                                     {
                         Scene *scene = GetActiveScene();
                         if (!scene || !path || !*path)
                             return 0;
                         std::filesystem::path resolved = path;
                         if (!AssetFileExists(resolved))
                             resolved = Path::ResolveAsset(path);
                         const SceneNodeHandle root = scene->InstantiatePrefab(resolved, nullptr);
                         return root.nodeId ? static_cast<CppScriptSystem *>(ctx)->Handle(root.nodeId) : 0; });
                 },
                 [](void *ctx, phasma::Node handle) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         auto *self = static_cast<CppScriptSystem *>(ctx);
                         NodeId *node = self->Resolve(handle);
                         if (!node) return 0;
                         GetActiveScene()->DeleteNode(node);
                         self->m_handles.MarkStale(); // the whole subtree died
                         return 1; });
                 },
                 [](void *ctx, phasma::Node handle, phasma::Vec3 value) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         if (!node || !Finite(value)) return 0;
                         const vec3 rotation(value.x, value.y, value.z);
                         if (Camera *camera = GetActiveScene()->GetCameraForNode(node))
                             camera->SetEuler(glm::radians(rotation));
                         SetLocalTrs(*GetActiveScene(), node, &rotation, nullptr);
                         return 1; });
                 },
                 [](void *ctx, phasma::Node handle, phasma::Vec3 value) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         if (!node || !Finite(value) || value.x == 0.0f || value.y == 0.0f || value.z == 0.0f) return 0;
                         const vec3 scale(value.x, value.y, value.z);
                         SetLocalTrs(*GetActiveScene(), node, nullptr, &scale);
                         return 1; });
                 },
                 [](void *ctx, phasma::Node handle, const char *clip, uint32_t loop) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         auto *animation = GetGlobalSystem<AnimationSystem>();
                         return node && clip && animation && PlayTree(*GetActiveScene(), *animation, node, clip, loop != 0); });
                 },
                 // UI calls run every frame: no active UI returns 0 without logging.
                 [](void *, const char *screen, uint32_t visible, uint32_t overlay) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         RuntimeUiSystem *ui = GetActiveRuntimeUi();
                         if (!ui || !screen || !*screen) return 0;
                         ui->SetScreenOverlay(screen, overlay != 0);
                         ui->SetScreenVisible(screen, visible != 0);
                         return 1; });
                 },
                 [](void *ctx, const char *screen, const char *id, const phasma::UiQuad *q) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         RuntimeUiSystem *ui = GetActiveRuntimeUi();
                         if (!ui || !screen || !*screen || !id || !*id || !q) return 0;
                         const auto color = [](phasma::Color c) { return RuntimeUiColor{c.r, c.g, c.b, c.a}; };
                         RuntimeUiQuadDesc d{};
                         d.label = q->label;
                         d.title = q->title;
                         d.subtitle = q->subtitle;
                         d.body = q->body;
                         d.footer = q->footer;
                         d.x = q->x;
                         d.y = q->y;
                         d.z = q->z;
                         d.width = q->width;
                         d.height = q->height;
                         d.fillColor = color(q->fill);
                         d.borderColor = color(q->border);
                         d.accentColor = color(q->accent);
                         d.textColor = color(q->textColor);
                         d.imageTint = color(q->imageTint);
                         d.backgroundImageTint = color(q->backgroundImageTint);
                         d.imageColorize = q->imageColorize;
                         d.useBackgroundTint = q->useBackgroundTint;
                         d.imageWhiten = q->imageWhiten;
                         d.cornerRadius = q->cornerRadius;
                         d.radialSegments = q->radialSegments;
                         d.radialFilled = q->radialFilled;
                         d.draggable = q->draggable;
                         d.selected = q->selected;
                         d.visible = q->visible;
                         d.bringToFront = q->bringToFront;
                         d.noInput = q->noInput;
                         d.fontScale = q->fontScale;
                         d.fit = q->fit;
                         d.textOffsetX = q->offsetX;
                         d.textOffsetY = q->offsetY;
                         d.textInsetRight = q->textInsetRight;
                         // Out-of-range enums from a module fall back to the defaults.
                         if (static_cast<uint32_t>(q->style) < static_cast<uint32_t>(RuntimeUiQuadVisualStyle::Count))
                             d.visualStyle = static_cast<RuntimeUiQuadVisualStyle>(q->style);
                         if (static_cast<uint32_t>(q->alignH) <= static_cast<uint32_t>(RuntimeUiTextAlignH::Right))
                             d.textAlignH = static_cast<RuntimeUiTextAlignH>(q->alignH);
                         if (static_cast<uint32_t>(q->alignV) <= static_cast<uint32_t>(RuntimeUiTextAlignV::Bottom))
                             d.textAlignV = static_cast<RuntimeUiTextAlignV>(q->alignV);
                         if (q->node)
                         {
                             NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(q->node);
                             if (!node) return 0;
                             GetActiveScene()->AddComponentFlag(node, Component_RuntimeUi);
                             d.node = node;
                         }
                         ui->SetQuad(screen, id, d, q->image ? q->image : "");
                         return 1; });
                 },
                 [](void *, const char *screen, const char *id) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         RuntimeUiSystem *ui = GetActiveRuntimeUi();
                         if (!ui || !screen || !id) return 0;
                         ui->RemoveWidget(screen, id);
                         return 1; });
                 },
                 [](void *, phasma::UiSurface *surface) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         if (!surface) return 0;
                         *surface = {};
                         RuntimeUiSystem *ui = GetActiveRuntimeUi();
                         if (!ui) return 0;
                         ui->GetFrameSurfaceSize(surface->width, surface->height);
                         surface->uiScale = ui->GetFrameUiScale();
                         surface->safeValid = ui->GetFrameSafeArea(surface->safeX, surface->safeY, surface->safeWidth, surface->safeHeight);
                         return surface->width > 0 && surface->height > 0; });
                 },
                 [](void *, const char *screen, const char *id, phasma::UiWidgetState *state) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         if (!state) return 0;
                         *state = {};
                         RuntimeUiSystem *ui = GetActiveRuntimeUi();
                         RuntimeUiWidgetState s{};
                         if (!ui || !screen || !id || !ui->GetWidgetState(screen, id, s)) return 0;
                         *state = {s.hovered, s.active, s.clicked, s.rightClicked, s.down, s.dragging, s.dragStarted,
                                   s.dragReleased, s.mouseX, s.mouseY, s.dragDeltaX, s.dragDeltaY};
                         return 1; });
                 },
                 [](void *) noexcept -> uint32_t
                 { return GuardApi([]() -> uint32_t
                                   { return InputState::IsLeftMouseDown(); }); },
                 [](void *ctx, phasma::Node handle, float speed) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         auto *animation = GetGlobalSystem<AnimationSystem>();
                         return node && std::isfinite(speed) && animation && SetSpeedTree(*GetActiveScene(), *animation, node, speed); });
                 },
                 [](void *ctx, phasma::Node handle, const char *clip, float *out) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         return node && clip && out && GetClipDurationTree(*GetActiveScene(), node, clip, *out); });
                 },
                 [](void *ctx, phasma::Node handle, uint32_t visible) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         if (!node) return 0;
                         GetActiveScene()->SetNodeRenderVisible(node, visible != 0);
                         return 1; });
                 },
                 [](void *ctx, phasma::Node handle, const char *boneName, phasma::Vec3 *out) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         if (!node || !boneName || !out) return 0;
                         Scene *scene = GetActiveScene();
                         // Same math as Lua animation.get_bone_position.
                         const auto &skeleton = scene->GetSkeletonForNode(node);
                         const int bone = skeleton.GetBoneIndex(boneName);
                         const auto &matrices = scene->GetNodeRuntime(node).jointMatrices;
                         if (bone < 0 || bone >= static_cast<int>(matrices.size())) return 0;
                         const vec3 p((matrices[bone] * glm::inverse(skeleton.bones[bone].offsetMatrix))[3]);
                         *out = {p.x, p.y, p.z};
                         return 1; });
                 },
                 [](void *ctx, phasma::Node handle, const char *name) noexcept -> phasma::Node
                 {
                     return GuardApi([&]() -> phasma::Node
                                     {
                         auto *self = static_cast<CppScriptSystem *>(ctx);
                         NodeId *root = self->Resolve(handle);
                         if (!root || !name || !*name) return 0;
                         return self->Handle(FindChildTree(*GetActiveScene(), root, name)); });
                 },
                 [](void *ctx, const char *name, float radius) noexcept -> phasma::Node
                 {
                     return GuardApi([&]() -> phasma::Node
                                     {
                         Scene *scene = GetActiveScene();
                         if (!scene || !name || !*name || !std::isfinite(radius) || radius <= 0.0f)
                             return 0;
                         NodeId *node = scene->CreateNode(name);
                         try { scene->AttachPrimitiveToNode(node, Primitives::CreateSphere(radius)); }
                         catch (...) { scene->DeleteNode(node); throw; }
                         return static_cast<CppScriptSystem *>(ctx)->Handle(node); });
                 },
                 [](void *ctx, phasma::Node handle, phasma::Color base, phasma::Vec3 emissive) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         if (!node || !Finite({base.r, base.g, base.b}) || !std::isfinite(base.a) || !Finite(emissive)) return 0;
                         Scene *scene = GetActiveScene();
                         const int meshIdx = scene->GetMeshRef(node);
                         if (meshIdx < 0) return 0;
                         // Same path as Lua material.set: a per-mesh instance over the shared material.
                         Mesh &mesh = scene->GetMesh(meshIdx);
                         if (!mesh.material) return 0;
                         MaterialInstance *inst = mesh.materialInstance ? mesh.materialInstance : scene->CreateMaterialInstance(mesh);
                         if (!inst) return 0;
                         const bool baseChanged = inst->SetBaseColorFactor(vec4(base.r, base.g, base.b, base.a));
                         if (inst->SetEmissiveFactor(vec3(emissive.x, emissive.y, emissive.z)) || baseChanged) scene->SetMaterialDirty();
                         return 1; });
                 },
                 [](void *, const char *clip) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     { return clip && *clip ? WithAudio([&](auto &audio)
                                                                        { audio.PlaySound(clip); })
                                                            : 0; });
                 },
                 [](void *, const char *clip) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     { return clip && *clip ? WithAudio([&](auto &audio)
                                                                        { audio.PlayMusic(clip); })
                                                            : 0; });
                 },
                 [](void *) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     { return WithAudio([](auto &audio)
                                                        { audio.StopMusic(); }); });
                 },
                 [](void *, const char *name, char *out, uint32_t capacity) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         if (!name || !out || capacity == 0) return 0;
                         out[0] = '\0';
                         const std::optional<std::string> value = ReadScriptLaunchOption(name);
                         if (!value || value->size() >= capacity) return 0;
                         std::memcpy(out, value->c_str(), value->size() + 1);
                         return 1; });
                 },
                 [](void *, const phasma::ParticleBurst *b) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         Scene *scene = GetActiveScene();
                         ParticleManager *pm = scene ? scene->GetParticleManager() : nullptr;
                         if (!pm || !b || !Finite(b->position) || !Finite(b->velocity) || !Finite(b->gravity))
                             return 0;
                         const float scalars[] = {b->sizeMin, b->sizeMax, b->lifeMin, b->lifeMax, b->spawnRadius,
                                                  b->noiseStrength, b->drag, b->cleanupDelay, b->colorStart.r,
                                                  b->colorStart.g, b->colorStart.b, b->colorStart.a, b->colorEnd.r,
                                                  b->colorEnd.g, b->colorEnd.b, b->colorEnd.a};
                         for (float f : scalars)
                             if (!std::isfinite(f)) return 0;
                         // particles.emit_burst: the preset, then each field that is set.
                         ParticleBurstDesc desc{};
                         if (b->preset) ParticleManager::FillBurstPreset(b->preset, desc);
                         desc.position = vec3(b->position.x, b->position.y, b->position.z);
                         const uint32_t set = b->set;
                         if (set & phasma::BurstCount) desc.count = b->count;
                         if (set & phasma::BurstSizeMin) desc.sizeMin = b->sizeMin;
                         if (set & phasma::BurstSizeMax) desc.sizeMax = b->sizeMax;
                         if (set & phasma::BurstLifeMin) desc.lifeMin = b->lifeMin;
                         if (set & phasma::BurstLifeMax) desc.lifeMax = b->lifeMax;
                         if (set & phasma::BurstSpawnRadius) desc.spawnRadius = b->spawnRadius;
                         if (set & phasma::BurstNoise) desc.noiseStrength = b->noiseStrength;
                         if (set & phasma::BurstDrag) desc.drag = b->drag;
                         if (set & phasma::BurstCleanupDelay) desc.cleanupDelay = b->cleanupDelay;
                         if (set & phasma::BurstVelocity) desc.velocity = vec3(b->velocity.x, b->velocity.y, b->velocity.z);
                         if (set & phasma::BurstGravity) desc.gravity = vec3(b->gravity.x, b->gravity.y, b->gravity.z);
                         if (set & phasma::BurstColorStart)
                             desc.colorStart = vec4(b->colorStart.r, b->colorStart.g, b->colorStart.b, b->colorStart.a);
                         if (set & phasma::BurstColorEnd)
                             desc.colorEnd = vec4(b->colorEnd.r, b->colorEnd.g, b->colorEnd.b, b->colorEnd.a);
                         return pm->EmitBurst(desc) >= 0; });
                 },
                 [](void *ctx, phasma::Node handle, const char *type) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         if (!node || !type) return 0;
                         Scene *scene = GetActiveScene();
                         return SetNodeRenderTypeByName(scene, node, scene->GetMeshRef(node), type); });
                 },
                 [](void *, const char *path, char *out, uint32_t capacity, uint32_t *size) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         if (size) *size = 0;
                         if (!path || !size) return 0;
                         const std::optional<std::string> content = ReadAssetsFile(path);
                         if (!content || content->empty() || content->size() > UINT32_MAX) return 0;
                         *size = static_cast<uint32_t>(content->size());
                         if (!out || content->size() > capacity) return 0;
                         std::memcpy(out, content->data(), content->size());
                         return 1; });
                 },
                 [](void *, const char *path, const char *data, uint32_t size) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     { return path && data && WriteAssetsFile(path, std::string_view(data, size), false) ? 1u : 0u; });
                 },
                 [](void *ctx, const char *name, float majorRadius, float minorRadius, uint32_t majorSegments,
                    uint32_t minorSegments) noexcept -> phasma::Node
                 {
                     return GuardApi([&]() -> phasma::Node
                                     {
                         Scene *scene = GetActiveScene();
                         if (!scene || !name || !*name || !std::isfinite(majorRadius) || !std::isfinite(minorRadius) ||
                             majorRadius <= 0.0f || minorRadius <= 0.0f || majorSegments < 3 || minorSegments < 3 ||
                             majorSegments > 512 || minorSegments > 512)
                             return 0;
                         NodeId *node = scene->CreateNode(name);
                         try
                         {
                             scene->AttachPrimitiveToNode(node, Primitives::CreateTorus(majorRadius, minorRadius,
                                                                                        static_cast<int>(majorSegments),
                                                                                        static_cast<int>(minorSegments)));
                         }
                         catch (...)
                         {
                             scene->DeleteNode(node);
                             throw;
                         }
                         return static_cast<CppScriptSystem *>(ctx)->Handle(node); });
                 },
                 [](void *ctx, const char *name, const phasma::FullscreenPass *pass) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         if (!name || !*name || !pass || !pass->shader || !*pass->shader ||
                             !std::isfinite(pass->thicknessAt1080) || !std::isfinite(pass->minThickness))
                             return 0;
                         for (float f : pass->params)
                             if (!std::isfinite(f)) return 0;
                         return static_cast<CppScriptSystem *>(ctx)->AddFullscreenPass(name, *pass); });
                 },
                 [](void *ctx, const char *name) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         if (!name || !*name) return 0;
                         return static_cast<CppScriptSystem *>(ctx)->RemoveFullscreenPass(name); });
                 },
                 [](void *, phasma::AudioBus bus, float value) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         if (static_cast<uint32_t>(bus) > static_cast<uint32_t>(phasma::AudioBus::Ambient) ||
                             !std::isfinite(value))
                             return 0;
                         value = std::clamp(value, 0.0f, 1.0f);
                         return WithAudio([&](auto &audio)
                                          {
                             switch (bus)
                             {
                             case phasma::AudioBus::Master: audio.SetMasterVolume(value); break;
                             case phasma::AudioBus::Music: audio.SetMusicVolume(value); break;
                             case phasma::AudioBus::Sfx: audio.SetSFXVolume(value); break;
                             case phasma::AudioBus::Ambient: audio.SetAmbientVolume(value); break;
                             } }); });
                 },
                 [](void *, phasma::AudioBus bus, float *value) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         if (!value || static_cast<uint32_t>(bus) > static_cast<uint32_t>(phasma::AudioBus::Ambient))
                             return 0;
                         return WithAudio([&](auto &audio)
                                          {
                             const float volumes[] = {audio.GetMasterVolume(), audio.GetMusicVolume(),
                                                      audio.GetSFXVolume(), audio.GetAmbientVolume()};
                             *value = volumes[static_cast<uint32_t>(bus)]; }); });
                 },
                 [](void *, phasma::UiStyle style, const char *image) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         RuntimeUiSystem *ui = GetActiveRuntimeUi();
                         if (!ui || !image ||
                             static_cast<uint32_t>(style) >= static_cast<uint32_t>(RuntimeUiQuadVisualStyle::Count))
                             return 0;
                         ui->SetStyleBackground(static_cast<RuntimeUiQuadVisualStyle>(style), image);
                         return 1; });
                 },
                 [](void *ctx, phasma::Node handle, const phasma::NodeUi *in) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         NodeRuntimeUiTag *ui = node ? GetActiveScene()->GetRuntimeUiComponent(node) : nullptr;
                         if (!ui || !in) return 0;
                         const phasma::NodeUi &u = *in;
                         const auto has = [&](uint32_t bit) { return (u.set & bit) != 0; };
                         const auto finite = [](const phasma::Color &c)
                         { return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a); };
                         // Validate everything first so a refused call changes nothing.
                         const struct { uint32_t bit; const char *text; } strings[] = {
                             {phasma::NodeUiBody, u.body},       {phasma::NodeUiTitle, u.title},
                             {phasma::NodeUiSubtitle, u.subtitle}, {phasma::NodeUiFooter, u.footer},
                             {phasma::NodeUiLabel, u.label},     {phasma::NodeUiImage, u.image}};
                         for (const auto &s : strings)
                             if (has(s.bit) && !s.text) return 0;
                         const struct { uint32_t bit; const phasma::Color *color; vec4 *dst; } colors[] = {
                             {phasma::NodeUiFill, &u.fill, &ui->fillColor},
                             {phasma::NodeUiBorder, &u.border, &ui->borderColor},
                             {phasma::NodeUiAccent, &u.accent, &ui->accentColor},
                             {phasma::NodeUiTextColor, &u.textColor, &ui->textColor},
                             {phasma::NodeUiImageTint, &u.imageTint, &ui->imageTint},
                             {phasma::NodeUiBackgroundImageTint, &u.backgroundImageTint, &ui->backgroundImageTint}};
                         for (const auto &c : colors)
                             if (has(c.bit) && !finite(*c.color)) return 0;
                         if ((has(phasma::NodeUiFontScale) && !std::isfinite(u.fontScale)) ||
                             (has(phasma::NodeUiImageWhiten) && !std::isfinite(u.imageWhiten)) ||
                             (has(phasma::NodeUiOffset) && !(std::isfinite(u.offsetX) && std::isfinite(u.offsetY))) ||
                             (has(phasma::NodeUiAlignH) && static_cast<uint32_t>(u.alignH) > 3u) ||
                             (has(phasma::NodeUiAlignV) && static_cast<uint32_t>(u.alignV) > 3u))
                             return 0;
                         if (has(phasma::NodeUiBody)) ui->body = u.body;
                         if (has(phasma::NodeUiTitle)) ui->title = u.title;
                         if (has(phasma::NodeUiSubtitle)) ui->subtitle = u.subtitle;
                         if (has(phasma::NodeUiFooter)) ui->footer = u.footer;
                         if (has(phasma::NodeUiLabel)) ui->label = u.label;
                         if (has(phasma::NodeUiImage)) ui->imagePath = u.image;
                         for (const auto &c : colors)
                             if (has(c.bit)) *c.dst = vec4(c.color->r, c.color->g, c.color->b, c.color->a);
                         if (has(phasma::NodeUiFontScale)) ui->fontScale = u.fontScale;
                         if (has(phasma::NodeUiImageWhiten)) ui->imageWhiten = std::clamp(u.imageWhiten, -1.0f, 1.0f);
                         if (has(phasma::NodeUiAlignH)) ui->textAlignH = static_cast<uint8_t>(u.alignH);
                         if (has(phasma::NodeUiAlignV)) ui->textAlignV = static_cast<uint8_t>(u.alignV);
                         if (has(phasma::NodeUiOffset)) ui->textOffset = vec2(u.offsetX, u.offsetY);
                         if (has(phasma::NodeUiVisible)) ui->visible = u.visible;
                         if (has(phasma::NodeUiNoInput)) ui->noInput = u.noInput;
                         if (has(phasma::NodeUiUseBackgroundTint)) ui->useBackgroundTint = u.useBackgroundTint;
                         return 1; });
                 },
                 [](void *ctx, phasma::Node handle, uint32_t enabled) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         if (!node) return 0;
                         GetActiveScene()->SetNodeEnabled(node, enabled != 0);
                         return 1; });
                 },
                 [](void *ctx, phasma::Node handle) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         return node && GetActiveScene()->IsNodeEnabled(node) ? 1u : 0u; });
                 },
                 [](void *ctx, phasma::Node handle, phasma::UiRect *rect) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         RuntimeUiSystem *ui = GetActiveRuntimeUi();
                         if (!node || !rect || !ui) return 0;
                         float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
                         if (!ui->GetNodeRect(node, x, y, w, h)) return 0;
                         *rect = {x, y, w, h};
                         return 1; });
                 },
                 [](void *ctx, phasma::Node handle, phasma::Vec3 *scale) noexcept -> uint32_t
                 {
                     return GuardApi([&]() -> uint32_t
                                     {
                         NodeId *node = static_cast<CppScriptSystem *>(ctx)->Resolve(handle);
                         if (!node || !scale) return 0;
                         const mat4 m = GetActiveScene()->GetLocalMatrix(node);
                         *scale = {glm::length(vec3(m[0])), glm::length(vec3(m[1])), glm::length(vec3(m[2]))};
                         return 1; });
                 }};
    }

    void CppScriptSystem::Stop(Instance &instance)
    {
        // Even failed instances own state that must die while their module is loaded; faulted state is leaked.
        uint32_t fault = 0;
        if (instance.started && !instance.faulted)
            GuardedDestroy(instance.script->destroy, instance.state, &fault);
        if (fault)
            ScriptError(instance.script->name, fault);
        instance.state = nullptr;
        instance.started = instance.failed = instance.faulted = false;
    }

    void CppScriptSystem::ClearInstances()
    {
        for (auto &instance : m_instances)
            Stop(instance);
        m_instances.clear();
    }

    void CppScriptSystem::Destroy()
    {
        ClearInstances();
        ClearFullscreenPasses();
        m_module.Reset();
        m_handles.Clear();
        m_sceneGeneration = m_scriptGeneration = UINT32_MAX;
        m_initialized = false;
    }

    uint32_t CppScriptSystem::AddFullscreenPass(const std::string &name, const phasma::FullscreenPass &cfg)
    {
        FullscreenPassEntry &entry = m_fullscreenPasses[name];
        // Re-added with another shader, or after its shader failed: rebuilt on next use.
        if (entry.pass && (entry.failed || entry.shaderPath != cfg.shader))
        {
            RHII.WaitDeviceIdle();
            delete entry.pass;
            entry.pass = nullptr;
        }
        entry.failed = false;
        entry.shaderPath = cfg.shader;
        entry.thicknessAt1080 = cfg.thicknessAt1080;
        entry.minThickness = cfg.minThickness;
        std::memcpy(entry.params, cfg.params, sizeof(entry.params));

        RegisterScriptRenderPass(
            name, cfg.order,
            [this, name](CommandBuffer *cmd)
            {
                auto it = m_fullscreenPasses.find(name);
                if (it == m_fullscreenPasses.end() || it->second.failed)
                    return;
                // A shader that fails to load must not take the host down; Lua's protected call
                // catches the same for render_graph passes.
                try
                {
                    RecordFullscreenPass(name, it->second, cmd);
                }
                catch (const std::exception &error)
                {
                    it->second.failed = true;
                    Log::Error("[CppScript] fullscreen pass '" + name + "' failed; pass disabled: " + error.what());
                }
            },
            this);
        return 1;
    }

    void CppScriptSystem::RecordFullscreenPass(const std::string &name, FullscreenPassEntry &e, CommandBuffer *cmd)
    {
        // Resolved every frame: render targets are recreated on resize.
        SceneRendererHost *host = GetActiveSceneRendererHost();
        if (!host)
            return;
        Image *target = host->GetRenderTarget("viewport");
        Image *depth = host->GetDepthStencilTarget("depthStencil");
        Image *normal = host->GetRenderTarget("normal");
        if (!target || !depth || !normal)
            return;
        if (!e.pass)
        {
            e.pass = new PassInfo();
            e.pass->name = name;
            e.pass->pVertShader = Shader::Create({.sourcePath = Path::ResolveAsset(e.shaderPath),
                                                  .entryPoint = "mainVS",
                                                  .stage = PE_SHADER_STAGE_VERTEX});
            e.pass->pFragShader = Shader::Create({.sourcePath = Path::ResolveAsset(e.shaderPath),
                                                  .entryPoint = "mainPS",
                                                  .stage = PE_SHADER_STAGE_FRAGMENT});
            e.pass->colorFormats = {target->GetFormat()};
            e.pass->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
            e.pass->cullMode = PE_CULL_MODE_NONE;
            e.pass->depthTestEnable = false;
            e.pass->depthWriteEnable = false;
            e.pass->blendEnable = true;
            e.pass->colorBlendAttachments = {BlendState::Default};
            e.pass->Update();
        }
        const auto &descs = e.pass->GetDescriptors(RHII.GetFrameIndex());
        if (descs.empty())
            return;
        Descriptor *desc = descs[0];
        desc->SetImageView(0, depth->GetSRV(), depth->GetSampler());
        desc->SetImageView(1, normal->GetSRV(), normal->GetSampler());
        desc->Update();
        ImageBarrierInfo depthBarrier{};
        depthBarrier.image = depth;
        depthBarrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        depthBarrier.stageFlags = PE_STAGE_FRAGMENT_SHADER;
        depthBarrier.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
        cmd->ImageBarrier(depthBarrier);
        ImageBarrierInfo normalBarrier = depthBarrier;
        normalBarrier.image = normal;
        cmd->ImageBarrier(normalBarrier);
        const float w = target->GetWidth_f(), h = target->GetHeight_f();
        Attachment att{};
        att.image = target;
        att.loadOp = PE_LOAD_OP_LOAD;
        att.storeOp = PE_STORE_OP_STORE;
        cmd->BeginPass(1, &att, name);
        cmd->BindPipeline(*e.pass);
        cmd->SetViewport(0, 0, w, h, 0.0f, 1.0f);
        cmd->SetScissor(0, 0, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        Scene *scene = GetActiveScene();
        Camera *camera = scene ? scene->GetActiveCamera() : nullptr;
        const float nearPlane = camera ? camera->GetNearPlane() : 0.0f;
        const float thickness = std::max(e.minThickness, e.thicknessAt1080 * h / 1080.0f);
        cmd->SetConstantAt(0, vec4(1.0f / w, 1.0f / h, thickness, nearPlane));
        cmd->SetConstantAt(4, vec4(e.params[0], e.params[1], e.params[2], e.params[3]));
        cmd->PushConstants();
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();
    }

    uint32_t CppScriptSystem::RemoveFullscreenPass(const std::string &name)
    {
        auto it = m_fullscreenPasses.find(name);
        if (it == m_fullscreenPasses.end())
            return 0;
        UnregisterScriptRenderPass(name);
        RHII.WaitDeviceIdle();
        delete it->second.pass;
        m_fullscreenPasses.erase(it);
        return 1;
    }

    void CppScriptSystem::ClearFullscreenPasses()
    {
        if (m_fullscreenPasses.empty())
            return;
        ClearScriptRenderPasses(this);
        RHII.WaitDeviceIdle();
        for (auto &[name, entry] : m_fullscreenPasses)
            delete entry.pass;
        m_fullscreenPasses.clear();
    }

    std::vector<std::string> CppScriptSystem::ListNodeScripts() const
    {
        std::vector<std::string> names;
        if (const auto *module = m_module.Active())
            for (uint32_t i = 0; i < module->scriptCount; ++i)
                if (module->scripts[i].kind == phasma::ScriptKind::Node)
                    names.emplace_back(module->scripts[i].name);
        return names;
    }

    std::string CppScriptSystem::SourceFile(const std::string &path) const
    {
        if (const auto *module = m_module.Active())
            for (uint32_t i = 0; i < module->scriptCount; ++i)
                if (module->scripts[i].sourceFile && MatchesScript(module->scripts[i], path))
                    return module->scripts[i].sourceFile;
        return {};
    }

    CppScriptStatus CppScriptSystem::Status() const
    {
        return m_module.Status();
    }

    bool CppScriptSystem::Eligible(const Instance &instance) const
    {
        const bool play = !IsEditorHost() || IsScriptPlayMode();
        if (instance.script->kind == phasma::ScriptKind::Global)
            return instance.script->mode == phasma::ScriptMode::Always ||
                   instance.script->mode == (play ? phasma::ScriptMode::Play : phasma::ScriptMode::Editor);
        Scene *scene = GetActiveScene();
        if (!scene || !instance.node.IsValid(*scene))
            return false;
        const auto mode = scene->GetNodeScriptRunMode(instance.node.nodeId);
        return mode == ScriptRunMode::Both || mode == (play ? ScriptRunMode::Player : ScriptRunMode::Editor);
    }

    void CppScriptSystem::StopPlay()
    {
        // Called before the editor restores its scene snapshot; passes a script registered must not
        // survive the snapshot restore, so they are cleared here too (not just on full teardown).
        ClearFullscreenPasses();
        for (auto &instance : m_instances)
        {
            bool playOnly = instance.script->mode == phasma::ScriptMode::Play;
            if (instance.script->kind == phasma::ScriptKind::Node)
            {
                Scene *scene = GetActiveScene();
                playOnly = !scene || !instance.node.IsValid(*scene) ||
                           scene->GetNodeScriptRunMode(instance.node.nodeId) == ScriptRunMode::Player;
            }
            if (playOnly)
                Stop(instance);
        }
    }

    void CppScriptSystem::Reconcile()
    {
        Scene *scene = GetActiveScene();
        const auto generation = scene ? scene->GetGeneration() : 0;
        if (generation != m_sceneGeneration)
        {
            ClearInstances();
            m_handles.Clear();
            m_sceneGeneration = generation;
            m_scriptGeneration = UINT32_MAX;
            if (const auto *module = m_module.Active())
                for (uint32_t i = 0; i < module->scriptCount; ++i)
                    if (module->scripts[i].kind == phasma::ScriptKind::Global)
                        m_instances.push_back({&module->scripts[i]});
        }
        if (!scene || scene->GetScriptAttachGeneration() == m_scriptGeneration)
            return;
        m_scriptGeneration = scene->GetScriptAttachGeneration();
        std::unordered_set<NodeId *> attached;
        for (auto it = m_instances.begin(); it != m_instances.end();)
        {
            if (it->script->kind == phasma::ScriptKind::Global)
            {
                ++it;
                continue;
            }
            if (!it->node.IsValid(*scene) ||
                !MatchesScript(*it->script, scene->GetNodeScriptPath(it->node.nodeId)))
            {
                Stop(*it);
                it = m_instances.erase(it);
            }
            else
            {
                attached.insert(it->node.nodeId);
                ++it;
            }
        }
        std::unordered_map<std::string, const phasma::ScriptDesc *> scripts;
        if (const auto *module = m_module.Active())
            for (uint32_t i = 0; i < module->scriptCount; ++i)
                if (module->scripts[i].kind == phasma::ScriptKind::Node)
                {
                    scripts.emplace(module->scripts[i].name, &module->scripts[i]);
                    if (module->scripts[i].sourceFile)
                        scripts.emplace(CppSourceName(module->scripts[i].sourceFile), &module->scripts[i]);
                }
        for (uint32_t i = 0; i < scene->GetNodeCount(); ++i)
        {
            NodeId *node = scene->GetNodeId(i);
            if (attached.count(node) || !(scene->GetComponentFlags(node) & Component_Script))
                continue;
            const auto &path = scene->GetNodeScriptPath(node);
            if (!IsCppScriptPath(path))
                continue;
            const auto script = scripts.find(CppScriptKey(path));
            if (script != scripts.end())
                m_instances.push_back({script->second, scene->MakeHandle(node)});
        }
    }

    void CppScriptSystem::Update(double dt)
    {
#if defined(PE_PROJECT_NATIVE)
#if !defined(PE_PROJECT_NATIVE_STATIC)
        // Editors and build-folder Players live-reload; exported Players load the installed module in place once.
        const bool liveReload = ProjectNativeModule::LiveReloadEnabled(IsEditorHost(), Path::Executable);
        // This runs between dispatches, never while module code is on the stack.
        const bool loaded = m_module.Sync(std::filesystem::path(Path::Executable) / ProjectNativeModule::ModuleFileName(), liveReload);
        if (m_module.NoticeCount() != m_loggedNotices && !m_module.Notice().empty()) // repeats are logged too
            Log::Warn("[CppScript] " + m_module.Notice());
        m_loggedNotices = m_module.NoticeCount();
        const auto status = m_module.Status();
        if (loaded)
        {
            ClearInstances();
            m_module.Commit();
            m_sceneGeneration = UINT32_MAX;
            Log::Info(status.liveReload && liveReload ? "[CppScript] module reloaded; private script state reset" : "[CppScript] module loaded");
        }
        else if (status.attempts != m_loggedAttempts && !m_module.Error().empty()) // every rejected attempt, even with an identical error
            Log::Error(std::string(m_module.Active() ? "[CppScript] replacement rejected; keeping current module: "
                                                     : "[CppScript] module rejected: ") +
                       m_module.Error());
        m_loggedAttempts = status.attempts;
#endif
        if (!m_module.Active())
            return;
        Reconcile();
        for (auto &instance : m_instances)
        {
            if (!Eligible(instance))
            {
                if (instance.started)
                    Stop(instance);
                continue;
            }
            if (instance.failed || (IsScriptPlayMode() && IsScriptPaused()))
                continue;
            if (!instance.started)
            {
                const auto node = instance.script->kind == phasma::ScriptKind::Node ? Handle(instance.node.nodeId) : 0;
                instance.started = true;
                uint32_t fault = 0;
                if (!GuardedCreate(instance.script->create, &m_api, node, &instance.state, &fault))
                {
                    instance.failed = true;
                    instance.faulted = fault != 0;
                    ScriptError(instance.script->name, fault);
                    continue;
                }
            }
            uint32_t fault = 0;
            if (!GuardedUpdate(instance.script->update, instance.state, dt, &fault))
            {
                instance.failed = true;
                instance.faulted = fault != 0;
                ScriptError(instance.script->name, fault);
            }
        }
        m_handles.Maintain();
#endif
    }
} // namespace pe
