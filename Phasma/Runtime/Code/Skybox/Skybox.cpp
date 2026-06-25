#include "Skybox.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Shader.h"

#include "API/RHI.h"
#include "stb_image.h"

namespace pe
{
    namespace
    {
        Sampler *CreateSkyboxSampler(const std::string &name)
        {
            SamplerDesc samplerInfo = Sampler::CreateInfoInit();
            samplerInfo.addressModeU = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            return Sampler::Create(samplerInfo, name);
        }

        void TransitionSkyboxToShaderRead(CommandBuffer *cmd, Image *image)
        {
            ImageBarrierInfo barrier{};
            barrier.image = image;
            barrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.stageFlags = PE_STAGE_FRAGMENT_SHADER | PE_STAGE_RAY_TRACING_SHADER_KHR;
            barrier.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
            barrier.mipLevels = image->GetMipLevels();
            cmd->ImageBarrier(barrier);
        }

        struct PrefilterPushConstants
        {
            uint32_t mipLevel = 0;
            uint32_t faceSize = 1;
            uint32_t sampleCount = 1;
            float roughness = 0.0f;
        };

        static_assert(sizeof(PrefilterPushConstants) == 16);

        uint32_t PrefilterSampleCount(float roughness)
        {
            // Skybox prefiltering runs once at load, so sample counts are
            // generous: more samples shrink the gaps between importance-sample
            // directions that let a sub-texel sun fall through and alias into a
            // lattice of bright squares on rough reflections.
            if (roughness < 0.2f)
                return 64;
            if (roughness < 0.5f)
                return 128;
            if (roughness < 0.75f)
                return 256;
            return 512;
        }

        void PrefilterSkyboxMips(CommandBuffer *cmd, Image *cubeMap, Image *equiImage)
        {
            const uint32_t mipLevels = cubeMap->GetMipLevels();
            if (mipLevels <= 1)
                return;

            for (uint32_t mip = 0; mip < mipLevels; ++mip)
            {
                if (!cubeMap->HasUAV(mip))
                    cubeMap->CreateUAV(PE_IMAGE_VIEW_TYPE_2D_ARRAY, mip);
            }

            PassInfo *passInfo = new PassInfo();
            passInfo->name = "Skybox_PrefilterCubemap_pipeline";
            passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Compute/PrefilterCubemap.hlsl", .entryPoint = "main", .stage = PE_SHADER_STAGE_COMPUTE, .defines = std::vector<Define>{}});
            passInfo->Update();

            auto &descriptors = passInfo->GetDescriptors(RHII.GetFrameIndex());
            Descriptor *descriptor = descriptors[0];

            std::vector<ImageView *> mipViews(mipLevels);
            for (uint32_t mip = 0; mip < mipLevels; ++mip)
                mipViews[mip] = cubeMap->GetUAV(mip);

            descriptor->SetImageViews(0, mipViews);
            descriptor->SetImageView(1, equiImage->GetSRV());
            descriptor->SetSampler(2, equiImage->GetSampler());
            descriptor->Update();

            cmd->BeginDebugRegion("Skybox Prefilter");
            cmd->BindPipeline(*passInfo);
            for (uint32_t mip = 1; mip < mipLevels; ++mip)
            {
                const float roughness = static_cast<float>(mip) / static_cast<float>(mipLevels - 1);
                const uint32_t faceSize = std::max(1u, cubeMap->GetWidth() >> mip);
                PrefilterPushConstants constants{};
                constants.mipLevel = mip;
                constants.faceSize = faceSize;
                constants.sampleCount = PrefilterSampleCount(roughness);
                constants.roughness = roughness;

                cmd->SetConstants(constants);
                cmd->PushConstants();
                cmd->Dispatch((faceSize + 7u) / 8u, (faceSize + 7u) / 8u, cubeMap->GetArrayLayers());
            }
            cmd->EndDebugRegion();

            cmd->AddAfterWaitCallback([passInfo]()
                                      { delete passInfo; });
        }
    } // namespace

    bool SkyBox::LoadSkyBox(CommandBuffer *cmd, const std::array<std::string, 6> &textureNames)
    {
        int texWidth = 0;
        int texHeight = 0;
        std::array<stbi_uc *, 6> pixels{};

        for (uint32_t i = 0; i < static_cast<uint32_t>(pixels.size()); ++i)
        {
            int faceWidth = 0;
            int faceHeight = 0;
            int faceChannels = 0;
            pixels[i] = stbi_load(textureNames[i].c_str(), &faceWidth, &faceHeight, &faceChannels, STBI_rgb_alpha);
            if (!pixels[i])
            {
                PE_WARN("[SkyBox] Failed to load cubemap face: %s", textureNames[i].c_str());
                for (auto *loadedPixels : pixels)
                    stbi_image_free(loadedPixels);
                return false;
            }

            if (i == 0)
            {
                texWidth = faceWidth;
                texHeight = faceHeight;
            }
            else if (faceWidth != texWidth || faceHeight != texHeight)
            {
                PE_WARN("[SkyBox] Cubemap face dimensions do not match: %s", textureNames[i].c_str());
                for (auto *loadedPixels : pixels)
                    stbi_image_free(loadedPixels);
                return false;
            }
        }

        Destroy();

        ImageDesc desc{};
        desc.cubeCompatible = true;
        desc.format = PE_FORMAT_R8G8B8A8_UNORM;
        desc.width = static_cast<uint32_t>(texWidth);
        desc.height = static_cast<uint32_t>(texHeight);
        desc.arrayLayers = 6;
        desc.usage = PE_IMAGE_USAGE_TRANSFER_DST | PE_IMAGE_USAGE_SAMPLED;
        desc.name = "Skybox_Cubemap_Legacy";
        m_cubeMap = Image::Create(desc);

        m_cubeMap->CreateSRV(PE_IMAGE_VIEW_TYPE_CUBE);
        m_cubeMap->SetSampler(CreateSkyboxSampler("Skybox_Legacy_Sampler"));

        for (uint32_t i = 0; i < m_cubeMap->GetArrayLayers(); ++i)
        {
            size_t size = static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight) * STBI_rgb_alpha;
            cmd->CopyDataToImageStaged(m_cubeMap, pixels[i], size, i, 1);
            cmd->AddAfterWaitCallback([facePixels = pixels[i]]()
                                      { stbi_image_free(facePixels); });
        }

