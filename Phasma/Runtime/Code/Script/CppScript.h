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
    class CommandBuffer;
    class PassInfo;

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
        uint32_t AddFullscreenPass(const std::string &name, const phasma::FullscreenPass &cfg);
        uint32_t RemoveFullscreenPass(const std::string &name);

    private:
        struct FullscreenPassEntry
        {
            PassInfo *pass = nullptr;
            std::string shaderPath;
            float thicknessAt1080 = 1.0f, minThickness = 0.0f;
            float params[4] = {};
            bool failed = false; // its shader failed; skipped until re-added
        };
        void RecordFullscreenPass(const std::string &name, FullscreenPassEntry &entry, CommandBuffer *cmd);
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
        std::unordered_map<std::string, FullscreenPassEntry> m_fullscreenPasses;
    };
} // namespace pe
