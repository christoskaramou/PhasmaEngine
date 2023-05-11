#if PE_VULKAN
#include "Renderer/Deferred.h"
#include "Model/Model.h"
#include "Model/Mesh.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Surface.h"
#include "Shader/Shader.h"
#include "GUI/GUI.h"
#include "tinygltf/stb_image.h"
#include "Shader/Reflection.h"
#include "Renderer/RHI.h"
#include "Renderer/Command.h"
#include "Renderer/Descriptor.h"
#include "Systems/LightSystem.h"
#include "Renderer/Framebuffer.h"
#include "Renderer/RenderPass.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Queue.h"
#include "Systems/RendererSystem.h"
#include "PostProcess/SSAO.h"
#include "PostProcess/SSR.h"
#include "Utilities/Downsampler.h"

namespace pe
{
    Deferred::Deferred()
    {
        DSet = {};
        pipeline = nullptr;
    }

    Deferred::~Deferred()
    {
    }

    void Deferred::Init()
    {
        RendererSystem *rs = CONTEXT->GetSystem<RendererSystem>();

        normalRT = rs->GetRenderTarget("normal");
        albedoRT = rs->GetRenderTarget("albedo");
        srmRT = rs->GetRenderTarget("srm"); // Specular Roughness Metallic
        velocityRT = rs->GetRenderTarget("velocity");
        emissiveRT = rs->GetRenderTarget("emissive");
        viewportRT = rs->GetRenderTarget("viewport");
        depth = rs->GetDepthTarget("depth");
        ssaoBlurRT = rs->GetRenderTarget("ssaoBlur");
        ssrRT = rs->GetRenderTarget("ssr");
    }

    void Deferred::UpdatePipelineInfo()
    {
        Shadows &shadows = *WORLD_ENTITY->GetComponent<Shadows>();
        SkyBox &skybox = CONTEXT->GetSystem<RendererSystem>()->skyBoxDay;

        const std::vector<Define> definesFrag{
            Define{"SHADOWMAP_CASCADES", std::to_string(SHADOWMAP_CASCADES)},
            Define{"SHADOWMAP_SIZE", std::to_string((float)SHADOWMAP_SIZE)},
            Define{"SHADOWMAP_TEXEL_SIZE", std::to_string(1.0f / (float)SHADOWMAP_SIZE)},
            Define{"MAX_POINT_LIGHTS", std::to_string(MAX_POINT_LIGHTS)},
            Define{"MAX_SPOT_LIGHTS", std::to_string(MAX_SPOT_LIGHTS)}};

        pipelineInfo = std::make_shared<PipelineCreateInfo>();
        PipelineCreateInfo &info = *pipelineInfo;

        info.pVertShader = Shader::Create(ShaderInfo{"Shaders/Common/quad.hlsl", ShaderStage::VertexBit});
        info.pFragShader = Shader::Create(ShaderInfo{"Shaders/Deferred/compositionPS.hlsl", ShaderStage::FragmentBit, definesFrag});
        info.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
        info.pushConstantStage = ShaderStage::FragmentBit;
        info.pushConstantSize = sizeof(mat4);
        info.colorBlendAttachments = {viewportRT->blendAttachment};
        info.descriptorSetLayouts = {
            DSet->GetLayout(),
            shadows.DSetDeferred->GetLayout(),
            skybox.DSet->GetLayout()};
        info.dynamicColorTargets = 1;
        info.colorFormats = &viewportRT->imageInfo.format;
        info.name = "composition_pipeline";
        
        info.UpdateHash();
    }

