#include "Skybox.h"
#include "Renderer/Pipeline.h"
#include "GUI/GUI.h"
#include "tinygltf/stb_image.h"
#include "Renderer/RHI.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Renderer/Command.h"

namespace pe
{
    SkyBox::SkyBox()
    {
        DSet = {};
    }

    SkyBox::~SkyBox()
    {
    }

    void SkyBox::createDescriptorSet()
    {
        std::vector<DescriptorBindingInfo> bindingInfos(1);
        bindingInfos[0].binding = 0;
        bindingInfos[0].imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfos[0].type = DescriptorType::CombinedImageSampler;
        DSet = Descriptor::Create(bindingInfos, ShaderStage::FragmentBit, "Skybox_descriptor");

        DSet->SetImage(0, cubeMap->GetSRV(), cubeMap->sampler);
        DSet->UpdateDescriptor();
    }

    void SkyBox::loadSkyBox(CommandBuffer *cmd, const std::array<std::string, 6> &textureNames, uint32_t imageSideSize, bool show)
    {
        loadTextures(cmd, textureNames, imageSideSize);
    }

    // images must be squared and the image size must be the real else the assertion will fail
    void SkyBox::loadTextures(CommandBuffer *cmd, const std::array<std::string, 6> &paths, int imageSideSize)
    {
        assert(paths.size() == 6);

        ImageCreateInfo info{};
        info.format = Format::RGBA8Unorm;
        info.arrayLayers = 6;
        info.imageFlags = ImageCreate::CubeCompatibleBit;
        info.width = imageSideSize;
        info.height = imageSideSize;
        info.usage = ImageUsage::SampledBit | ImageUsage::TransferDstBit;
        info.properties = MemoryProperty::DeviceLocalBit;
        info.name = "skybox_image";
        cubeMap = Image::Create(info);

        cubeMap->CreateSRV(ImageViewType::TypeCube);

        SamplerCreateInfo samplerInfo{};
        samplerInfo.addressModeU = SamplerAddressMode::ClampToEdge;
        samplerInfo.addressModeV = SamplerAddressMode::ClampToEdge;
        samplerInfo.addressModeW = SamplerAddressMode::ClampToEdge;
        cubeMap->CreateSampler(samplerInfo);

        // CubeMap Load
        for (uint32_t i = 0; i < cubeMap->imageInfo.arrayLayers; ++i)
        {
            int texWidth, texHeight, texChannels;
            // stbi_set_flip_vertically_on_load(true);
            stbi_uc *pixels = stbi_load(paths[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            assert(imageSideSize == texWidth && imageSideSize == texHeight);

            if (!pixels)
                PE_ERROR("No pixel data loaded");

            size_t size = static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight) * 4;
            cmd->CopyDataToImageStaged(cubeMap, pixels, size, i, 1);
            cmd->AddAfterWaitCallback([pixels]()
                                      { stbi_image_free(pixels); });
        }
    }

    void SkyBox::destroy()
    {
        Image::Destroy(cubeMap);
        Descriptor::Destroy(DSet);
    }
}
