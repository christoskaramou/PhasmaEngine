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
        {
            m_module.Commit();
            ++m_reloads;
        }
        else
        {
            m_lastError = m_module.Error();
            Log::Error("[CppScript] linked module rejected: " + m_lastError);
        }
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
        m_lastError.clear();
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
        const auto *module = m_module.Active();
        return {m_reloads, module != nullptr, module ? module->scriptCount : 0u, m_lastError};
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
#if defined(_WIN32)
        constexpr const char *moduleName = "PhasmaGame.dll";
#elif defined(__APPLE__)
        constexpr const char *moduleName = "libPhasmaGame.dylib";
#else
        constexpr const char *moduleName = "libPhasmaGame.so";
#endif
        // Editors and build-folder Players live-reload; exported Players load the installed module in place once.
        const bool liveReload = ProjectNativeModule::LiveReloadEnabled(IsEditorHost(), Path::Executable);
        // This runs between dispatches, never while module code is on the stack.
        if (m_module.Sync(std::filesystem::path(Path::Executable) / moduleName, liveReload))
        {
            ClearInstances();
            m_module.Commit();
            ++m_reloads;
            m_sceneGeneration = UINT32_MAX;
            m_lastError.clear();
            Log::Info(liveReload ? "[CppScript] module reloaded; private script state reset" : "[CppScript] module loaded");
        }
        else if (!m_module.Error().empty() && m_module.Error() != m_lastError)
        {
            m_lastError = m_module.Error();
            Log::Error(std::string(liveReload ? "[CppScript] replacement rejected; keeping current module: "
                                              : "[CppScript] module rejected: ") +
                       m_lastError);
        }
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
