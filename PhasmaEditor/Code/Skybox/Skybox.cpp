#include "Skybox.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Shader.h"

#include "API/RHI.h"
#include "stb/stb_image.h"

namespace pe
{
    void SkyBox::LoadSkyBox(CommandBuffer *cmd, const std::array<std::string, 6> &textureNames)
    {
        int texWidth, texHeight, texChannels;
        // Load the first image to get dimensions
        stbi_uc *pixels = stbi_load(textureNames[0].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        PE_ERROR_IF(!pixels, "No pixel data loaded");
        stbi_image_free(pixels);

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

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.addressModeU = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        m_cubeMap->SetSampler(Sampler::Create(samplerInfo));

        for (uint32_t i = 0; i < m_cubeMap->GetArrayLayers(); ++i)
        {
            pixels = stbi_load(textureNames[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            PE_ERROR_IF(!pixels, "No pixel data loaded");

            size_t size = static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight) * STBI_rgb_alpha;
            cmd->CopyDataToImageStaged(m_cubeMap, pixels, size, i, 1);
            cmd->AddAfterWaitCallback([pixels]()
                                      { stbi_image_free(pixels); });
        }

        ImageBarrierInfo barrier{};
        barrier.image = m_cubeMap;
        barrier.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.accessMask = vk::AccessFlagBits2::eShaderRead;
        cmd->ImageBarrier(barrier);
    }

    void SkyBox::LoadSkyBox(CommandBuffer *cmd, const std::string &path)
    {
        // 1. Load Equirectangular Image
        Image *equiImage = Image::LoadRGBA32F(cmd, path);

        uint32_t cubemapSize = equiImage->GetWidth() / 4;

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

        SamplerDesc samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.addressModeU = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        m_cubeMap->SetSampler(Sampler::Create(samplerInfo));

        // 3. Setup Compute Pass
        PassInfo *passInfo = new PassInfo();
        passInfo->pCompShader = Shader::Create(Path::Assets + "Shaders/Compute/EquirectangularToCubemap.hlsl", vk::ShaderStageFlagBits::eCompute, "main", std::vector<Define>{}, ShaderCodeType::HLSL);
        passInfo->Update();

        // 4. Barriers
        ImageBarrierInfo barrier{};
        barrier.image = m_cubeMap;
        barrier.layout = vk::ImageLayout::eGeneral;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eComputeShader;
        barrier.accessMask = vk::AccessFlagBits2::eShaderWrite;
        cmd->ImageBarrier(barrier);
        ImageBarrierInfo barrierInput{};
        barrierInput.image = equiImage;
        barrierInput.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrierInput.stageFlags = vk::PipelineStageFlagBits2::eComputeShader;
        barrierInput.accessMask = vk::AccessFlagBits2::eShaderRead;
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

        // 7. Generate Mips
        cmd->GenerateMipMaps(m_cubeMap);

        // 8. Final Barrier
        barrier.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eFragmentShader;
        barrier.accessMask = vk::AccessFlagBits2::eShaderRead;
        barrier.mipLevels = m_cubeMap->GetMipLevels();
        cmd->ImageBarrier(barrier);

        // 9. Cleanup
        cmd->AddAfterWaitCallback([equiImage, passInfo]()
                                  {
                                    Image *im = equiImage;
                                    Image::Destroy(im);
                                    delete passInfo; });
    }

    void SkyBox::Destroy()
    {
        Image::Destroy(m_cubeMap);
    }
} // namespace pe