    void Deferred::CreateUniforms(CommandBuffer *cmd)
    {
        const std::string path = Path::Assets + "Objects/ibl_brdf_lut.png";
        StringHash hash = StringHash(path);
        if (Image::uniqueImages.find(hash) != Image::uniqueImages.end())
        {
            ibl_brdf_lut = Image::uniqueImages[hash];
        }
        else
        {
            int texWidth, texHeight, texChannels;
            stbi_set_flip_vertically_on_load(true);
            unsigned char *pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            stbi_set_flip_vertically_on_load(false);

            if (!pixels)
                PE_ERROR("No pixel data loaded");

            ImageCreateInfo info{};
            info.format = Format::RGBA8Unorm;
            info.mipLevels = Image::CalculateMips(texWidth, texHeight);
            info.width = texWidth;
            info.height = texHeight;
            info.usage = ImageUsage::TransferSrcBit | ImageUsage::TransferDstBit | ImageUsage::SampledBit | ImageUsage::StorageBit;
            info.properties = MemoryProperty::DeviceLocalBit;
            info.initialLayout = ImageLayout::Preinitialized;
            info.name = "Deferred_ibl_brdf_lut_image";
            ibl_brdf_lut = Image::Create(info);

            ibl_brdf_lut->CreateSRV(ImageViewType::Type2D);

            SamplerCreateInfo samplerInfo{};
            samplerInfo.maxLod = static_cast<float>(info.mipLevels);
            ibl_brdf_lut->sampler = Sampler::Create(samplerInfo);

            cmd->CopyDataToImageStaged(ibl_brdf_lut, pixels, texWidth * texHeight * STBI_rgb_alpha);

            Image::uniqueImages[hash] = ibl_brdf_lut;
            cmd->AddAfterWaitCallback([pixels]()
                                      { stbi_image_free(pixels); });
        }

        uniform = Buffer::Create(
            RHII.AlignUniform(sizeof(UBO)) * SWAPCHAIN_IMAGES,
            BufferUsage::UniformBufferBit,
            AllocationCreate::HostAccessSequentialWriteBit,
            "Deferred_uniform_buffer");
        uniform->Map();
        uniform->Zero();
        uniform->Flush();
        uniform->Unmap();

        std::vector<DescriptorBindingInfo> bindingInfos(10);

        bindingInfos[0].binding = 0;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        bindingInfos[0].imageLayout = ImageLayout::DepthStencilReadOnly;

        bindingInfos[1].binding = 1;
        bindingInfos[1].type = DescriptorType::CombinedImageSampler;
        bindingInfos[1].imageLayout = ImageLayout::ShaderReadOnly;

        bindingInfos[2].binding = 2;
        bindingInfos[2].type = DescriptorType::CombinedImageSampler;
        bindingInfos[2].imageLayout = ImageLayout::ShaderReadOnly;

        bindingInfos[3].binding = 3;
        bindingInfos[3].type = DescriptorType::CombinedImageSampler;
        bindingInfos[3].imageLayout = ImageLayout::ShaderReadOnly;

        bindingInfos[4].binding = 4;
        bindingInfos[4].type = DescriptorType::UniformBufferDynamic;

        bindingInfos[5].binding = 5;
        bindingInfos[5].type = DescriptorType::CombinedImageSampler;
        bindingInfos[5].imageLayout = ImageLayout::ShaderReadOnly;

        bindingInfos[6].binding = 6;
        bindingInfos[6].type = DescriptorType::CombinedImageSampler;
        bindingInfos[6].imageLayout = ImageLayout::ShaderReadOnly;

        bindingInfos[7].binding = 7;
        bindingInfos[7].type = DescriptorType::CombinedImageSampler;
        bindingInfos[7].imageLayout = ImageLayout::ShaderReadOnly;

        bindingInfos[8].binding = 8;
        bindingInfos[8].type = DescriptorType::CombinedImageSampler;
        bindingInfos[8].imageLayout = ImageLayout::ShaderReadOnly;

        bindingInfos[9].binding = 9;
        bindingInfos[9].type = DescriptorType::UniformBufferDynamic;

        DSet = Descriptor::Create(bindingInfos, ShaderStage::FragmentBit, "Deferred_Composition_descriptor");

        UpdateDescriptorSets();
    }

    void Deferred::UpdateDescriptorSets()
    {
        DSet->SetImage(0, depth->GetSRV(), depth->sampler->Handle());
        DSet->SetImage(1, normalRT->GetSRV(), normalRT->sampler->Handle());
        DSet->SetImage(2, albedoRT->GetSRV(), albedoRT->sampler->Handle());
        DSet->SetImage(3, srmRT->GetSRV(), srmRT->sampler->Handle());
        DSet->SetBuffer(4, CONTEXT->GetSystem<LightSystem>()->GetUniform());
        DSet->SetImage(5, ssaoBlurRT->GetSRV(), ssaoBlurRT->sampler->Handle());
        DSet->SetImage(6, ssrRT->GetSRV(), ssrRT->sampler->Handle());
        DSet->SetImage(7, emissiveRT->GetSRV(), emissiveRT->sampler->Handle());
        DSet->SetImage(8, ibl_brdf_lut->GetSRV(), ibl_brdf_lut->sampler->Handle());
        DSet->SetBuffer(9, uniform);
        DSet->Update();
    }

    void Deferred::Update(Camera *camera)
    {
        ubo.screenSpace[0] = {camera->invViewProjection[0]};
        ubo.screenSpace[1] = {camera->invViewProjection[1]};
        ubo.screenSpace[2] = {camera->invViewProjection[2]};
        ubo.screenSpace[3] = {camera->invViewProjection[3]};
        ubo.screenSpace[4] = {
            static_cast<float>(GUI::show_ssao), static_cast<float>(GUI::show_ssr),
            static_cast<float>(GUI::show_tonemapping), static_cast<float>(GUI::use_FSR2)};
        ubo.screenSpace[5] = {
            static_cast<float>(GUI::use_IBL), static_cast<float>(GUI::use_Volumetric_lights),
            static_cast<float>(GUI::volumetric_steps), static_cast<float>(GUI::volumetric_dither_strength)};
        ubo.screenSpace[6] = {GUI::fog_global_thickness, GUI::lights_intensity, GUI::lights_range, GUI::fog_max_height};
        ubo.screenSpace[7] = {
            GUI::fog_ground_thickness, static_cast<float>(GUI::use_fog), static_cast<float>(GUI::shadow_cast), 0.0f};

        MemoryRange mr{};
        mr.data = &ubo;
        mr.size = sizeof(UBO);
        mr.offset = RHII.GetFrameDynamicOffset(uniform->Size(), RHII.GetFrameIndex());
        uniform->Copy(1, &mr, false);
    }

