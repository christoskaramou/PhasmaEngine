#pragma once
#include "CppScriptPath.h"
#include "NativeHandleTable.h"
#include "ProjectNativeHooks.h"
#include "Scene/SceneNodeHandle.h"
#include <unordered_map>
#include <vector>

namespace pe
{
    struct CppScriptStatus
    {
        uint64_t reloads = 0;
        bool active = false;
        uint32_t scriptCount = 0;
        std::string error;
    };

    class CppScriptSystem
    {
    public:
        ~CppScriptSystem();
        void Init();
        void Update(double dt);
        void StopPlay();
        void Destroy();
        std::vector<std::string> ListNodeScripts() const;
        std::string SourceFile(const std::string &path) const;
        CppScriptStatus Status() const;

    private:
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
        uint64_t m_reloads = 0;
        uint32_t m_sceneGeneration = UINT32_MAX;
        uint32_t m_scriptGeneration = UINT32_MAX;
        std::string m_lastError;
        bool m_initialized = false;
    };
} // namespace pe
