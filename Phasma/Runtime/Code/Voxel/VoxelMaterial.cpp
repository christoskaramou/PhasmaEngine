#include "Voxel/VoxelMaterial.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Sampler.h"
#include "Scene/Scene.h"
#include "stb_image.h"

namespace pe::voxel
{
    namespace
    {
        std::string MakeAtlasResourceId(const std::vector<std::string> &tilePngPaths)
        {
            std::string id = "VoxelMaterial.Atlas";
            for (const std::string &path : tilePngPaths)
            {
                id += "|";
                id += path;
            }
            return id;
        }

        void FreePixels(std::vector<stbi_uc *> &pixels)
        {
            for (stbi_uc *pixelData : pixels)
                stbi_image_free(pixelData);
            pixels.clear();
        }

        uint32_t MipExtent(uint32_t extent, uint32_t mipLevel)
        {
            return std::max(extent >> mipLevel, 1u);
        }

        size_t Rgba8Size(uint32_t width, uint32_t height)
        {
            return static_cast<size_t>(width) * static_cast<size_t>(height) * STBI_rgb_alpha;
        }

        std::vector<stbi_uc> DownsampleRgba8Box(const stbi_uc *src,
                                                uint32_t srcWidth,
                                                uint32_t srcHeight,
                                                uint32_t dstWidth,
                                                uint32_t dstHeight)
        {
            // Straight RGBA8 averaging is enough for the Phase-1 placeholder atlas tiles.
            std::vector<stbi_uc> dst(Rgba8Size(dstWidth, dstHeight));

            for (uint32_t y = 0; y < dstHeight; ++y)
            {
                const uint32_t y0 = std::min(y * 2u, srcHeight - 1u);
                const uint32_t y1 = std::min(y0 + 1u, srcHeight - 1u);

                for (uint32_t x = 0; x < dstWidth; ++x)
                {
                    const uint32_t x0 = std::min(x * 2u, srcWidth - 1u);
                    const uint32_t x1 = std::min(x0 + 1u, srcWidth - 1u);
                    const size_t dstIndex = (static_cast<size_t>(y) * dstWidth + x) * STBI_rgb_alpha;
                    const size_t src00 = (static_cast<size_t>(y0) * srcWidth + x0) * STBI_rgb_alpha;
                    const size_t src10 = (static_cast<size_t>(y0) * srcWidth + x1) * STBI_rgb_alpha;
                    const size_t src01 = (static_cast<size_t>(y1) * srcWidth + x0) * STBI_rgb_alpha;
                    const size_t src11 = (static_cast<size_t>(y1) * srcWidth + x1) * STBI_rgb_alpha;

                    for (uint32_t channel = 0; channel < STBI_rgb_alpha; ++channel)
                    {
                        const uint32_t sum = static_cast<uint32_t>(src[src00 + channel]) +
                                             static_cast<uint32_t>(src[src10 + channel]) +
                                             static_cast<uint32_t>(src[src01 + channel]) +
                                             static_cast<uint32_t>(src[src11 + channel]);
                        dst[dstIndex + channel] = static_cast<stbi_uc>((sum + 2u) / 4u);
                    }
                }
            }

            return dst;
        }

        void TransitionAtlasToShaderRead(CommandBuffer *cmd, Image *image)
        {
            ImageBarrierInfo barrier{};
            barrier.image = image;
            barrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.stageFlags = PE_STAGE_FRAGMENT_SHADER |
                                 PE_STAGE_COMPUTE_SHADER |
                                 PE_STAGE_RAY_TRACING_SHADER_KHR;
            barrier.accessMask = PE_ACCESS_SHADER_SAMPLED_READ;
            barrier.arrayLayers = image->GetArrayLayers();
            barrier.mipLevels = image->GetMipLevels();
            cmd->ImageBarrier(barrier);
        }

        bool SceneHasImage(Scene *scene, Image *image)
        {
            for (const ResourceHandle<Image> &storedImage : scene->GetImageStore())
            {
                if (storedImage.get() == image)
                    return true;
            }
            return false;
        }

        void StoreAtlas(Scene *scene, const ResourceHandle<Image> &atlas)
        {
            if (!atlas || SceneHasImage(scene, atlas.get()))
                return;

            scene->GetImageStore().push_back(atlas);
        }

