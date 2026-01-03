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

        vk::ImageCreateInfo info = Image::CreateInfoInit();
        info.flags = vk::ImageCreateFlagBits::eCubeCompatible;
        info.format = vk::Format::eR8G8B8A8Unorm;
        info.extent = vk::Extent3D{static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1};
        info.arrayLayers = 6;
        info.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
        m_cubeMap = Image::Create(info, "Skybox_Cubemap_Legacy");

        m_cubeMap->CreateSRV(vk::ImageViewType::eCube);

        vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
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
        vk::ImageCreateInfo info = Image::CreateInfoInit();
        info.flags = vk::ImageCreateFlagBits::eCubeCompatible;
        info.format = vk::Format::eR32G32B32A32Sfloat;
        info.extent = vk::Extent3D{cubemapSize, cubemapSize, 1};
        info.arrayLayers = 6;
        info.mipLevels = Image::CalculateMips(cubemapSize, cubemapSize);
        info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
        m_cubeMap = Image::Create(info, "Skybox_Cubemap_Converted");

        m_cubeMap->CreateSRV(vk::ImageViewType::eCube);
        m_cubeMap->CreateUAV(vk::ImageViewType::e2DArray, 0);

        vk::SamplerCreateInfo samplerInfo = Sampler::CreateInfoInit();
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        m_cubeMap->SetSampler(Sampler::Create(samplerInfo));

        // 3. Setup Compute Pass
        PassInfo *passInfo = new PassInfo();
        passInfo->pCompShader = Shader::Create(Path::Executable + "Assets/Shaders/Compute/EquirectangularToCubemap.hlsl", vk::ShaderStageFlagBits::eCompute, "main", std::vector<Define>{}, ShaderCodeType::HLSL);
        passInfo->Update();

        // 5. Barriers - Image Layout Transitions
        ImageBarrierInfo barrier{};
        barrier.image = m_cubeMap;
        barrier.layout = vk::ImageLayout::eGeneral;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eComputeShader;
        barrier.accessMask = vk::AccessFlagBits2::eShaderWrite;
        barrier.baseArrayLayer = 0;
        barrier.arrayLayers = m_cubeMap->GetArrayLayers();
        barrier.baseMipLevel = 0;
        barrier.mipLevels = m_cubeMap->GetMipLevels();
        cmd->ImageBarrier(barrier);

        // 4. Update Descriptors
        auto &descriptors = passInfo->GetDescriptors(RHII.GetFrameIndex());
        Descriptor *descriptor = descriptors[0];
        descriptor->SetImageView(0, m_cubeMap->GetUAV(0), nullptr);
        descriptor->SetImageView(1, equiImage->GetSRV(), nullptr);
        descriptor->SetSampler(2, equiImage->GetSampler()->ApiHandle());
        descriptor->Update();

        // 6. Dispatch
        cmd->BindPipeline(*passInfo); // Internally calls BindDescriptors if updated
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
