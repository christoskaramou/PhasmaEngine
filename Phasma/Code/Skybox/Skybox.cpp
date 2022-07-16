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
        descriptorSet = {};
    }

    SkyBox::~SkyBox()
    {
    }

    void SkyBox::createDescriptorSet()
    {
        DescriptorBindingInfo bindingInfo{};
        bindingInfo.binding = 0;
        bindingInfo.type = DescriptorType::CombinedImageSampler;
        bindingInfo.imageLayout = ImageLayout::ShaderReadOnly;
        bindingInfo.pImage = texture;

        DescriptorInfo info{};
        info.count = 1;
        info.bindingInfos = &bindingInfo;
        info.stage = ShaderStage::FragmentBit;

        descriptorSet = Descriptor::Create(&info, "Skybox_descriptor");
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
        texture = Image::Create(info);

        ImageViewCreateInfo viewInfo{};
        viewInfo.image = texture;
        viewInfo.viewType = ImageViewType::TypeCube;
        texture->CreateImageView(viewInfo);

        SamplerCreateInfo samplerInfo{};
        samplerInfo.addressModeU = SamplerAddressMode::ClampToEdge;
        samplerInfo.addressModeV = SamplerAddressMode::ClampToEdge;
        samplerInfo.addressModeW = SamplerAddressMode::ClampToEdge;
        texture->CreateSampler(samplerInfo);

        for (uint32_t i = 0; i < texture->imageInfo.arrayLayers; ++i)
        {
            // Texture Load
            int texWidth, texHeight, texChannels;
            // stbi_set_flip_vertically_on_load(true);
            stbi_uc *pixels = stbi_load(paths[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            assert(imageSideSize == texWidth && imageSideSize == texHeight);

            if (!pixels)
                PE_ERROR("No pixel data loaded");

            size_t size = static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight) * 4;
            cmd->CopyDataToImageStaged(texture, pixels, size, i);
            cmd->AddAfterWaitCallback([pixels]()
                                     { stbi_image_free(pixels); });
        }
    }

    void SkyBox::destroy()
    {
        Image::Destroy(texture);
        Descriptor::Destroy(descriptorSet);
    }
}