        TransitionSkyboxToShaderRead(cmd, m_cubeMap);
        return true;
    }

    bool SkyBox::LoadSkyBox(CommandBuffer *cmd, const std::string &path)
    {
        // 1. Load Equirectangular Image
        Image *equiImage = Image::LoadRGBA32F(cmd, path);
        if (!equiImage)
        {
            PE_WARN("[SkyBox] Failed to load skybox: %s", path.c_str());
            return false;
        }

        uint32_t cubemapSize = equiImage->GetWidth() / 4;
        if (cubemapSize == 0)
        {
            PE_WARN("[SkyBox] Invalid skybox dimensions: %s", path.c_str());
            Image::Destroy(equiImage);
            return false;
        }

        Destroy();

        // 2. Create Cubemap
        ImageDesc desc{};
        desc.cubeCompatible = true;
        desc.format = PE_FORMAT_R32G32B32A32_SFLOAT;
        desc.width = cubemapSize;
        desc.height = cubemapSize;
        desc.arrayLayers = 6;
        desc.mipLevels = Image::CalculateMips(cubemapSize, cubemapSize);
        desc.usage = PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_STORAGE | PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST;
        desc.name = "Skybox_Cubemap_Converted";
        m_cubeMap = Image::Create(desc);

        m_cubeMap->CreateSRV(PE_IMAGE_VIEW_TYPE_CUBE);
        m_cubeMap->CreateUAV(PE_IMAGE_VIEW_TYPE_2D_ARRAY, 0);

        m_cubeMap->SetSampler(CreateSkyboxSampler("Skybox_Converted_Sampler"));

        // 3. Setup Compute Pass
        PassInfo *passInfo = new PassInfo();
        passInfo->pCompShader = Shader::Create({.sourcePath = Path::RuntimeAssets + "Shaders/Compute/EquirectangularToCubemap.hlsl", .entryPoint = "main", .stage = PE_SHADER_STAGE_COMPUTE, .defines = std::vector<Define>{}});
        passInfo->Update();

        // 4. Barriers
        ImageBarrierInfo barrier{};
        barrier.image = m_cubeMap;
        barrier.layout = PE_IMAGE_LAYOUT_GENERAL;
        barrier.stageFlags = PE_STAGE_COMPUTE_SHADER;
        barrier.accessMask = PE_ACCESS_SHADER_STORAGE_WRITE;
        cmd->ImageBarrier(barrier);
        ImageBarrierInfo barrierInput{};
        barrierInput.image = equiImage;
        barrierInput.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrierInput.stageFlags = PE_STAGE_COMPUTE_SHADER;
        barrierInput.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
        cmd->ImageBarrier(barrierInput);

        // 5. Update Descriptors
        auto &descriptors = passInfo->GetDescriptors(RHII.GetFrameIndex());
        Descriptor *descriptor = descriptors[0];
        descriptor->SetImageView(0, m_cubeMap->GetUAV(0));
        descriptor->SetImageView(1, equiImage->GetSRV());
        descriptor->SetSampler(2, equiImage->GetSampler());
        descriptor->Update();

        // 6. Dispatch
        cmd->BindPipeline(*passInfo);
        uint32_t groups = (cubemapSize + 31) / 32;
        cmd->Dispatch(groups, groups, 6);

        // 7. Generate roughness-prefiltered mips for image-based lighting
        PrefilterSkyboxMips(cmd, m_cubeMap, equiImage);

        // 8. Final Barrier
        TransitionSkyboxToShaderRead(cmd, m_cubeMap);

        // 9. Cleanup
        cmd->AddAfterWaitCallback([equiImage, passInfo]()
                                  {
                                    Image *im = equiImage;
                                    Image::Destroy(im);
                                    delete passInfo; });
        return true;
    }

    void SkyBox::LoadSolidColor(CommandBuffer *cmd, const vec4 &color, const std::string &name)
    {
        Destroy();

        ImageDesc desc{};
        desc.cubeCompatible = true;
        desc.format = PE_FORMAT_R32G32B32A32_SFLOAT;
        desc.width = 1;
        desc.height = 1;
        desc.arrayLayers = 6;
        desc.usage = PE_IMAGE_USAGE_TRANSFER_DST | PE_IMAGE_USAGE_SAMPLED;
        desc.name = name;
        m_cubeMap = Image::Create(desc);

        m_cubeMap->CreateSRV(PE_IMAGE_VIEW_TYPE_CUBE);
        m_cubeMap->SetSampler(CreateSkyboxSampler(name + "_Sampler"));

        std::array<float, 4> pixel = {color.x, color.y, color.z, color.w};
        for (uint32_t i = 0; i < m_cubeMap->GetArrayLayers(); ++i)
            cmd->CopyDataToImageStaged(m_cubeMap, pixel.data(), sizeof(float) * pixel.size(), i, 1);

        TransitionSkyboxToShaderRead(cmd, m_cubeMap);
    }

    void SkyBox::Destroy()
    {
        Image::Destroy(m_cubeMap);
    }
} // namespace pe
