#include "Render/RenderPassShaderReload.h"
#include "API/Command.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"

namespace pe
{
    namespace
    {
        bool ShaderReadsFile(Shader *shader, size_t fileHash)
        {
            if (shader->GetPathID() == fileHash)
                return true;

            for (const std::string &include : shader->GetCache().GetIncludes())
            {
                if (static_cast<size_t>(StringHash(include)) == fileHash)
                    return true;
            }

            return false;
        }

        void AddUniqueShader(std::vector<Shader *> &shaders, Shader *shader)
        {
            if (!shader)
                return;

            if (std::find(shaders.begin(), shaders.end(), shader) == shaders.end())
                shaders.push_back(shader);
        }

        struct PassInfoShaderState
        {
            Shader *comp = nullptr;
            Shader *vert = nullptr;
            Shader *frag = nullptr;
            Shader *rayGen = nullptr;
            std::vector<Shader *> miss;
            std::vector<HitGroup> hitGroups;
            uint32_t maxRecursionDepth = 1;

            std::vector<Shader *> GetShaders() const
            {
                std::vector<Shader *> shaders;
                AddUniqueShader(shaders, comp);
                AddUniqueShader(shaders, vert);
                AddUniqueShader(shaders, frag);
                AddUniqueShader(shaders, rayGen);
                for (Shader *shader : miss)
                    AddUniqueShader(shaders, shader);
                for (const HitGroup &hitGroup : hitGroups)
                {
                    AddUniqueShader(shaders, hitGroup.closestHit);
                    AddUniqueShader(shaders, hitGroup.anyHit);
                    AddUniqueShader(shaders, hitGroup.intersection);
                }
                return shaders;
            }

            bool Matches(std::optional<size_t> changedShaderHash) const
            {
                if (!changedShaderHash.has_value())
                    return true;

                for (Shader *shader : GetShaders())
                    if (ShaderReadsFile(shader, changedShaderHash.value()))
                        return true;

                return false;
            }

            void Restore(PassInfo &info) const
            {
                info.pCompShader = comp;
                info.pVertShader = vert;
                info.pFragShader = frag;
                info.acceleration.rayGen = rayGen;
                info.acceleration.miss = miss;
                info.acceleration.hitGroups = hitGroups;
                info.acceleration.maxRecursionDepth = maxRecursionDepth;
            }
        };

        PassInfoShaderState CaptureShaderState(const PassInfo &info)
        {
            PassInfoShaderState state;
            state.comp = info.pCompShader;
            state.vert = info.pVertShader;
            state.frag = info.pFragShader;
            state.rayGen = info.acceleration.rayGen;
            state.miss = info.acceleration.miss;
            state.hitGroups = info.acceleration.hitGroups;
            state.maxRecursionDepth = info.acceleration.maxRecursionDepth;
            return state;
        }

        std::vector<PassInfoShaderState> CaptureShaderStates(const std::vector<PassInfo *> &passInfos)
        {
            std::vector<PassInfoShaderState> states;
            states.reserve(passInfos.size());
            for (const PassInfo *info : passInfos)
                states.push_back(CaptureShaderState(*info));
            return states;
        }

        std::vector<Shader *> CollectShaders(const std::vector<PassInfoShaderState> &states)
        {
            std::vector<Shader *> shaders;
            for (const PassInfoShaderState &state : states)
                for (Shader *shader : state.GetShaders())
                    AddUniqueShader(shaders, shader);
            return shaders;
        }

        bool ContainsShader(const std::vector<Shader *> &shaders, Shader *shader)
        {
            return std::find(shaders.begin(), shaders.end(), shader) != shaders.end();
        }

        bool MatchesShaderHash(const std::vector<PassInfoShaderState> &states,
                               std::optional<size_t> changedShaderHash)
        {
            for (const PassInfoShaderState &state : states)
                if (state.Matches(changedShaderHash))
                    return true;
            return false;
        }

        void DestroyShadersNotIn(const std::vector<Shader *> &candidates, const std::vector<Shader *> &keep)
        {
            std::vector<Shader *> destroyed;
            for (Shader *shader : candidates)
            {
                if (!shader || ContainsShader(keep, shader) || ContainsShader(destroyed, shader))
                    continue;

                destroyed.push_back(shader);
                Shader *doomed = shader;
                Shader::Destroy(doomed);
            }
        }

