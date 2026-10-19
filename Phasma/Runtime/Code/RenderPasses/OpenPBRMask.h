#pragma once

#include <array>
#include <map>
#include <memory>
#include <vector>

namespace pe
{
    class Buffer;
    class CommandBuffer;
    class PassInfo;
    class Scene;

    // Each raster pass owns its pipelines/descriptors; pass Update() runs concurrently.
    class OpenPBRMask
    {
    public:
        void Sync(Scene &scene, bool shadow = false);
        void Update(Scene &scene);
        void Rebuild();
        void AppendPassInfos(std::vector<PassInfo *> &infos) const;
        void Prepare(CommandBuffer *cmd, Scene &scene) const;
        void Draw(CommandBuffer *cmd, Scene &scene, Buffer *indirect, Buffer *counters,
                  uint32_t counter, uint32_t capacity, bool doubleSided, const mat4 *lightVP = nullptr) const;
        void Clear();

    private:
        void CreateShaders(PassInfo &info, uint32_t module);
        void Build(PassInfo &info, uint32_t module, bool doubleSided);
        void BindFrame(PassInfo &info, Scene &scene);

        bool m_shadow = false;
        std::map<uint32_t, std::array<std::shared_ptr<PassInfo>, 2>> m_modules;
    };
} // namespace pe
