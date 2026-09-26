#include "CppScript.h"
#include "Base/GamePack.h"
#include "Base/Log.h"
#include "Base/Path.h"
#include "Camera/Camera.h"
#include "Scene/Primitives.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Script/NativeScriptGuard.h"
#include "Script/NativeTrs.h"
#include "Script/ScriptRuntimeHooks.h"
#include "Script/Bindings/Input/InputState.h"
#include "Systems/AnimationSystem.h"
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
        m_module.Reset();
        m_handles.Clear();
        m_sceneGeneration = m_scriptGeneration = UINT32_MAX;
        m_initialized = false;
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
        // Called before the editor restores its scene snapshot.
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
