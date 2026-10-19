#include "OpenPBRMask.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Render/SceneRenderTargets.h"
#include "Scene/OpenPBRMaterial.h"
#include "Scene/Scene.h"

namespace pe
{
    namespace
    {
        struct PushConstants_OpenPBRMask
        {
            uint32_t jointsCount;
            uint32_t verticesOffset;
            uint32_t moduleIndex;
            uint32_t padding;
            vec4 cameraPosition;
            mat4 lightViewProjection;
        };

        static_assert(sizeof(PushConstants_OpenPBRMask) == 96);
    } // namespace

    void OpenPBRMask::CreateShaders(PassInfo &info, uint32_t module)
    {
        std::vector<Define> defines;
        if (m_shadow)
            defines.push_back({"PE_MASK_SHADOW", "1"});

        info.name = "OpenPBRMask_" + std::to_string(module) + (m_shadow ? "_shadow" : "_depth");
        // Rebuild reuses the PassInfo, so release the shaders it already holds.
        info.DestroyShaders();
        info.pVertShader = Shader::Create({
            .sourcePath = Path::RuntimeAssets + "Shaders/MaterialX/MaterialMaskVS.hlsl",
            .entryPoint = "mainVS",
            .stage = PE_SHADER_STAGE_VERTEX,
            .defines = defines,
        });
        info.pFragShader = Shader::Create({
            .sourcePath = GetOpenPBRModule(module).maskSource,
            .entryPoint = "mainPS",
            .stage = PE_SHADER_STAGE_FRAGMENT,
            .defines = defines,
        });
    }

    void OpenPBRMask::Build(PassInfo &info, uint32_t module, bool doubleSided)
    {
        CreateShaders(info, module);
        info.cullMode = doubleSided || m_shadow ? PE_CULL_MODE_NONE : PE_CULL_MODE_FRONT;
        info.depthFormat = RHII.GetDepthFormat();
        info.depthTestEnable = true;
        info.depthWriteEnable = true;
        info.dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
        if (m_shadow)
        {
            const auto &settings = Settings::Get<SceneSettings>();
            info.depthBiasEnable = true;
            info.depthBiasConstantFactor = settings.depth_bias[0];
            info.depthBiasClamp = settings.depth_bias[1];
            info.depthBiasSlopeFactor = settings.depth_bias[2];
            info.dynamicStates.push_back(PE_DYNAMIC_STATE_DEPTH_BIAS);
        }
        else
        {
            info.colorFormats = {PE_FORMAT_R32G32B32A32_UINT};
            info.colorBlendAttachments = {BlendState(false, PE_BLEND_FACTOR_ONE, PE_BLEND_FACTOR_ZERO,
                                                     PE_BLEND_OP_ADD, PE_BLEND_FACTOR_ONE, PE_BLEND_FACTOR_ZERO,
                                                     PE_BLEND_OP_ADD, PE_COLOR_COMPONENT_RGBA)};
        }

        info.Update();
    }

    void OpenPBRMask::Sync(Scene &scene, bool shadow)
    {
        if (m_shadow != shadow)
            Clear();
        m_shadow = shadow;

        if (!SceneUsesOpenPBR())
        {
            Clear();
            return;
        }

        const auto &modules = scene.GetOpenPBRMaskModules();
        for (auto it = m_modules.begin(); it != m_modules.end();)
        {
            if (std::find(modules.begin(), modules.end(), it->first) == modules.end())
            {
                RHII.AddToDeletionQueue([retired = it->second] {});
                it = m_modules.erase(it);
            }
            else
            {
                ++it;
            }
        }

        for (uint32_t module : modules)
        {
            if (m_modules.contains(module))
                continue;

            // Build before inserting, like MaterialThinPass: a failed build leaves no half-made entry to draw.
            std::array<std::shared_ptr<PassInfo>, 2> variants;
            const uint32_t count = m_shadow ? 1u : 2u;
            for (uint32_t side = 0; side < count; ++side)
            {
                variants[side] = std::make_shared<PassInfo>();
                Build(*variants[side], module, side != 0u);
            }

            m_modules.emplace(module, std::move(variants));
        }
    }

    void OpenPBRMask::Update(Scene &scene)
    {
        for (const auto &[module, variants] : m_modules)
        {
            for (const auto &info : variants)
            {
                if (info)
                    BindFrame(*info, scene);
            }
        }
    }

