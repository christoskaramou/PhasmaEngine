#include "Render/RenderPassShaderReload.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"

namespace pe
{
    namespace
    {
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
                    if (shader->GetPathID() == changedShaderHash.value())
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

        try
        {
            renderPass.UpdatePassInfo();
            renderPass.UpdateDescriptorSets();

            const std::vector<PassInfoShaderState> newStates = CaptureShaderStates(passInfos);
            DestroyShadersNotIn(CollectShaders(oldStates), CollectShaders(newStates));
            return true;
        }
        catch (const std::exception &)
        {
            const std::vector<PassInfoShaderState> currentStates = CaptureShaderStates(passInfos);
            DestroyShadersNotIn(CollectShaders(currentStates), CollectShaders(oldStates));
            RestoreShaderStates(passInfos, oldStates);
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
