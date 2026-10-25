#pragma once

#include <array>
#include <map>
#include <memory>
#include <vector>

#include "RenderPasses/ModulePipelines.h"

namespace pe
{
    class Buffer;
    class CommandBuffer;
    class PassInfo;
    class Scene;

    // Each raster pass owns its pipelines/descriptors; pass Update() runs concurrently.
    class OpenPBRMask : public ModulePipelines
    {
    public:
        void Sync(Scene &scene, bool shadow = false);
        bool IsActive() const { return m_active; } // synced for the scene's mask modules since the last Clear
        std::vector<std::function<void(PassInfo &)>> ModuleBuilders(uint32_t module) const override;
        void AdoptModule(uint32_t module, const std::vector<std::shared_ptr<PassInfo>> &infos) override;
        bool HasModule(uint32_t module) const override { return m_modules.contains(module); }
        void Update(Scene &scene);
        void Rebuild();
        void AppendPassInfos(std::vector<PassInfo *> &infos) const;
        void Prepare(CommandBuffer *cmd, Scene &scene) const;
        void Draw(CommandBuffer *cmd, Scene &scene, Buffer *indirect, Buffer *counters,
                  uint32_t counter, uint32_t capacity, bool doubleSided, const mat4 *lightVP = nullptr) const;
        void Clear();

    private:
        std::function<void(PassInfo &)> Builder(uint32_t module, bool doubleSided) const;
        void BindFrame(PassInfo &info, Scene &scene);

        bool m_shadow = false;
        bool m_active = false;
        std::map<uint32_t, std::array<std::shared_ptr<PassInfo>, 2>> m_modules;
    };
} // namespace pe
