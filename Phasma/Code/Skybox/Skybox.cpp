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

#include "Skybox.h"
#include "Renderer/Pipeline.h"
#include "GUI/GUI.h"
#include "tinygltf/stb_image.h"
#include "Renderer/RHI.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"

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
        descriptorSet = Descriptor::Create(Pipeline::getDescriptorSetLayoutSkybox());

        DescriptorUpdateInfo info{};
        info.binding = 0;
        info.pImage = texture;
        descriptorSet->UpdateDescriptor(1, &info);
    }

    void SkyBox::loadSkyBox(const std::array<std::string, 6> &textureNames, uint32_t imageSideSize, bool show)
    {
        loadTextures(textureNames, imageSideSize);
    }

    // images must be squared and the image size must be the real else the assertion will fail
    void SkyBox::loadTextures(const std::array<std::string, 6> &paths, int imageSideSize)
    {
        assert(paths.size() == 6);

        ImageCreateInfo info{};
        info.format = VK_FORMAT_R8G8B8A8_UNORM;
        info.arrayLayers = 6;
        info.imageFlags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        info.width = imageSideSize;
        info.height = imageSideSize;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        texture = Image::Create(info);

        ImageViewCreateInfo viewInfo{};
        viewInfo.image = texture;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        texture->CreateImageView(viewInfo);

        SamplerCreateInfo samplerInfo{};
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        texture->CreateSampler(samplerInfo);

        texture->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        for (uint32_t i = 0; i < texture->imageInfo.arrayLayers; ++i)
        {
            // Texture Load
            int texWidth, texHeight, texChannels;
            //stbi_set_flip_vertically_on_load(true);
            stbi_uc *pixels = stbi_load(paths[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            assert(imageSideSize == texWidth && imageSideSize == texHeight);

            VkDeviceSize imageSize = static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight) * 4;
            if (!pixels)
                PE_ERROR("No pixel data loaded");

            Buffer *staging = Buffer::Create(
                imageSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
            );
            staging->Map();
            staging->CopyData(pixels);
            staging->Flush();
            staging->Unmap();

            stbi_image_free(pixels);

            texture->CopyBufferToImage(staging, i);
            staging->Destroy();
        }

        texture->TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void SkyBox::destroy()
    {
        texture->Destroy();
        Pipeline::getDescriptorSetLayoutSkybox()->Destroy();
    }
}