        ResourceHandle<Image> CreateAtlas(const std::string &atlasId, const std::vector<std::string> &tilePngPaths)
        {
            if (ResourceHandle<Image> cached = ResourceManager::Get().Find<Image>(atlasId))
                return cached;

            std::vector<stbi_uc *> pixels(tilePngPaths.size(), nullptr);
            int atlasWidth = 0;
            int atlasHeight = 0;

            for (uint32_t layer = 0; layer < static_cast<uint32_t>(tilePngPaths.size()); ++layer)
            {
                int tileWidth = 0;
                int tileHeight = 0;
                int channels = 0;
                pixels[layer] = stbi_load(tilePngPaths[layer].c_str(), &tileWidth, &tileHeight, &channels, STBI_rgb_alpha);
                if (!pixels[layer])
                {
                    PE_WARN("[VoxelMaterial] Failed to load tile png: %s", tilePngPaths[layer].c_str());
                    FreePixels(pixels);
                    return {};
                }

                if (layer == 0)
                {
                    atlasWidth = tileWidth;
                    atlasHeight = tileHeight;
                }
                else if (tileWidth != atlasWidth || tileHeight != atlasHeight)
                {
                    PE_WARN("[VoxelMaterial] Tile dimensions do not match first tile: %s", tilePngPaths[layer].c_str());
                    FreePixels(pixels);
                    return {};
                }
            }

            if (atlasWidth <= 0 || atlasHeight <= 0)
            {
                PE_WARN("[VoxelMaterial] Invalid atlas tile dimensions.");
                FreePixels(pixels);
                return {};
            }

            Queue *queue = RHII.GetMainQueue();
            if (!queue)
            {
                PE_WARN("[VoxelMaterial] Cannot upload voxel atlas without a main queue.");
                FreePixels(pixels);
                return {};
            }

            const uint32_t atlasWidthU = static_cast<uint32_t>(atlasWidth);
            const uint32_t atlasHeightU = static_cast<uint32_t>(atlasHeight);
            const uint32_t mipLevels = Image::CalculateMips(atlasWidthU, atlasHeightU);

            ImageDesc desc{};
            desc.format = PE_FORMAT_R8G8B8A8_SRGB;
            desc.width = atlasWidthU;
            desc.height = atlasHeightU;
            desc.mipLevels = mipLevels;
            desc.arrayLayers = static_cast<uint32_t>(tilePngPaths.size());
            // No STORAGE/UAV: CPU-generated mips keep the SRGB atlas DX12-safe.
            desc.usage = PE_IMAGE_USAGE_TRANSFER_SRC |
                         PE_IMAGE_USAGE_TRANSFER_DST |
                         PE_IMAGE_USAGE_SAMPLED;
            desc.initialLayout = PE_IMAGE_LAYOUT_UNDEFINED;
            desc.imageType = PE_IMAGE_TYPE_2D;
            desc.name = atlasId;

            Image *rawAtlas = Image::Create(desc);
            rawAtlas->SetClearColor(Color::Transparent);
            rawAtlas->CreateSRV(PE_IMAGE_VIEW_TYPE_2D_ARRAY);

            SamplerDesc samplerInfo = Sampler::CreateInfoInit();
            samplerInfo.mipLodBias = log2(Settings::Get<SceneSettings>().render_scale) - 1.0f;
            samplerInfo.maxLod = static_cast<float>(mipLevels);
            samplerInfo.borderColor = PE_SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
            rawAtlas->SetSampler(Sampler::Create(samplerInfo, "VoxelMaterial_AtlasSampler"));

            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            std::vector<std::vector<stbi_uc>> mipPixels;
            mipPixels.reserve(static_cast<size_t>(rawAtlas->GetArrayLayers()) * static_cast<size_t>(mipLevels - 1u));
            cmd->Begin();
            for (uint32_t layer = 0; layer < rawAtlas->GetArrayLayers(); ++layer)
            {
                const stbi_uc *srcMip = pixels[layer];
                uint32_t srcWidth = atlasWidthU;
                uint32_t srcHeight = atlasHeightU;

                for (uint32_t mipLevel = 0; mipLevel < mipLevels; ++mipLevel)
                {
                    const uint32_t mipWidth = MipExtent(atlasWidthU, mipLevel);
                    const uint32_t mipHeight = MipExtent(atlasHeightU, mipLevel);
                    stbi_uc *mipData = pixels[layer];

                    if (mipLevel > 0)
                    {
                        mipPixels.emplace_back(DownsampleRgba8Box(srcMip, srcWidth, srcHeight, mipWidth, mipHeight));
                        mipData = mipPixels.back().data();
                    }

                    cmd->CopyDataToImageStaged(rawAtlas, mipData, Rgba8Size(mipWidth, mipHeight), layer, 1, mipLevel);
                    srcMip = mipData;
                    srcWidth = mipWidth;
                    srcHeight = mipHeight;
                }
            }
            TransitionAtlasToShaderRead(cmd, rawAtlas);
            cmd->AddAfterWaitCallback([pixels = std::move(pixels), mipPixels = std::move(mipPixels)]() mutable
                                      {
                                          mipPixels.clear();
                                          FreePixels(pixels); });
            cmd->End();

            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            cmd->Return();

            std::shared_ptr<Image> atlas(rawAtlas, [](Image *image)
                                         { Image::Destroy(image); });
            ResourceManager::Get().Register<Image>(atlasId, atlas);
            return ResourceHandle<Image>(atlas);
        }
    } // namespace

    void VoxelMaterial::Build(Scene *scene, const std::vector<std::string> &tilePngPaths)
    {
        m_atlas = {};

        if (!scene)
        {
            PE_WARN("[VoxelMaterial] Cannot build without a scene.");
            return;
        }

        if (tilePngPaths.empty())
        {
            PE_WARN("[VoxelMaterial] Cannot build without tile png paths.");
            return;
        }

        const std::string atlasId = MakeAtlasResourceId(tilePngPaths);
        m_atlas = CreateAtlas(atlasId, tilePngPaths);
        if (!m_atlas)
            return;

        StoreAtlas(scene, m_atlas);

        // Rebuild material/mesh-constant tables so the host material + atlas image are registered
        // before VoxelWorld reserves arena capacity. The atlas SRV itself is bound via
        // Scene::SetVoxelAtlasView, not through this table.
        if (RHII.GetMainQueue())
            scene->UpdateTextures();
        else
            PE_WARN("[VoxelMaterial] Cannot refresh scene texture bindings without a main queue.");
    }

    ResourceHandle<Image> VoxelMaterial::Atlas() const
    {
        return m_atlas;
    }
} // namespace pe::voxel
