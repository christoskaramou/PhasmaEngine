/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#if PE_VULKAN
#include "Renderer/Renderer.h"
#include "Model/Mesh.h"
#include "Renderer/RHI.h"
#include "Systems/CameraSystem.h"
#include "Systems/EventSystem.h"
#include "Systems/PostProcessSystem.h"
#include "Renderer/Command.h"
#include "Renderer/Image.h"
#include "Renderer/RenderPass.h"
#include "Renderer/FrameBuffer.h"
#include "Renderer/Queue.h"
#include "PostProcess/Bloom.h"
#include "PostProcess/DOF.h"
#include "PostProcess/FXAA.h"
#include "PostProcess/MotionBlur.h"
#include "PostProcess/SSAO.h"
#include "PostProcess/SSR.h"
#include "PostProcess/TAA.h"
#include "PostProcess/SSGI.h"

namespace pe
{
    Renderer::Renderer()
    {
    }

    Renderer::~Renderer()
    {
    }

    void RenderArea::Update(float x, float y, float w, float h, float minDepth, float maxDepth)
    {
        viewport.x = x;
        viewport.y = y;
        viewport.width = w;
        viewport.height = h;
        viewport.minDepth = minDepth;
        viewport.maxDepth = maxDepth;

        scissor.x = static_cast<int>(x);
        scissor.y = static_cast<int>(y);
        scissor.width = static_cast<int>(w);
        scissor.height = static_cast<int>(h);
    }

    void Renderer::CheckModelsQueue()
    {
#ifndef IGNORE_SCRIPTS
        for (auto it = Queue::addScript.begin(); it != Queue::addScript.end();)
        {
            delete Model::models[std::get<0>(*it)].script;
            Model::models[std::get<0>(*it)].script = new Script(std::get<1>(*it).c_str());
            it = Queue::addScript.erase(it);
        }
        for (auto it = Queue::removeScript.begin(); it != Queue::removeScript.end();)
        {
            if (Model::models[*it].script)
            {
                delete Model::models[*it].script;
                Model::models[*it].script = nullptr;
            }
            it = Queue::removeScript.erase(it);
        }

        for (auto it = Queue::compileScript.begin(); it != Queue::compileScript.end();)
        {
            std::string name;
            if (Model::models[*it].script)
            {
                name = Model::models[*it].script->name;
                delete Model::models[*it].script;
                Model::models[*it].script = new Script(name.c_str());
            }
            it = Queue::compileScript.erase(it);
        }

#endif
    }

    void Renderer::ComputeAnimations()
    {
    }

    enum class RenderQueue
    {
        Opaque = 1,
        AlphaCut = 2,
        AlphaBlend = 3
    };

    std::vector<GPUTimer> gpuTimers{};
    