        void RestoreShaderStates(const std::vector<PassInfo *> &passInfos,
                                 const std::vector<PassInfoShaderState> &states)
        {
            for (size_t i = 0; i < passInfos.size() && i < states.size(); ++i)
                states[i].Restore(*passInfos[i]);
        }

        std::vector<size_t> CaptureHashes(const std::vector<PassInfo *> &passInfos)
        {
            std::vector<size_t> hashes;
            hashes.reserve(passInfos.size());
            for (const PassInfo *info : passInfos)
                hashes.push_back(info->GetHash());
            return hashes;
        }

        // The pipeline cache is keyed by PassInfo::GetHash (shader contents, cached descriptor layouts, fixed-function
        // state), so a pass whose hash survived the reload keeps its pipelines. Rebuilding them all took 18 s per
        // reload on Vulkan with a MaterialX RT hit group: the driver never caches RT pipelines with descriptor heaps.
        void RetireChangedPipelines(const std::vector<PassInfo *> &passInfos, const std::vector<size_t> &oldHashes)
        {
            for (size_t i = 0; i < passInfos.size(); ++i)
            {
                if (static_cast<size_t>(passInfos[i]->GetHash()) != oldHashes[i])
                    CommandBuffer::RetirePipelines(*passInfos[i]);
            }
        }

        // A pass info updated before the failure still hashes the shaders it gave back: reflect the restored ones, or
        // a later reload of the same sources would find the old shaders' pipeline under the new hash.
        void ResyncRestoredPassInfos(IRenderPassComponent &renderPass,
                                     const std::vector<PassInfo *> &passInfos,
                                     const std::vector<size_t> &oldHashes)
        {
            bool resynced = false;
            for (size_t i = 0; i < passInfos.size(); ++i)
            {
                if (static_cast<size_t>(passInfos[i]->GetHash()) == oldHashes[i])
                    continue;

                passInfos[i]->Update();
                resynced = true;
            }

            if (resynced)
                renderPass.UpdateDescriptorSets();

            RetireChangedPipelines(passInfos, oldHashes);
        }
    } // namespace

    bool ReloadRenderPassShaders(IRenderPassComponent &renderPass, std::optional<size_t> changedShaderHash)
    {
        std::vector<PassInfo *> passInfos = renderPass.GetPassInfos();
        passInfos.erase(std::remove(passInfos.begin(), passInfos.end(), nullptr), passInfos.end());
        if (passInfos.empty())
            return false;

        const std::vector<PassInfoShaderState> oldStates = CaptureShaderStates(passInfos);
        if (CollectShaders(oldStates).empty())
            return false;

        if (!MatchesShaderHash(oldStates, changedShaderHash))
            return false;

        const std::vector<size_t> oldHashes = CaptureHashes(passInfos);
        try
        {
            renderPass.UpdatePassInfo();
            renderPass.UpdateDescriptorSets();

            const std::vector<PassInfoShaderState> newStates = CaptureShaderStates(passInfos);
            DestroyShadersNotIn(CollectShaders(oldStates), CollectShaders(newStates));
            RetireChangedPipelines(passInfos, oldHashes);
            return true;
        }
        catch (const std::exception &)
        {
            const std::vector<PassInfoShaderState> currentStates = CaptureShaderStates(passInfos);
            DestroyShadersNotIn(CollectShaders(currentStates), CollectShaders(oldStates));
            RestoreShaderStates(passInfos, oldStates);
            ResyncRestoredPassInfos(renderPass, passInfos, oldHashes);
        }

        return false;
    }

    void ReloadRenderPassShaders(const OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents,
                                 std::optional<size_t> changedShaderHash)
    {
        RHII.WaitDeviceIdle();

        for (auto &renderPassComponent : renderPassComponents)
        {
            if (renderPassComponent)
                ReloadRenderPassShaders(*renderPassComponent, changedShaderHash);
        }
    }
} // namespace pe
