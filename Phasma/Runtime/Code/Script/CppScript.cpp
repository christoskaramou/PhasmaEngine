#include "CppScript.h"
#include "Base/GamePack.h"
#include "Base/Log.h"
#include "Base/Path.h"
#include "Camera/Camera.h"
#include "Scene/Primitives.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
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

        // Rebuilds the local TRS, replacing rotation (degrees) or scale; same math as the Lua node bindings.
        void SetLocalTrs(Scene &scene, NodeId *node, const vec3 *rotationDegrees, const vec3 *scale)
        {
            const mat4 &m = scene.GetLocalMatrix(node);
            const vec3 oldScale(glm::length(vec3(m[0])), glm::length(vec3(m[1])), glm::length(vec3(m[2])));
            const quat rotation = rotationDegrees ? quat(glm::radians(*rotationDegrees))
                                                  : glm::quat_cast(mat3(vec3(m[0]) / oldScale.x, vec3(m[1]) / oldScale.y, vec3(m[2]) / oldScale.z));
            scene.SetLocalMatrix(node, glm::translate(mat4(1.0f), vec3(m[3])) * glm::mat4_cast(rotation) *
                                           glm::scale(mat4(1.0f), scale ? *scale : oldScale));
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

        void ScriptError(const char *name)
        {
            Log::Error(std::string("[CppScript] callback failed; instance disabled: ") + name);
        }

        std::string ScriptKey(const std::string &path)
        {
            return path.rfind("cpp:", 0) == 0 ? path.substr(4) : std::filesystem::path(path).filename().string();
        }

        bool MatchesScript(const phasma::ScriptDesc &script, const std::string &path)
        {
            return path == std::string("cpp:") + script.name ||
                   (script.sourceFile && ScriptKey(path) == std::filesystem::path(script.sourceFile).filename().string());
        }
    } // namespace

    CppScriptSystem::~CppScriptSystem()
    {
        Destroy();
    }

    phasma::Node CppScriptSystem::Handle(NodeId *node)
    {
        Scene *scene = GetActiveScene();
        if (!scene || !node || !scene->IsNodeAlive(node))
            return 0;
        auto it = m_nodeHandles.find(node);
        if (it != m_nodeHandles.end() && m_handles.at(it->second).IsValid(*scene))
            return it->second;
        const auto id = m_nextHandle++;
        m_handles.emplace(id, scene->MakeHandle(node));
        m_nodeHandles[node] = id;
        return id;
    }

    NodeId *CppScriptSystem::Resolve(phasma::Node handle)
    {
        Scene *scene = GetActiveScene();
        const auto it = m_handles.find(handle);
        return scene && it != m_handles.end() && it->second.IsValid(*scene) ? it->second.nodeId : nullptr;
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
                         self->m_handles.erase(handle);
                         self->m_nodeHandles.erase(node);
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
        // Even failed instances own state that must die while their module is loaded.
        if (instance.started)
            instance.script->destroy(instance.state);
        instance.state = nullptr;
        instance.started = instance.failed = false;
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
        m_handles.clear();
        m_nodeHandles.clear();
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
            m_handles.clear();
            m_nodeHandles.clear();
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
                        scripts.emplace(std::filesystem::path(module->scripts[i].sourceFile).filename().string(), &module->scripts[i]);
                }
        for (uint32_t i = 0; i < scene->GetNodeCount(); ++i)
        {
            NodeId *node = scene->GetNodeId(i);
            if (attached.count(node) || !(scene->GetComponentFlags(node) & Component_Script))
                continue;
            const auto &path = scene->GetNodeScriptPath(node);
            if (!IsCppScriptPath(path))
                continue;
            const auto script = scripts.find(ScriptKey(path));
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
        // Only the editor live-reloads. Players load the installed module in place once.
        const bool liveReload = IsEditorHost();
        // This runs between dispatches, never while module code is on the stack.
        if (m_module.Sync(std::filesystem::path(Path::Executable) / moduleName, liveReload))
        {
            ClearInstances();
            m_module.Commit();
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
                if (!instance.script->create(&m_api, node, &instance.state))
                {
                    instance.failed = true;
                    ScriptError(instance.script->name);
                    continue;
                }
            }
            if (!instance.script->update(instance.state, dt))
            {
                instance.failed = true;
                ScriptError(instance.script->name);
            }
        }
#endif
    }
} // namespace pe
