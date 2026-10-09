#pragma once
#include "CppScriptPath.h"
#include "NativeHandleTable.h"
#include "ProjectNativeHooks.h"
#include "Scene/SceneNodeHandle.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace pe
{
    class CppScriptSystem
    {
    public:
        ~CppScriptSystem();
        void Init();
        void Update(double dt);
        void StopPlay();
        void Destroy();
        std::vector<std::string> ListScripts(phasma::ScriptKind kind) const;
        std::string SourceFile(const std::string &path) const;
        CppScriptStatus Status() const;
        uint32_t AddFullscreenPass(const std::string &name, const phasma::FullscreenPass &cfg);
        uint32_t RemoveFullscreenPass(const std::string &name);
        // The scene's pipeline script (ScriptKind::Pipeline, referenced as cpp:<name>); false when the module has none of
        // that name. The fullscreen passes it adds belong to owner (ScriptSystem's pipeline), so Play Stop keeps them.
        bool CreatePipeline(const std::string &reference, const void *owner);
        void DestroyPipeline();
        bool HasModule() const;
        uint64_t ModuleLoads() const { return m_moduleLoads; } // bumped each time a module is (re)loaded
        // ScriptSystem's pipeline values, which the pipeline's exposeNumber / exposeBool / exposeBase reach.
        std::function<double(const char *name, double fallback, const char *shownWhen, bool isBool)> pipelineExpose;
        std::function<void()> pipelineExposeBase;

    private:
        void ClearFullscreenPasses();
        struct Instance
        {
            const phasma::ScriptDesc *script = nullptr;
            SceneNodeHandle node;
            void *state = nullptr;
            bool started = false;
            bool failed = false;
            bool faulted = false; // hardware fault: state is leaked, never destroyed
        };
        struct LiveNode
        {
            bool operator()(const SceneNodeHandle &handle) const;
        };
        phasma::Node Handle(NodeId *node);
        NodeId *Resolve(phasma::Node handle);
        void Reconcile();
        void ClearInstances();
        static void Stop(Instance &instance);
        bool Eligible(const Instance &instance) const;
        ProjectNativeModule m_module;
        phasma::ScriptApi m_api{};
        std::vector<Instance> m_instances;
        NativeHandleTable<NodeId *, SceneNodeHandle, LiveNode> m_handles;
        uint64_t m_loggedAttempts = 0;
        uint32_t m_sceneGeneration = UINT32_MAX;
        uint32_t m_scriptGeneration = UINT32_MAX;
        uint64_t m_loggedNotices = 0;
        bool m_initialized = false;
        std::string m_pendingScene; // loadScene, applied after the update loop
        Instance m_pipeline;
        const void *m_pipelineOwner = nullptr; // set only while the pipeline's create runs
        uint64_t m_moduleLoads = 0;
    };
} // namespace pe
