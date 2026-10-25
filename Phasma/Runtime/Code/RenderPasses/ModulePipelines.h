#pragma once

#include "API/Command.h"
#include "API/RHI.h"

namespace pe
{
    class PassInfo;

    // Pipelines a pass keeps per MaterialX module. SceneRendererCore::SyncModulePipelines builds a new module's on a
    // worker thread and hands them to AdoptModule, unless the pass rebuilt or released its pipelines in between: those
    // bump the generation.
    class ModulePipelines
    {
    public:
        // One builder per PassInfo the module needs. Each copies what it reads, so it can fill a fresh PassInfo on the
        // worker; the pass's own rebuilds run them in place.
        virtual std::vector<std::function<void(PassInfo &)>> ModuleBuilders(uint32_t module) const = 0;
        virtual void AdoptModule(uint32_t module, const std::vector<std::shared_ptr<PassInfo>> &infos) = 0;
        virtual bool HasModule(uint32_t module) const = 0;
        uint64_t GetModuleGeneration() const { return m_moduleGeneration; }

    protected:
        ~ModulePipelines() = default;

        uint64_t m_moduleGeneration = 0;
    };

    // In-flight frames still use a dropped module's pipelines and descriptor sets: retire both with the frame. A module
    // that comes back must create pipelines of its own, not reuse cached ones that reference the retired PassInfo.
    inline void RetireModulePassInfo(const std::shared_ptr<PassInfo> &info)
    {
        if (!info)
            return;

        CommandBuffer::RetirePipelines(*info);
        RHII.AddToDeletionQueue([retired = info] {});
    }
} // namespace pe