    // Set 1 is the RHI's shared scene texture table, written by Scene::UpdateImageViews.
    void OpenPBRMask::BindFrame(PassInfo &info, Scene &scene)
    {
        const uint32_t frame = RHII.GetFrameIndex();
        const auto &sets = info.GetDescriptors(frame);
        Descriptor *set = sets[0];
        if (set->HasBinding(0))
            set->SetBuffer(0, scene.GetUniforms(frame));
        if (set->HasBinding(1))
            set->SetBuffer(1, scene.GetMeshConstants());
        if (set->HasBinding(2))
            set->SetBuffer(2, scene.GetIndirectAll());
        if (set->HasBinding(3))
            set->SetBuffer(3, scene.GetOpenPBRSkinningBinding(frame));
        if (set->HasBinding(4))
            set->SetBuffer(4, scene.GetBuffer());
        if (set->HasBinding(5))
            set->SetBuffer(5, scene.GetMaterialByteBuffer());
        if (set->HasBinding(6))
            set->SetSamplers(6, scene.GetOpenPBRSamplers());
        set->Update();
    }

    void OpenPBRMask::Rebuild()
    {
        if (!SceneUsesOpenPBR())
            return;

        for (const auto &[module, variants] : m_modules)
        {
            for (uint32_t side = 0; side < variants.size(); ++side)
            {
                if (variants[side])
                    Build(*variants[side], module, side != 0u);
            }
        }
    }

    void OpenPBRMask::AppendPassInfos(std::vector<PassInfo *> &infos) const
    {
        for (const auto &[module, variants] : m_modules)
        {
            for (const auto &info : variants)
            {
                if (info)
                    infos.push_back(info.get());
            }
        }
    }

    void OpenPBRMask::Prepare(CommandBuffer *cmd, Scene &scene) const
    {
        if (m_modules.empty())
            return;

        cmd->BufferBarrier({.buffer = scene.GetBuffer(),
                            .stageMask = PE_STAGE_VERTEX_INPUT | PE_STAGE_VERTEX_SHADER |
                                         PE_STAGE_FRAGMENT_SHADER | PE_STAGE_COMPUTE_SHADER,
                            .accessMask = PE_ACCESS_VERTEX_ATTRIBUTE_READ | PE_ACCESS_INDEX_READ |
                                          PE_ACCESS_SHADER_READ});
    }

    void OpenPBRMask::Draw(CommandBuffer *cmd, Scene &scene, Buffer *indirect, Buffer *counters,
                           uint32_t counter, uint32_t capacity, bool doubleSided, const mat4 *lightVP) const
    {
        if (m_modules.empty())
            return;

        PE_ERROR_IF(scene.GetVerticesOffset() > UINT32_MAX, "OpenPBR mask geometry exceeds uint32 addressing");
        PushConstants_OpenPBRMask pc{};
        pc.jointsCount = static_cast<uint32_t>(scene.GetMaxJointCount());
        pc.verticesOffset = static_cast<uint32_t>(scene.GetVerticesOffset());
        pc.cameraPosition = vec4(scene.GetActiveCamera()->GetPosition(), 1.f);
        pc.lightViewProjection = lightVP ? *lightVP : mat4(1.f);
        cmd->BindIndexBuffer(scene.GetBuffer(), 0);
        cmd->BindVertexBuffer(scene.GetBuffer(), scene.GetPositionsOffset());
        scene.BindDrawIdBuffer(cmd);

        // ponytail: replay culled lists per module; change routing only after measuring module-count cost.
        for (const auto &[module, variants] : m_modules)
        {
            cmd->BindPipeline(*variants[m_shadow || !doubleSided ? 0 : 1]);
            if (m_shadow)
            {
                const auto &settings = Settings::Get<SceneSettings>();
                cmd->SetDepthBias(settings.depth_bias[0], settings.depth_bias[1], settings.depth_bias[2]);
            }

            pc.moduleIndex = module;
            cmd->SetConstants(pc);
            cmd->PushConstants();
            if (counters)
                cmd->DrawIndexedIndirectCount(indirect, 0, counters, counter * sizeof(uint32_t), capacity);
            else
                cmd->DrawIndexedIndirect(indirect, 0, capacity);
        }
    }

    void OpenPBRMask::Clear()
    {
        for (const auto &[module, variants] : m_modules)
            RHII.AddToDeletionQueue([retired = variants] {});
        m_modules.clear();
    }
} // namespace pe