    void Renderer::RecordDeferredCmds(CommandBuffer *cmd, uint32_t imageIndex)
    {
        Deferred &deferred = *WORLD_ENTITY->GetComponent<Deferred>();
        Shadows &shadows = *WORLD_ENTITY->GetComponent<Shadows>();
        SSAO &ssao = *WORLD_ENTITY->GetComponent<SSAO>();
        SSR &ssr = *WORLD_ENTITY->GetComponent<SSR>();
        FXAA &fxaa = *WORLD_ENTITY->GetComponent<FXAA>();
        TAA &taa = *WORLD_ENTITY->GetComponent<TAA>();
        Bloom &bloom = *WORLD_ENTITY->GetComponent<Bloom>();
        DOF &dof = *WORLD_ENTITY->GetComponent<DOF>();
        MotionBlur &motionBlur = *WORLD_ENTITY->GetComponent<MotionBlur>();
        SSGI &ssgi = *WORLD_ENTITY->GetComponent<SSGI>();

        if (gpuTimers.size() < 12)
        {
            size_t count = 12 - gpuTimers.size();
            for (size_t i = 0; i < count; i++)
                gpuTimers.push_back(GPUTimer());
        }

        FrameTimer &frameTimer = FrameTimer::Instance();

        cmd->Begin();
        Debug::BeginCmdRegion(cmd->Handle(), "RecordDeferredCmds");

        gpuTimers[0].Start(cmd);

        // MODELS
        Debug::BeginCmdRegion(cmd->Handle(), "Geometry");
        gpuTimers[1].Start(cmd);
        deferred.BeginPass(cmd, imageIndex);
        for (auto &model : Model::models)
            model.draw((uint16_t)RenderQueue::Opaque);
        for (auto &model : Model::models)
            model.draw((uint16_t)RenderQueue::AlphaCut);
        for (auto &model : Model::models)
            model.draw((uint16_t)RenderQueue::AlphaBlend);
        deferred.EndPass(cmd);
        frameTimer.timestamps[4] = gpuTimers[1].End();
        Debug::EndCmdRegion(cmd->Handle());

        // SCREEN SPACE AMBIENT OCCLUSION
        if (GUI::show_ssao)
        {
            gpuTimers[2].Start(cmd);
            ssao.Draw(cmd, imageIndex);
            frameTimer.timestamps[5] = gpuTimers[2].End();
        }

        // SCREEN SPACE REFLECTIONS
        if (GUI::show_ssr)
        {
            gpuTimers[3].Start(cmd);
            ssr.Draw(cmd, imageIndex);
            frameTimer.timestamps[6] = gpuTimers[3].End();
        }

        // COMPOSITION
        gpuTimers[4].Start(cmd);
        deferred.Draw(cmd, imageIndex);
        frameTimer.timestamps[7] = gpuTimers[4].End();

        if (GUI::use_SSGI)
        {
            // gpuTimers[8].Start(cmd);
            ssgi.Draw(cmd, imageIndex);
            // frameTimer.timestamps[10] = gpuTimers[8].End();
        }

        if (GUI::use_AntiAliasing)
        {
            // TAA
            if (GUI::use_TAA)
            {
                gpuTimers[5].Start(cmd);
                taa.Draw(cmd, imageIndex);
                frameTimer.timestamps[8] = gpuTimers[5].End();
            }
            // FXAA
            else if (GUI::use_FXAA)
            {
                gpuTimers[6].Start(cmd);
                fxaa.Draw(cmd, imageIndex);
                frameTimer.timestamps[8] = gpuTimers[6].End();
            }
        }

        // BLOOM
        if (GUI::show_Bloom)
        {
            gpuTimers[7].Start(cmd);
            bloom.Draw(cmd, imageIndex);
            frameTimer.timestamps[9] = gpuTimers[7].End();
        }

        // Depth of Field
        if (GUI::use_DOF)
        {
            gpuTimers[8].Start(cmd);
            dof.Draw(cmd, imageIndex);
            frameTimer.timestamps[10] = gpuTimers[8].End();
        }

        // MOTION BLUR
        if (GUI::show_motionBlur)
        {
            gpuTimers[9].Start(cmd);
            motionBlur.Draw(cmd, imageIndex);
            frameTimer.timestamps[11] = gpuTimers[9].End();
        }

        BlitToViewport(cmd, GUI::s_currRenderImage ? GUI::s_currRenderImage : m_viewportRT, imageIndex);

        // GUI
        gpuTimers[10].Start(cmd);
        gui.Draw(cmd, imageIndex);
        frameTimer.timestamps[12] = gpuTimers[10].End();

        frameTimer.timestamps[2] = gpuTimers[0].End();

        Debug::EndCmdRegion(cmd->Handle());
        cmd->End();
    }

