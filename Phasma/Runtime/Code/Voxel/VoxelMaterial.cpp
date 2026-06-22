#include "Voxel/VoxelMaterial.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Sampler.h"
#include "Base/Path.h"
#include "Base/ResourceManager.h"
#include "Base/Settings.h"
#include "Scene/PassInfoAsset.h"
#include "Scene/Scene.h"
#include "stb_image.h"

namespace pe::voxel
{
    namespace
    {
        constexpr const char *kVoxelPassInfo = "Shaders/Voxel/voxel_gbuffer.passinfo";
        constexpr const char *kAtlasTextureName = "gVoxelAtlas";

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

        void TransitionAtlasToShaderRead(CommandBuffer *cmd, Image *image)
        {
            ImageBarrierInfo barrier{};
            barrier.image = image;
            barrier.layout = PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.stageFlags = PE_STAGE_FRAGMENT_SHADER |
                                 PE_STAGE_COMPUTE_SHADER |
                                 PE_STAGE_RAY_TRACING_SHADER_KHR;
            barrier.accessMask = PE_ACCESS_SHADER_READ;
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

        void UpdateAtlasNamedTextureIndex(Scene *scene, Material *material, const ResourceHandle<Image> &atlas)
        {
            material->namedTextureIndices[kAtlasTextureName] = 0xFFFFFFFF;
            if (!atlas || !atlas->GetSRV())
                return;

            const std::vector<ImageView *> &imageViews = scene->GetImageViews();
            for (uint32_t index = 0; index < static_cast<uint32_t>(imageViews.size()); ++index)
            {
                if (imageViews[index] == atlas->GetSRV())
                {
                    material->namedTextureIndices[kAtlasTextureName] = index;
                    return;
                }
            }
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

            // Single mip (Phase 1): the engine's runtime mip-gen uses a compute UAV, but DX12 forbids
            // a UAV on an SRGB format. Proper mips later via a UNORM resource + SRGB sample view.
            const uint32_t mipLevels = 1u;

            ImageDesc desc{};
            desc.format = PE_FORMAT_R8G8B8A8_SRGB;
            desc.width = static_cast<uint32_t>(atlasWidth);
            desc.height = static_cast<uint32_t>(atlasHeight);
            desc.mipLevels = mipLevels;
            desc.arrayLayers = static_cast<uint32_t>(tilePngPaths.size());
            // No STORAGE/UAV: the atlas is sampled-only (single mip), and DX12 forbids UAV on an SRGB
            // format (CreateResource fails -> device removed).
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
            samplerInfo.mipLodBias = log2(Settings::Get<GlobalSettings>().render_scale) - 1.0f;
            samplerInfo.maxLod = static_cast<float>(mipLevels);
            samplerInfo.borderColor = PE_SAMPLER_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
            rawAtlas->SetSampler(Sampler::Create(samplerInfo, "VoxelMaterial_AtlasSampler"));

            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            cmd->Begin();
            for (uint32_t layer = 0; layer < rawAtlas->GetArrayLayers(); ++layer)
            {
                size_t size = static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * STBI_rgb_alpha;
                cmd->CopyDataToImageStaged(rawAtlas, pixels[layer], size, layer, 1);
            }
            TransitionAtlasToShaderRead(cmd, rawAtlas);
            cmd->AddAfterWaitCallback([pixels = std::move(pixels)]() mutable
                                      { FreePixels(pixels); });
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
        m_material.reset();
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

        m_material = std::make_unique<Material>();
        m_material->name = "VoxelMaterial";
        m_material->passInfoAsset = ResourceManager::Get().Load<PassInfoAsset>(Path::RuntimeAssets + kVoxelPassInfo);
        m_material->namedTextures[kAtlasTextureName] = m_atlas;
        m_material->dirty = true;

        if (RHII.GetMainQueue())
        {
            scene->UpdateTextures();
            UpdateAtlasNamedTextureIndex(scene, m_material.get(), m_atlas);
        }
        else
            PE_WARN("[VoxelMaterial] Cannot refresh scene texture bindings without a main queue.");
    }

    Material *VoxelMaterial::Get() const
    {
        return m_material.get();
    }

    ResourceHandle<Image> VoxelMaterial::Atlas() const
    {
        return m_atlas;
    }
} // namespace pe::voxel
