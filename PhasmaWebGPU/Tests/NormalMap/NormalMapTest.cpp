#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"


#include <cmath>
#include <cstring>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265f;

    // Hardcoded GUI defaults mirroring upstream main.ts
    constexpr float kCameraPosX = 0.0f;
    constexpr float kCameraPosY = 0.8f;
    constexpr float kCameraPosZ = -1.4f;
    constexpr float kLightPosX = 1.7f;
    constexpr float kLightPosY = 0.7f;
    constexpr float kLightPosZ = -1.9f;
    constexpr float kLightIntensity = 5.0f;
    constexpr float kDepthScale = 0.05f;
    constexpr float kDepthLayers = 16.0f;
    constexpr uint32_t kBumpMode = 3; // Normal Map
    // Texture atlas index: 0=Spiral, 1=Toybox, 2=BrickWall
    constexpr uint32_t kTextureAtlas = 0; // Spiral

    // Box-with-tangents vertex: position(3) + normal(3) + uv(2) + tangent(3) + bitangent(3) = 14 floats
    constexpr uint32_t kFloatsPerVertex = 14;
    constexpr uint32_t kBoxVertexStride = kFloatsPerVertex * sizeof(float);
    constexpr uint32_t kBoxFaces = 6;
    constexpr uint32_t kVertsPerFace = 4;
    constexpr uint32_t kBoxVertexCount = kBoxFaces * kVertsPerFace;
    constexpr uint32_t kIndicesPerFace = 6;
    constexpr uint32_t kBoxIndexCount = kBoxFaces * kIndicesPerFace;

    struct BoxMesh
    {
        std::vector<float> vertices;
        std::vector<uint16_t> indices;
    };

    BoxMesh CreateBoxMeshWithTangents(float width, float height, float depth)
    {
        // Indexing of halfVecs: 0=+x, 1=-x, 2=+y, 3=-y, 4=+z, 5=-z
        struct Face
        {
            int tangent;
            int bitangent;
            int normal;
        };
        const Face faces[6] = {
            {5, 2, 0}, // +x : tangent=-z, bitangent=+y, normal=+x
            {4, 2, 1}, // -x : tangent=+z, bitangent=+y, normal=-x
            {0, 5, 2}, // +y : tangent=+x, bitangent=-z, normal=+y
            {0, 4, 3}, // -y : tangent=+x, bitangent=+z, normal=-y
            {0, 2, 4}, // +z : tangent=+x, bitangent=+y, normal=+z
            {1, 2, 5}, // -z : tangent=-x, bitangent=+y, normal=-z
        };

        const float halfVecs[6][3] = {
            {+width * 0.5f, 0.0f, 0.0f},  // +x
            {-width * 0.5f, 0.0f, 0.0f},  // -x
            {0.0f, +height * 0.5f, 0.0f}, // +y
            {0.0f, -height * 0.5f, 0.0f}, // -y
            {0.0f, 0.0f, +depth * 0.5f},  // +z
            {0.0f, 0.0f, -depth * 0.5f},  // -z
        };

        BoxMesh mesh;
        mesh.vertices.resize(kBoxVertexCount * kFloatsPerVertex, 0.0f);
        mesh.indices.resize(kBoxIndexCount, 0);

        size_t vertexOffset = 0;
        size_t indexOffset = 0;
        for (uint32_t faceIndex = 0; faceIndex < kBoxFaces; ++faceIndex)
        {
            const Face &face = faces[faceIndex];
            const float *tangent = halfVecs[face.tangent];
            const float *bitangent = halfVecs[face.bitangent];
            const float *normal = halfVecs[face.normal];

            for (int u = 0; u < 2; ++u)
            {
                for (int v = 0; v < 2; ++v)
                {
                    float sU = (u == 0) ? -1.0f : 1.0f;
                    float sV = (v == 0) ? -1.0f : 1.0f;
                    // position
                    for (int i = 0; i < 3; ++i)
                        mesh.vertices[vertexOffset++] = normal[i] + sU * tangent[i] + sV * bitangent[i];
                    // normal
                    for (int i = 0; i < 3; ++i)
                        mesh.vertices[vertexOffset++] = normal[i];
                    // uv
                    mesh.vertices[vertexOffset++] = static_cast<float>(u);
                    mesh.vertices[vertexOffset++] = static_cast<float>(v);
                    // tangent
                    for (int i = 0; i < 3; ++i)
                        mesh.vertices[vertexOffset++] = tangent[i];
                    // bitangent
                    for (int i = 0; i < 3; ++i)
                        mesh.vertices[vertexOffset++] = bitangent[i];
                }
            }

            const uint16_t base = static_cast<uint16_t>(faceIndex * kVertsPerFace);
            mesh.indices[indexOffset++] = base + 0;
            mesh.indices[indexOffset++] = base + 2;
            mesh.indices[indexOffset++] = base + 1;
            mesh.indices[indexOffset++] = base + 2;
            mesh.indices[indexOffset++] = base + 3;
            mesh.indices[indexOffset++] = base + 1;
        }

        return mesh;
    }

    struct LoadedImage
    {
        uint8_t *pixels = nullptr;
        int width = 0;
        int height = 0;
    };

    bool LoadImageRGBA8(const std::string &path, LoadedImage &image)
    {
        int channels = 0;
        image.pixels = stbi_load(path.c_str(), &image.width, &image.height, &channels, 4);
        if (!image.pixels)
        {
            fprintf(stderr, "[Image] Failed to load %s: %s\n", path.c_str(), stbi_failure_reason());
            return false;
        }
        return true;
    }

    void FreeImage(LoadedImage &image)
    {
        if (image.pixels)
        {
            stbi_image_free(image.pixels);
            image.pixels = nullptr;
        }
    }

    WGPUTexture CreateTextureFromImage(pwgpu::test::SampleContext &ctx,
                                       const LoadedImage &image,
                                       const char *label)
    {
        WGPUTextureDescriptor textureDesc{};
        textureDesc.label = {label, WGPU_STRLEN};
        textureDesc.dimension = WGPUTextureDimension_2D;
        textureDesc.size = {static_cast<uint32_t>(image.width),
                            static_cast<uint32_t>(image.height),
                            1};
        textureDesc.mipLevelCount = 1;
        textureDesc.sampleCount = 1;
        textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
        textureDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = wgpuDeviceCreateTexture(ctx.device, &textureDesc);
        if (!texture)
            return nullptr;

        pwgpu::test::UploadRGBA8Texture(ctx.queue,
                                        texture,
                                        image.pixels,
                                        static_cast<uint32_t>(image.width),
                                        static_cast<uint32_t>(image.height));
        return texture;
    }

    // CPU uniform layout for the MapInfo cbuffer. Matches HLSL std140-ish packing:
    //   float3 lightPosVS   (12 bytes)
    //   uint   mode         (4  bytes)  -> fills the vec4 slot
    //   float  lightIntensity
    //   float  depthScale
    //   float  depthLayers
    //   float  pad0
    struct MapInfoCB
    {
        float lightPosVS[3];
        uint32_t mode;
        float lightIntensity;
        float depthScale;
        float depthLayers;
        float pad0;
    };
    static_assert(sizeof(MapInfoCB) == 32, "MapInfoCB must be 32 bytes");

    class NormalMapSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_vertexShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "normalMap.vert.hlsl").c_str(), "nm_vs");
            m_pixelShader = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "normalMap.pixel.hlsl").c_str(), "nm_ps");
            if (!m_vertexShader || !m_pixelShader)
                return false;

            if (!CreateTextureAtlases(ctx))
                return false;

            if (!CreateGeometry(ctx))
                return false;

            if (!CreateUniformBuffers(ctx))
                return false;

            if (!CreatePipelineAndBindGroups(ctx))
                return false;

            Resize(ctx, ctx.width, ctx.height);
            return m_depthTexture != nullptr && m_depthView != nullptr;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseDepthTarget();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor depthDesc{};
            depthDesc.label = {"nm_depth", WGPU_STRLEN};
            depthDesc.dimension = WGPUTextureDimension_2D;
            depthDesc.size = {width, height, 1};
            depthDesc.mipLevelCount = 1;
            depthDesc.sampleCount = 1;
            depthDesc.format = WGPUTextureFormat_Depth24Plus;
            depthDesc.usage = WGPUTextureUsage_RenderAttachment;
            m_depthTexture = wgpuDeviceCreateTexture(ctx.device, &depthDesc);
            if (!m_depthTexture)
                return;

            WGPUTextureViewDescriptor depthViewDesc{};
            depthViewDesc.format = WGPUTextureFormat_Depth24Plus;
            depthViewDesc.dimension = WGPUTextureViewDimension_2D;
            depthViewDesc.mipLevelCount = 1;
            depthViewDesc.arrayLayerCount = 1;
            depthViewDesc.aspect = WGPUTextureAspect_DepthOnly;
            m_depthView = wgpuTextureCreateView(m_depthTexture, &depthViewDesc);
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            float aspect = ctx.height > 0 ? static_cast<float>(ctx.width) /
                                                static_cast<float>(ctx.height)
                                          : 1.0f;
            mat4 projection = glm::perspectiveRH_ZO((2.0f * kPi) / 5.0f, aspect, 0.1f, 10.0f);
            projection[1][1] *= -1.0f;

            mat4 view = glm::lookAtRH(vec3(kCameraPosX, kCameraPosY, kCameraPosZ),
                                      vec3(0.0f, 0.0f, 0.0f),
                                      vec3(0.0f, 1.0f, 0.0f));

            float timeSeconds = static_cast<float>(ctx.timing.elapsedSeconds);
            mat4 model = glm::rotate(mat4(1.0f), timeSeconds * -0.5f, vec3(0.0f, 1.0f, 0.0f));

            mat4 worldView = view * model;
            mat4 worldViewProj = projection * worldView;

            // Upload SpaceTransforms { worldViewProjMatrix, worldViewMatrix } — 128 bytes total
            float matrices[32];
            std::memcpy(matrices, glm::value_ptr(worldViewProj), 64);
            std::memcpy(matrices + 16, glm::value_ptr(worldView), 64);
            wgpuQueueWriteBuffer(ctx.queue, m_spaceTransformsBuffer, 0, matrices, sizeof(matrices));

            // Transform light position into view space on the CPU
            vec4 lightWS(kLightPosX, kLightPosY, kLightPosZ, 1.0f);
            vec4 lightVS4 = view * lightWS;

            MapInfoCB mi{};
            mi.lightPosVS[0] = lightVS4.x;
            mi.lightPosVS[1] = lightVS4.y;
            mi.lightPosVS[2] = lightVS4.z;
            mi.mode = kBumpMode;
            mi.lightIntensity = kLightIntensity;
            mi.depthScale = kDepthScale;
            mi.depthLayers = kDepthLayers;
            mi.pad0 = 0.0f;
            wgpuQueueWriteBuffer(ctx.queue, m_mapInfoBuffer, 0, &mi, sizeof(mi));
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_depthView)
                return true;

            WGPURenderPassEncoder renderPass = BeginRenderPass(frame, "nm_pass", {0.0, 0.0, 0.0, 1.0}, m_depthView);
            wgpuRenderPassEncoderSetPipeline(renderPass, m_pipeline);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_frameBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 1, m_surfaceBindGroups[kTextureAtlas], 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, m_vertexBuffer, 0, m_vertexBufferSize);
            wgpuRenderPassEncoderSetIndexBuffer(renderPass,
                                                m_indexBuffer,
                                                WGPUIndexFormat_Uint16,
                                                0,
                                                m_indexBufferSize);
            wgpuRenderPassEncoderDrawIndexed(renderPass, kBoxIndexCount, 1, 0, 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseDepthTarget();

            if (m_pipeline)
            {
                wgpuRenderPipelineRelease(m_pipeline);
                m_pipeline = nullptr;
            }
            if (m_pipelineLayout)
            {
                wgpuPipelineLayoutRelease(m_pipelineLayout);
                m_pipelineLayout = nullptr;
            }

            for (WGPUBindGroup &bg : m_surfaceBindGroups)
            {
                if (bg)
                {
                    wgpuBindGroupRelease(bg);
                    bg = nullptr;
                }
            }
            if (m_frameBindGroup)
            {
                wgpuBindGroupRelease(m_frameBindGroup);
                m_frameBindGroup = nullptr;
            }

            if (m_surfaceBindGroupLayout)
            {
                wgpuBindGroupLayoutRelease(m_surfaceBindGroupLayout);
                m_surfaceBindGroupLayout = nullptr;
            }
            if (m_frameBindGroupLayout)
            {
                wgpuBindGroupLayoutRelease(m_frameBindGroupLayout);
                m_frameBindGroupLayout = nullptr;
            }

            if (m_sampler)
            {
                wgpuSamplerRelease(m_sampler);
                m_sampler = nullptr;
            }

            for (WGPUTextureView &v : m_textureViews)
            {
                if (v)
                {
                    wgpuTextureViewRelease(v);
                    v = nullptr;
                }
            }
            for (WGPUTexture &t : m_textures)
            {
                if (t)
                {
                    wgpuTextureRelease(t);
                    t = nullptr;
                }
            }

            if (m_spaceTransformsBuffer)
            {
                wgpuBufferRelease(m_spaceTransformsBuffer);
                m_spaceTransformsBuffer = nullptr;
            }
            if (m_mapInfoBuffer)
            {
                wgpuBufferRelease(m_mapInfoBuffer);
                m_mapInfoBuffer = nullptr;
            }
            if (m_vertexBuffer)
            {
                wgpuBufferRelease(m_vertexBuffer);
                m_vertexBuffer = nullptr;
            }
            if (m_indexBuffer)
            {
                wgpuBufferRelease(m_indexBuffer);
                m_indexBuffer = nullptr;
            }

            if (m_pixelShader)
            {
                wgpuShaderModuleRelease(m_pixelShader);
                m_pixelShader = nullptr;
            }
            if (m_vertexShader)
            {
                wgpuShaderModuleRelease(m_vertexShader);
                m_vertexShader = nullptr;
            }
        }

    private:
        enum TextureSlot : uint32_t
        {
            TS_WoodAlbedo = 0,
            TS_SpiralNormal,
            TS_SpiralHeight,
            TS_ToyboxNormal,
            TS_ToyboxHeight,
            TS_BrickwallAlbedo,
            TS_BrickwallNormal,
            TS_BrickwallHeight,
            TS_Count
        };

        bool CreateTextureAtlases(pwgpu::test::SampleContext &ctx)
        {
            struct Entry
            {
                const char *file;
                const char *label;
            };
            const Entry entries[TS_Count] = {
                {"wood_albedo.png", "wood_albedo"},
                {"spiral_normal.png", "spiral_normal"},
                {"spiral_height.png", "spiral_height"},
                {"toybox_normal.png", "toybox_normal"},
                {"toybox_height.png", "toybox_height"},
                {"brickwall_albedo.png", "brickwall_albedo"},
                {"brickwall_normal.png", "brickwall_normal"},
                {"brickwall_height.png", "brickwall_height"},
            };

            for (uint32_t i = 0; i < TS_Count; ++i)
            {
                LoadedImage image{};
                std::string path = pwgpu::test::GetAssetPath(ctx.exeDir, entries[i].file);
                if (!LoadImageRGBA8(path, image))
                {
                    fprintf(stderr, "        Place %s next to the executable.\n", entries[i].file);
                    return false;
                }

                m_textures[i] = CreateTextureFromImage(ctx, image, entries[i].label);
                FreeImage(image);
                if (!m_textures[i])
                    return false;

                m_textureViews[i] = pwgpu::test::CreateTextureView(m_textures[i], WGPUTextureFormat_RGBA8Unorm);
                if (!m_textureViews[i])
                    return false;
            }

            WGPUSamplerDescriptor samplerDesc{};
            samplerDesc.label = {"nm_sampler", WGPU_STRLEN};
            samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
            samplerDesc.magFilter = WGPUFilterMode_Linear;
            samplerDesc.minFilter = WGPUFilterMode_Linear;
            samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
            samplerDesc.lodMinClamp = 0.0f;
            samplerDesc.lodMaxClamp = 32.0f;
            samplerDesc.compare = WGPUCompareFunction_Undefined;
            samplerDesc.maxAnisotropy = 1;
            m_sampler = wgpuDeviceCreateSampler(ctx.device, &samplerDesc);
            return m_sampler != nullptr;
        }

        bool CreateGeometry(pwgpu::test::SampleContext &ctx)
        {
            BoxMesh mesh = CreateBoxMeshWithTangents(1.0f, 1.0f, 1.0f);
            m_vertexBufferSize = mesh.vertices.size() * sizeof(float);
            m_indexBufferSize = mesh.indices.size() * sizeof(uint16_t);

            m_vertexBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       m_vertexBufferSize,
                                                       WGPUBufferUsage_Vertex |
                                                           WGPUBufferUsage_CopyDst,
                                                       "nm_vb");
            m_indexBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                      m_indexBufferSize,
                                                      WGPUBufferUsage_Index |
                                                          WGPUBufferUsage_CopyDst,
                                                      "nm_ib");
            if (!m_vertexBuffer || !m_indexBuffer)
                return false;

            wgpuQueueWriteBuffer(ctx.queue, m_vertexBuffer, 0, mesh.vertices.data(), m_vertexBufferSize);
            wgpuQueueWriteBuffer(ctx.queue, m_indexBuffer, 0, mesh.indices.data(), m_indexBufferSize);
            return true;
        }

        bool CreateUniformBuffers(pwgpu::test::SampleContext &ctx)
        {
            m_spaceTransformsBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                                kSpaceTransformsSize,
                                                                WGPUBufferUsage_Uniform |
                                                                    WGPUBufferUsage_CopyDst,
                                                                "nm_space");
            m_mapInfoBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                        kMapInfoSize,
                                                        WGPUBufferUsage_Uniform |
                                                            WGPUBufferUsage_CopyDst,
                                                        "nm_map_info");
            return m_spaceTransformsBuffer != nullptr && m_mapInfoBuffer != nullptr;
        }

        bool CreatePipelineAndBindGroups(pwgpu::test::SampleContext &ctx)
        {
            // Frame bind group layout (set=0): 2 uniform buffers, visible to VS+FS
            WGPUBindGroupLayoutEntry frameEntries[2] = {};
            frameEntries[0].binding = 0;
            frameEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            frameEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            frameEntries[0].buffer.minBindingSize = kSpaceTransformsSize;
            frameEntries[1].binding = 1;
            frameEntries[1].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            frameEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
            frameEntries[1].buffer.minBindingSize = kMapInfoSize;

            WGPUBindGroupLayoutDescriptor frameLayoutDesc{};
            frameLayoutDesc.label = {"nm_frame_bgl", WGPU_STRLEN};
            frameLayoutDesc.entryCount = 2;
            frameLayoutDesc.entries = frameEntries;
            m_frameBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &frameLayoutDesc);
            if (!m_frameBindGroupLayout)
                return false;

            // Surface bind group layout (set=1): sampler + 3 textures, visible to FS
            WGPUBindGroupLayoutEntry surfaceEntries[4] = {};
            surfaceEntries[0].binding = 0;
            surfaceEntries[0].visibility = WGPUShaderStage_Fragment;
            surfaceEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;
            for (uint32_t i = 0; i < 3; ++i)
            {
                surfaceEntries[1 + i].binding = 1 + i;
                surfaceEntries[1 + i].visibility = WGPUShaderStage_Fragment;
                surfaceEntries[1 + i].texture.sampleType = WGPUTextureSampleType_Float;
                surfaceEntries[1 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
                surfaceEntries[1 + i].texture.multisampled = WGPUOptionalBool_False;
            }

            WGPUBindGroupLayoutDescriptor surfaceLayoutDesc{};
            surfaceLayoutDesc.label = {"nm_surface_bgl", WGPU_STRLEN};
            surfaceLayoutDesc.entryCount = 4;
            surfaceLayoutDesc.entries = surfaceEntries;
            m_surfaceBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &surfaceLayoutDesc);
            if (!m_surfaceBindGroupLayout)
                return false;

            WGPUBindGroupLayout bgls[2] = {m_frameBindGroupLayout, m_surfaceBindGroupLayout};
            WGPUPipelineLayoutDescriptor pipelineLayoutDesc{};
            pipelineLayoutDesc.label = {"nm_pl", WGPU_STRLEN};
            pipelineLayoutDesc.bindGroupLayoutCount = 2;
            pipelineLayoutDesc.bindGroupLayouts = bgls;
            m_pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &pipelineLayoutDesc);
            if (!m_pipelineLayout)
                return false;

            // Frame bind group
            WGPUBindGroupEntry frameBGEntries[2] = {};
            frameBGEntries[0].binding = 0;
            frameBGEntries[0].buffer = m_spaceTransformsBuffer;
            frameBGEntries[0].size = kSpaceTransformsSize;
            frameBGEntries[1].binding = 1;
            frameBGEntries[1].buffer = m_mapInfoBuffer;
            frameBGEntries[1].size = kMapInfoSize;

            WGPUBindGroupDescriptor frameBGDesc{};
            frameBGDesc.label = {"nm_frame_bg", WGPU_STRLEN};
            frameBGDesc.layout = m_frameBindGroupLayout;
            frameBGDesc.entryCount = 2;
            frameBGDesc.entries = frameBGEntries;
            m_frameBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &frameBGDesc);
            if (!m_frameBindGroup)
                return false;

            // Three surface bind groups — one per atlas: Spiral / Toybox / BrickWall.
            // Spiral and Toybox reuse wood_albedo as their albedo fallback (mirroring upstream).
            const uint32_t atlasAlbedo[3] = {TS_WoodAlbedo, TS_WoodAlbedo, TS_BrickwallAlbedo};
            const uint32_t atlasNormal[3] = {TS_SpiralNormal, TS_ToyboxNormal, TS_BrickwallNormal};
            const uint32_t atlasHeight[3] = {TS_SpiralHeight, TS_ToyboxHeight, TS_BrickwallHeight};
            const char *atlasLabels[3] = {"nm_surface_spiral", "nm_surface_toybox", "nm_surface_brickwall"};

            for (uint32_t i = 0; i < 3; ++i)
            {
                WGPUBindGroupEntry surfaceBGEntries[4] = {};
                surfaceBGEntries[0].binding = 0;
                surfaceBGEntries[0].sampler = m_sampler;
                surfaceBGEntries[1].binding = 1;
                surfaceBGEntries[1].textureView = m_textureViews[atlasAlbedo[i]];
                surfaceBGEntries[2].binding = 2;
                surfaceBGEntries[2].textureView = m_textureViews[atlasNormal[i]];
                surfaceBGEntries[3].binding = 3;
                surfaceBGEntries[3].textureView = m_textureViews[atlasHeight[i]];

                WGPUBindGroupDescriptor surfaceBGDesc{};
                surfaceBGDesc.label = {atlasLabels[i], WGPU_STRLEN};
                surfaceBGDesc.layout = m_surfaceBindGroupLayout;
                surfaceBGDesc.entryCount = 4;
                surfaceBGDesc.entries = surfaceBGEntries;
                m_surfaceBindGroups[i] = wgpuDeviceCreateBindGroup(ctx.device, &surfaceBGDesc);
                if (!m_surfaceBindGroups[i])
                    return false;
            }

            // Vertex layout: position(3), normal(3), uv(2), tangent(3), bitangent(3)
            WGPUVertexAttribute attributes[5] = {};
            uint64_t offset = 0;
            attributes[0].format = WGPUVertexFormat_Float32x3;
            attributes[0].offset = offset;
            attributes[0].shaderLocation = 0;
            offset += 12;
            attributes[1].format = WGPUVertexFormat_Float32x3;
            attributes[1].offset = offset;
            attributes[1].shaderLocation = 1;
            offset += 12;
            attributes[2].format = WGPUVertexFormat_Float32x2;
            attributes[2].offset = offset;
            attributes[2].shaderLocation = 2;
            offset += 8;
            attributes[3].format = WGPUVertexFormat_Float32x3;
            attributes[3].offset = offset;
            attributes[3].shaderLocation = 3;
            offset += 12;
            attributes[4].format = WGPUVertexFormat_Float32x3;
            attributes[4].offset = offset;
            attributes[4].shaderLocation = 4;

            WGPUVertexBufferLayout vertexBufferLayout{};
            vertexBufferLayout.arrayStride = kBoxVertexStride;
            vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
            vertexBufferLayout.attributeCount = 5;
            vertexBufferLayout.attributes = attributes;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = ctx.surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = m_pixelShader;
            fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            WGPUDepthStencilState depthState{};
            depthState.format = WGPUTextureFormat_Depth24Plus;
            depthState.depthWriteEnabled = WGPUOptionalBool_True;
            depthState.depthCompare = WGPUCompareFunction_Less;
            depthState.stencilFront.compare = WGPUCompareFunction_Always;
            depthState.stencilBack.compare = WGPUCompareFunction_Always;

            WGPURenderPipelineDescriptor pipelineDesc{};
            pipelineDesc.label = {"nm_pipe", WGPU_STRLEN};
            pipelineDesc.layout = m_pipelineLayout;
            pipelineDesc.vertex.module = m_vertexShader;
            pipelineDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipelineDesc.vertex.bufferCount = 1;
            pipelineDesc.vertex.buffers = &vertexBufferLayout;
            pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipelineDesc.primitive.cullMode = WGPUCullMode_Back;
            pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
            pipelineDesc.depthStencil = &depthState;
            pipelineDesc.multisample.count = 1;
            pipelineDesc.multisample.mask = 0xFFFFFFFF;
            pipelineDesc.fragment = &fragmentState;
            m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipelineDesc);
            return m_pipeline != nullptr;
        }

        void ReleaseDepthTarget()
        {
            if (m_depthView)
            {
                wgpuTextureViewRelease(m_depthView);
                m_depthView = nullptr;
            }
            if (m_depthTexture)
            {
                wgpuTextureRelease(m_depthTexture);
                m_depthTexture = nullptr;
            }
        }

        static constexpr uint64_t kSpaceTransformsSize = 128; // 2 mat4x4
        static constexpr uint64_t kMapInfoSize = 32;

        WGPUShaderModule m_vertexShader = nullptr;
        WGPUShaderModule m_pixelShader = nullptr;

        std::array<WGPUTexture, TS_Count> m_textures{};
        std::array<WGPUTextureView, TS_Count> m_textureViews{};
        WGPUSampler m_sampler = nullptr;

        WGPUBuffer m_vertexBuffer = nullptr;
        WGPUBuffer m_indexBuffer = nullptr;
        uint64_t m_vertexBufferSize = 0;
        uint64_t m_indexBufferSize = 0;

        WGPUBuffer m_spaceTransformsBuffer = nullptr;
        WGPUBuffer m_mapInfoBuffer = nullptr;

        WGPUBindGroupLayout m_frameBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_surfaceBindGroupLayout = nullptr;
        WGPUPipelineLayout m_pipelineLayout = nullptr;
        WGPUBindGroup m_frameBindGroup = nullptr;
        std::array<WGPUBindGroup, 3> m_surfaceBindGroups{};
        WGPURenderPipeline m_pipeline = nullptr;

        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeNormalMapDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "NormalMapTest";
        desc.windowTitle = "PhasmaWebGPU Normal Map";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    NormalMapSample sample;
    pwgpu::test::SampleApp app(sample, MakeNormalMapDesc());
    return app.Run(argc, argv);
}