    void Renderer::RecordShadowsCmds(CommandBuffer **cmds, uint32_t cmdCount, uint32_t imageIndex)
    {
        Shadows &shadows = *WORLD_ENTITY->GetComponent<Shadows>();

        if (gpuTimers.size() < cmdCount)
        {
            size_t count = cmdCount - gpuTimers.size();
            for (size_t i = 0; i < count; i++)
                gpuTimers.push_back(GPUTimer());
        }

        for (uint32_t i = 0; i < cmdCount; i++)
        {
            CommandBuffer *cmd = cmds[i];
            FrameBuffer *frameBuffer = shadows.framebuffers[imageIndex][i];

            cmd->Begin();
            Debug::BeginCmdRegion(cmd->Handle(), "Shadow_Cascade_" + std::to_string(i));

            shadows.textures[i]->ChangeLayout(cmd, LayoutState::DepthStencilAttachment);

            FrameTimer &frameTimer = FrameTimer::Instance();
            gpuTimers[i].Start(cmd);

            cmd->BeginPass(shadows.renderPass, frameBuffer);
            cmd->SetDepthBias(GUI::depthBias[0], GUI::depthBias[1], GUI::depthBias[2]);
            cmd->BindPipeline(shadows.pipeline);
            for (auto &model : Model::models)
            {
                if (model.render)
                {
                    ShadowPushConstData data{};

                    cmd->BindVertexBuffer(model.shadowsVertexBuffer, 0);
                    cmd->BindIndexBuffer(model.indexBuffer, 0);

                    std::vector<Descriptor *> dsetHandles{RHII.uniformBuffers[model.uniformBufferIndex].descriptor};
                    cmd->BindDescriptors(shadows.pipeline, (uint32_t)dsetHandles.size(), dsetHandles.data());

                    for (auto &node : model.linearNodes)
                    {
                        if (node->mesh)
                        {
                            data.cascade = shadows.cascades[i] * model.ubo.matrix * node->mesh->meshData.matrix;
                            data.meshIndex = static_cast<uint32_t>(node->mesh->uniformBufferOffset / sizeof(mat4));
                            data.meshJointCount = static_cast<uint32_t>(node->mesh->meshData.jointMatrices.size());

                            cmd->PushConstants(shadows.pipeline, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstData), &data);
                            for (auto &primitive : node->mesh->primitives)
                            {
                                // if (primitive.render)
                                cmd->DrawIndexed(
                                    primitive.indicesSize,
                                    1,
                                    node->mesh->indexOffset + primitive.indexOffset,
                                    node->mesh->vertexOffset + primitive.vertexOffset,
                                    0);
                            }
                        }
                    }
                }
            }

            cmd->EndPass();
            frameTimer.timestamps[13 + static_cast<size_t>(i)] = gpuTimers[i].End();
            Debug::EndCmdRegion(cmd->Handle());
            cmd->End();
        }
    }

    Image *Renderer::CreateRenderTarget(const std::string &name, Format format, ImageUsageFlags additionalFlags, bool blendEnable)
    {
        ImageCreateInfo info{};
        info.format = format;
        info.width = static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale);
        info.height = static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale);
        info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | additionalFlags;
        info.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        info.name = name;
        Image *rt = Image::Create(info);

        ImageViewCreateInfo viewInfo{};
        viewInfo.image = rt;
        rt->CreateImageView(viewInfo);

        SamplerCreateInfo samplerInfo{};
        rt->CreateSampler(samplerInfo);

        rt->ChangeLayout(nullptr, LayoutState::ColorAttachment);

        rt->blendAttachment.blendEnable = blendEnable ? VK_TRUE : VK_FALSE;
        rt->blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        rt->blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        rt->blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        rt->blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        rt->blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        rt->blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        rt->blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        GUI::s_renderImages.push_back(rt);
        m_renderTargets[StringHash(name)] = rt;

        return rt;
    }

    Image *Renderer::CreateDepthTarget(const std::string &name, Format format, ImageUsageFlags additionalFlags)
    {
        ImageCreateInfo info{};
        info.width = static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale);
        info.height = static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale);
        info.usage = additionalFlags;
        info.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        info.format = (Format)format;
        info.name = name;
        Image *depth = Image::Create(info);

        ImageViewCreateInfo viewInfo{};
        viewInfo.image = depth;
        viewInfo.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; // | VK_IMAGE_ASPECT_STENCIL_BIT;
        depth->CreateImageView(viewInfo);

        SamplerCreateInfo samplerInfo{};
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.compareEnable = VK_TRUE;
        depth->CreateSampler(samplerInfo);

        depth->ChangeLayout(nullptr, LayoutState::DepthStencilAttachment);

        GUI::s_renderImages.push_back(depth);
        m_depthTargets[StringHash(name)] = depth;

        return depth;
    }

    Image *Renderer::GetRenderTarget(const std::string &name)
    {
        return m_renderTargets[StringHash(name)];
    }

    Image *Renderer::GetRenderTarget(size_t hash)
    {
        return m_renderTargets[hash];
    }

    Image *Renderer::GetDepthTarget(const std::string &name)
    {
        return m_depthTargets[StringHash(name)];
    }

    Image *Renderer::GetDepthTarget(size_t hash)
    {
        return m_depthTargets[hash];
    }

    Image *Renderer::CreateFSSampledImage()
    {
        ImageCreateInfo info{};
        info.format = RHII.surface->format;
        info.initialState = LayoutState::Undefined;
        info.width = static_cast<uint32_t>(WIDTH_f * GUI::renderTargetsScale);
        info.height = static_cast<uint32_t>(HEIGHT_f * GUI::renderTargetsScale);
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        info.name = "FSSampledImage";
        Image *sampledImage = Image::Create(info);

        ImageViewCreateInfo viewInfo{};
        viewInfo.image = sampledImage;
        sampledImage->CreateImageView(viewInfo);

        SamplerCreateInfo samplerInfo{};
        sampledImage->CreateSampler(samplerInfo);

        sampledImage->ChangeLayout(nullptr, LayoutState::ShaderReadOnly);

        return sampledImage;
    }

