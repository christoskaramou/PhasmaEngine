#include "Render/FullscreenPasses.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "Camera/Camera.h"
#include "Render/SceneRendererHost.h"
#include "Render/ScriptRenderPasses.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"

namespace pe
{
    namespace
    {
        struct Entry
        {
            PassInfo *pass = nullptr;
            FullscreenPassDesc desc;
            const void *owner = nullptr;
            bool failed = false; // its shader failed; skipped until re-added
        };
        std::unordered_map<std::string, Entry> s_passes;

        void Record(const std::string &name, Entry &e, CommandBuffer *cmd)
        {
            // Resolved every frame: render targets are recreated on resize.
            SceneRendererHost *host = GetActiveSceneRendererHost();
            if (!host)
                return;
            Image *target = host->GetRenderTarget("viewport");
            Image *depth = host->GetDepthStencilTarget("depthStencil");
            Image *normal = host->GetRenderTarget("normal");
            if (!target || !depth || !normal)
                return;
            if (!e.pass)
            {
                e.pass = new PassInfo();
                e.pass->name = name;
                e.pass->pVertShader = Shader::Create({.sourcePath = Path::ResolveAsset(e.desc.shader),
                                                      .entryPoint = "mainVS",
                                                      .stage = PE_SHADER_STAGE_VERTEX});
                e.pass->pFragShader = Shader::Create({.sourcePath = Path::ResolveAsset(e.desc.shader),
                                                      .entryPoint = "mainPS",
                                                      .stage = PE_SHADER_STAGE_FRAGMENT});
                e.pass->colorFormats = {target->GetFormat()};
                e.pass->dynamicStates = {PE_DYNAMIC_STATE_VIEWPORT, PE_DYNAMIC_STATE_SCISSOR};
                e.pass->cullMode = PE_CULL_MODE_NONE;
                e.pass->depthTestEnable = false;
                e.pass->depthWriteEnable = false;
                e.pass->blendEnable = true;
                e.pass->colorBlendAttachments = {BlendState::Default};
                e.pass->Update();
            }
            const auto &descs = e.pass->GetDescriptors(RHII.GetFrameIndex());
            if (descs.empty())
                return;
            Descriptor *desc = descs[0];
            desc->SetImageView(0, depth->GetSRV(), depth->GetSampler());
            desc->SetImageView(1, normal->GetSRV(), normal->GetSampler());
            desc->Update();
            ImageBarrierInfo depthBarrier{};
            depthBarrier.image = depth;
            depthBarrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            depthBarrier.stageFlags = PE_STAGE_FRAGMENT_SHADER;
            depthBarrier.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
            cmd->ImageBarrier(depthBarrier);
            ImageBarrierInfo normalBarrier = depthBarrier;
            normalBarrier.image = normal;
            cmd->ImageBarrier(normalBarrier);
            const float w = target->GetWidth_f(), h = target->GetHeight_f();
            Attachment att{};
            att.image = target;
            att.loadOp = PE_LOAD_OP_LOAD;
            att.storeOp = PE_STORE_OP_STORE;
            cmd->BeginPass(1, &att, name);
            cmd->BindPipeline(*e.pass);
            cmd->SetViewport(0, 0, w, h, 0.0f, 1.0f);
            cmd->SetScissor(0, 0, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            Scene *scene = GetActiveScene();
            Camera *camera = scene ? scene->GetActiveCamera() : nullptr;
            const float nearPlane = camera ? camera->GetNearPlane() : 0.0f;
            const float thickness = std::max(e.desc.minThickness, e.desc.thicknessAt1080 * h / 1080.0f);
            cmd->SetConstantAt(0, vec4(1.0f / w, 1.0f / h, thickness, nearPlane));
            cmd->SetConstantAt(4, vec4(e.desc.params[0], e.desc.params[1], e.desc.params[2], e.desc.params[3]));
            cmd->PushConstants();
            cmd->Draw(3, 1, 0, 0);
            cmd->EndPass();
        }
    } // namespace

    void AddFullscreenPass(const std::string &name, const FullscreenPassDesc &desc, const void *owner)
    {
        Entry &entry = s_passes[name];
        // Re-added with another shader, or after its shader failed: rebuilt on next use.
        if (entry.pass && (entry.failed || entry.desc.shader != desc.shader))
        {
            RHII.WaitDeviceIdle();
            delete entry.pass;
            entry.pass = nullptr;
        }
        entry.failed = false;
        entry.desc = desc;
        entry.owner = owner;

        RegisterScriptRenderPass(
            name, desc.order,
            [name](CommandBuffer *cmd)
            {
                auto it = s_passes.find(name);
                if (it == s_passes.end() || it->second.failed)
                    return;
                // A shader that fails to load must not take the host down; Lua's protected call
                // catches the same for render_graph passes.
                try
                {
                    Record(name, it->second, cmd);
                }
                catch (const std::exception &error)
                {
                    it->second.failed = true;
                    Log::Error("[Render] fullscreen pass '" + name + "' failed; pass disabled: " + error.what());
                }
            },
            owner);
    }

    bool RemoveFullscreenPass(const std::string &name)
    {
        auto it = s_passes.find(name);
        if (it == s_passes.end())
            return false;
        UnregisterScriptRenderPass(name);
        RHII.WaitDeviceIdle();
        delete it->second.pass;
        s_passes.erase(it);
        return true;
    }

    void ClearFullscreenPasses(const void *owner)
    {
        bool waited = false;
        for (auto it = s_passes.begin(); it != s_passes.end();)
        {
            if (it->second.owner != owner)
            {
                ++it;
                continue;
            }
            UnregisterScriptRenderPass(it->first);
            if (it->second.pass && !waited)
            {
                RHII.WaitDeviceIdle();
                waited = true;
            }
            delete it->second.pass;
            it = s_passes.erase(it);
        }
    }
} // namespace pe
