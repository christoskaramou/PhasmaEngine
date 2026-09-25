#pragma once
#include "ProjectNativeHooks.h"
#include "Scene/SceneNodeHandle.h"
#include <unordered_map>
#include <vector>

namespace pe
{
    inline bool IsCppScriptPath(const std::string &path)
    {
        return path.rfind("cpp:", 0) == 0 || std::filesystem::path(path).extension() == ".cpp";
    }

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

    private:
        struct Instance
        {
            const phasma::ScriptDesc *script = nullptr;
            SceneNodeHandle node;
            void *state = nullptr;
            bool started = false;
            bool failed = false;
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
        std::unordered_map<phasma::Node, SceneNodeHandle> m_handles;
        std::unordered_map<NodeId *, phasma::Node> m_nodeHandles;
        phasma::Node m_nextHandle = 1;
        uint32_t m_sceneGeneration = UINT32_MAX;
        uint32_t m_scriptGeneration = UINT32_MAX;
        std::string m_lastError;
        bool m_initialized = false;
    };
} // namespace pe
