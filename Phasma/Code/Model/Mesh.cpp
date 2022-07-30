#include "tinygltf/stb_image.h"
#include "Mesh.h"
#include "Renderer/Pipeline.h"
#include "Renderer/RHI.h"
#include "Renderer/Descriptor.h"
#include "Renderer/Image.h"
#include "Renderer/Buffer.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"

namespace pe
{
    Primitive::Primitive() : pbrMaterial({})
    {
        uniformBufferIndex = std::numeric_limits<size_t>::max();
        uniformBufferOffset = 0;
    }

    Primitive::~Primitive()
    {
    }

    Mesh::Mesh()
    {
        uniformBufferIndex = std::numeric_limits<size_t>::max();
        uniformBufferOffset = 0;
    }

    Mesh::~Mesh()
    {
    }

    void Mesh::SetUniformOffsets(size_t uniformBufferIndex)
    {
        this->uniformBufferIndex = uniformBufferIndex;
        auto &uniformBuffer = RHII.GetUniformBufferInfo(uniformBufferIndex);
        uniformBufferOffset = uniformBuffer.size;
        uniformBuffer.size += sizeof(mat4) * (2 + meshData.jointMatrices.size());

        for (auto &primitive : primitives)
        {
            primitive.uniformBufferIndex = uniformBufferIndex;
            primitive.uniformBufferOffset = uniformBuffer.size;
            uniformBuffer.size += sizeof(mat4);
        }
    }

    void Primitive::loadTexture(CommandBuffer *cmd,
                                MaterialType type,
                                const std::filesystem::path &file,
                                const Microsoft::glTF::Image *image,
                                const Microsoft::glTF::Document *document,
                                const Microsoft::glTF::GLTFResourceReader *resourceReader)
    {
        std::filesystem::path path = file.parent_path();
        if (image)
            path /= image->uri;

        // get the correct texture reference
        Image **tex;
        switch (type)
        {
        case MaterialType::BaseColor:
            tex = &pbrMaterial.baseColorTexture;
            if (!image || image->uri.empty())
                path = Path::Assets + "Objects/black.png";
            break;
        case MaterialType::MetallicRoughness:
            tex = &pbrMaterial.metallicRoughnessTexture;
            if (!image || image->uri.empty())
                path = Path::Assets + "Objects/black.png";
            break;
        case MaterialType::Normal:
            tex = &pbrMaterial.normalTexture;
            if (!image || image->uri.empty())
                path = Path::Assets + "Objects/normal.png";
            break;
        case MaterialType::Occlusion:
            tex = &pbrMaterial.occlusionTexture;
            if (!image || image->uri.empty())
                path = Path::Assets + "Objects/white.png";
            break;
        case MaterialType::Emissive:
            tex = &pbrMaterial.emissiveTexture;
            if (!image || image->uri.empty())
                path = Path::Assets + "Objects/black.png";
            break;
        default:
            PE_ERROR("Load texture invalid type");
        }

        // Check if it is already loaded
        StringHash hash(path.string());
        if (Image::uniqueImages.find(hash) == Image::uniqueImages.end())
        {
            int texWidth, texHeight, texChannels;
            unsigned char *pixels;
            // stbi_set_flip_vertically_on_load(true);
            if (!image || image->bufferViewId.empty())
                pixels = stbi_load(path.string().c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            else
            {
                const auto data = resourceReader->ReadBinaryData(*document, *image);
                pixels = stbi_load_from_memory(
                    data.data(), static_cast<int>(data.size()), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            }

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
            info.name = image ? image->uri : path.filename().string();
            *tex = Image::Create(info);

            (*tex)->CreateSRV(ImageViewType::Type2D);

            SamplerCreateInfo samplerInfo{};
            samplerInfo.maxLod = static_cast<float>(info.mipLevels);
            (*tex)->CreateSampler(samplerInfo);

            cmd->CopyDataToImageStaged(*tex, pixels, texWidth * texHeight * STBI_rgb_alpha);
            cmd->AddAfterWaitCallback([pixels]()
                                      { stbi_image_free(pixels); });

            Image::uniqueImages[hash] = *tex;
        }
        else
        {
            *tex = Image::uniqueImages[hash];
        }

        images.push_back(*tex);
    }

    void Mesh::destroy()
    {
        vertices.clear();
        vertices.shrink_to_fit();
        indices.clear();
        indices.shrink_to_fit();
    }
}