    void Deferred::Draw(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->BeginDebugRegion("Composition");

        Shadows &shadows = *WORLD_ENTITY->GetComponent<Shadows>();
        RendererSystem &rs = *CONTEXT->GetSystem<RendererSystem>();

        // SKYBOX
        SkyBox &skybox = GUI::shadow_cast ? rs.skyBoxDay : rs.skyBoxNight;

        mat4 values{};
        values[0] = shadows.viewZ;
        values[1] = vec4(GUI::shadow_cast);
        std::vector<Descriptor *> handles{DSet, shadows.DSetDeferred, skybox.DSet};

        // COMBINE
        // Input
        cmd->ImageBarrier(normalRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(albedoRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(srmRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(velocityRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(emissiveRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(ssaoBlurRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(ssrRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(ibl_brdf_lut, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(skybox.cubeMap, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(depth, ImageLayout::DepthStencilReadOnly);
        for (auto &shadowMap : shadows.textures)
            cmd->ImageBarrier(shadowMap, ImageLayout::DepthStencilReadOnly);
        // Output
        cmd->ImageBarrier(viewportRT, ImageLayout::ColorAttachment);

        AttachmentInfo info{};
        info.image = viewportRT;
        info.initialLayout = viewportRT->GetCurrentLayout();
        info.finalLayout = ImageLayout::ColorAttachment;

        cmd->BeginPass(1, &info, nullptr, &pipelineInfo->renderPass);
        cmd->BindPipeline(*pipelineInfo, &pipeline);
        cmd->SetViewport(0.f, 0.f, viewportRT->width_f, viewportRT->height_f);
        cmd->SetScissor(0, 0, viewportRT->imageInfo.width, viewportRT->imageInfo.height);
        cmd->PushConstants(pipeline, ShaderStage::FragmentBit, 0, sizeof(mat4), &values);
        cmd->BindDescriptors(pipeline, (uint32_t)handles.size(), handles.data());
        cmd->Draw(3, 1, 0, 0);
        cmd->EndPass();

        cmd->EndDebugRegion();
    }

    void Deferred::Resize(uint32_t width, uint32_t height)
    {
        Init();
        UpdateDescriptorSets();
    }

    void Deferred::BeginPass(CommandBuffer *cmd, uint32_t imageIndex)
    {
        cmd->ImageBarrier(normalRT, ImageLayout::ColorAttachment);
        cmd->ImageBarrier(albedoRT, ImageLayout::ColorAttachment);
        cmd->ImageBarrier(srmRT, ImageLayout::ColorAttachment);
        cmd->ImageBarrier(velocityRT, ImageLayout::ColorAttachment);
        cmd->ImageBarrier(emissiveRT, ImageLayout::ColorAttachment);
        cmd->ImageBarrier(depth, ImageLayout::DepthStencilAttachment);

        // Must be in order as they appear in shader and in pipeline
        AttachmentInfo colorInfos[5]{};
        colorInfos[0].image = normalRT;
        colorInfos[0].initialLayout = normalRT->GetCurrentLayout();
        colorInfos[0].finalLayout = ImageLayout::ColorAttachment;
        colorInfos[1].image = albedoRT;
        colorInfos[1].initialLayout = albedoRT->GetCurrentLayout();
        colorInfos[1].finalLayout = ImageLayout::ColorAttachment;
        colorInfos[2].image = srmRT;
        colorInfos[2].initialLayout = srmRT->GetCurrentLayout();
        colorInfos[2].finalLayout = ImageLayout::ColorAttachment;
        colorInfos[3].image = velocityRT;
        colorInfos[3].initialLayout = velocityRT->GetCurrentLayout();
        colorInfos[3].finalLayout = ImageLayout::ColorAttachment;
        colorInfos[4].image = emissiveRT;
        colorInfos[4].initialLayout = emissiveRT->GetCurrentLayout();
        colorInfos[4].finalLayout = ImageLayout::ColorAttachment;

        AttachmentInfo depthInfo{};
        depthInfo.image = depth;
        depthInfo.initialLayout = depth->GetCurrentLayout();
        depthInfo.finalLayout = ImageLayout::DepthStencilAttachment;

        cmd->BeginPass(5, colorInfos, &depthInfo, &m_renderPassModels);
        cmd->SetViewport(0.f, 0.f, normalRT->width_f, normalRT->height_f);
        cmd->SetScissor(0, 0, normalRT->imageInfo.width, normalRT->imageInfo.height);
    }

    void Deferred::EndPass(CommandBuffer *cmd)
    {
        cmd->EndPass();

        cmd->ImageBarrier(albedoRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(normalRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(velocityRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(emissiveRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(srmRT, ImageLayout::ShaderReadOnly);
        cmd->ImageBarrier(depth, ImageLayout::DepthStencilReadOnly);
    }

    void Deferred::Destroy()
    {
        Descriptor::Destroy(DSet);
        Buffer::Destroy(uniform);
        Pipeline::Destroy(pipeline);
    }
}
#endif