#ifndef IGNORE_SCRIPTS
    // Callbacks for scripts -------------------
    static void LoadModel(MonoString *folderPath, MonoString *modelName, uint32_t instances)
    {
        const std::string curPath = std::filesystem::current_path().string() + "\\";
        const std::string path(mono_string_to_utf8(folderPath));
        const std::string name(mono_string_to_utf8(modelName));
        for (; instances > 0; instances--)
            Queue::loadModel.emplace_back(curPath + path, name);
    }

    static bool KeyDown(uint32_t key)
    {
        return ImGui::GetIO().KeysDown[key];
    }

    static bool MouseButtonDown(uint32_t button)
    {
        return ImGui::GetIO().MouseDown[button];
    }

    static ImVec2 GetMousePos()
    {
        return ImGui::GetIO().MousePos;
    }

    static void SetMousePos(float x, float y)
    {
        SDL_WarpMouseInWindow(GUI::g_Window, static_cast<int>(x), static_cast<int>(y));
    }

    static float GetMouseWheel()
    {
        return ImGui::GetIO().MouseWheel;
    }

    static void SetTimeScale(float timeScale)
    {
        GUI::timeScale = timeScale;
    }
    // ----------------------------------------
#endif

    void Renderer::LoadResources()
    {
        // SKYBOXES LOAD
        std::array<std::string, 6> skyTextures = {
            Path::Assets + "Objects/sky/right.png",
            Path::Assets + "Objects/sky/left.png",
            Path::Assets + "Objects/sky/top.png",
            Path::Assets + "Objects/sky/bottom.png",
            Path::Assets + "Objects/sky/back.png",
            Path::Assets + "Objects/sky/front.png"};
        skyBoxDay.loadSkyBox(skyTextures, 1024);
        skyTextures = {
            Path::Assets + "Objects/lmcity/lmcity_rt.png",
            Path::Assets + "Objects/lmcity/lmcity_lf.png",
            Path::Assets + "Objects/lmcity/lmcity_up.png",
            Path::Assets + "Objects/lmcity/lmcity_dn.png",
            Path::Assets + "Objects/lmcity/lmcity_bk.png",
            Path::Assets + "Objects/lmcity/lmcity_ft.png"};
        skyBoxNight.loadSkyBox(skyTextures, 512);

#ifndef IGNORE_SCRIPTS
        // SCRIPTS
        Script::Init();
        Script::addCallback("Global::LoadModel", reinterpret_cast<const void *>(LoadModel));
        Script::addCallback("Global::KeyDown", reinterpret_cast<const void *>(KeyDown));
        Script::addCallback("Global::SetTimeScale", reinterpret_cast<const void *>(SetTimeScale));
        Script::addCallback("Global::MouseButtonDown", reinterpret_cast<const void *>(MouseButtonDown));
        Script::addCallback("Global::GetMousePos", reinterpret_cast<const void *>(GetMousePos));
        Script::addCallback("Global::SetMousePos", reinterpret_cast<const void *>(SetMousePos));
        Script::addCallback("Global::GetMouseWheel", reinterpret_cast<const void *>(GetMouseWheel));
        scripts.push_back(new Script("Load"));
#endif
    }

    void Renderer::CreateUniforms()
    {
        skyBoxDay.createDescriptorSet();
        skyBoxNight.createDescriptorSet();
    }

    void Renderer::ResizeViewport(uint32_t width, uint32_t height)
    {
        if (s_currentQueue)
            s_currentQueue->WaitIdle();

        renderArea.Update(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));

        WIDTH = width;
        HEIGHT = height;

        for (auto &rt : m_renderTargets)
            Image::Destroy(rt.second);
        Image::Destroy(m_depth);
        GUI::s_renderImages.clear();

        Swapchain::Destroy(RHII.swapchain);
        RHII.CreateSwapchain(RHII.surface);

        m_viewportRT = CreateRenderTarget("viewport", RHII.surface->format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        m_depth = CreateDepthTarget("depth", RHII.GetDepthFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        CreateRenderTarget("normal", VK_FORMAT_R32G32B32A32_SFLOAT);
        CreateRenderTarget("albedo", RHII.surface->format);
        CreateRenderTarget("srm", RHII.surface->format); // Specular Roughness Metallic
        CreateRenderTarget("ssao", VK_FORMAT_R16_UNORM);
        CreateRenderTarget("ssaoBlur", VK_FORMAT_R8_UNORM);
        CreateRenderTarget("ssr", RHII.surface->format);
        CreateRenderTarget("velocity", VK_FORMAT_R16G16_SFLOAT);
        CreateRenderTarget("brightFilter", RHII.surface->format);
        CreateRenderTarget("gaussianBlurHorizontal", RHII.surface->format);
        CreateRenderTarget("gaussianBlurVertical", RHII.surface->format);
        CreateRenderTarget("emissive", RHII.surface->format);
        CreateRenderTarget("taa", RHII.surface->format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

        for (auto &rc : m_renderComponents)
            rc.second->Resize(width, height);

        CONTEXT->GetSystem<PostProcessSystem>()->Resize(width, height);

        gui.Destroy();
        gui.CreateRenderPass();
        gui.CreateFrameBuffers();
    }

    void Renderer::BlitToViewport(CommandBuffer *cmd, Image *renderedImage, uint32_t imageIndex)
    {
        Debug::BeginCmdRegion(cmd->Handle(), "BlitToViewport");

        Image *s_chain_Image = RHII.swapchain->images[imageIndex];

        renderedImage->ChangeLayout(cmd, LayoutState::TransferSrc);
        s_chain_Image->ChangeLayout(cmd, LayoutState::TransferDst);

        ImageBlit blit{};
        blit.srcOffsets[0] = Offset3D{static_cast<int32_t>(renderedImage->imageInfo.width), static_cast<int32_t>(renderedImage->imageInfo.height), 0};
        blit.srcOffsets[1] = Offset3D{0, 0, 1};
        blit.srcSubresource.aspectMask = renderedImage->viewInfo.aspectMask;
        blit.srcSubresource.layerCount = 1;
        Viewport &vp = renderArea.viewport;
        blit.dstOffsets[0] = Offset3D{(int32_t)vp.x, (int32_t)vp.y, 0};
        blit.dstOffsets[1] = Offset3D{(int32_t)vp.x + (int32_t)vp.width, (int32_t)vp.y + (int32_t)vp.height, 1};
        blit.dstSubresource.aspectMask = s_chain_Image->viewInfo.aspectMask;
        blit.dstSubresource.layerCount = 1;

        cmd->BlitImage(
            renderedImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            s_chain_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR);

        renderedImage->ChangeLayout(cmd, LayoutState::ColorAttachment);
        s_chain_Image->ChangeLayout(cmd, LayoutState::PresentSrc);

        Debug::EndCmdRegion(cmd->Handle());
    }

    void Renderer::RecreatePipelines()
    {
        s_currentQueue->WaitIdle();

        Deferred &deferred = *WORLD_ENTITY->GetComponent<Deferred>();
        Shadows &shadows = *WORLD_ENTITY->GetComponent<Shadows>();
        SSAO &ssao = *WORLD_ENTITY->GetComponent<SSAO>();
        SSR &ssr = *WORLD_ENTITY->GetComponent<SSR>();
        FXAA &fxaa = *WORLD_ENTITY->GetComponent<FXAA>();
        TAA &taa = *WORLD_ENTITY->GetComponent<TAA>();
        Bloom &bloom = *WORLD_ENTITY->GetComponent<Bloom>();
        DOF &dof = *WORLD_ENTITY->GetComponent<DOF>();
        MotionBlur &motionBlur = *WORLD_ENTITY->GetComponent<MotionBlur>();

        Pipeline::Destroy(shadows.pipeline);
        Pipeline::Destroy(ssao.pipeline);
        Pipeline::Destroy(ssao.pipelineBlur);
        Pipeline::Destroy(ssr.pipeline);
        Pipeline::Destroy(deferred.pipeline);
        Pipeline::Destroy(deferred.pipelineComposition);
        Pipeline::Destroy(fxaa.pipeline);
        Pipeline::Destroy(taa.pipeline);
        Pipeline::Destroy(taa.pipelineSharpen);
        Pipeline::Destroy(dof.pipeline);
        Pipeline::Destroy(motionBlur.pipeline);

        deferred.CreatePipeline();
        shadows.CreatePipeline();
        ssao.CreatePipeline();
        ssr.CreatePipeline();
        fxaa.CreatePipeline();
        taa.CreatePipeline();
        bloom.CreatePipeline();
        dof.CreatePipeline();
        motionBlur.CreatePipeline();

        CONTEXT->GetSystem<CameraSystem>()->GetCamera(0)->ReCreateComputePipelines();
    }
}
#endif
