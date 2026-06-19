#include "../Common/CubeGeometry.h"
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
    constexpr uint32_t kImageSize = 256;
    constexpr uint32_t kMipLevelCount = 9;
    constexpr uint32_t kBorderSize = 10;
    constexpr float kPi = 3.14159265358979323846f;

    // Whether we have a real cube-array texture in slot 4.
    // Upstream allows a cube fallback; we take that path for portability.
    constexpr bool kUseCubeArray = false;
    constexpr uint32_t kCubeArrayLayers = 24; // 4 cube layers x 6 faces

    enum class ViewDim
    {
        Tex2D,
        Tex2DArray,
        TexCube,
        TexCubeArray,
    };

    struct Rgba
    {
        uint8_t r, g, b, a;
    };

    Rgba HslToRgba(float h, float s, float l, uint8_t alpha = 255)
    {
        h = h - floorf(h);
        float c = (1.f - fabsf(2.f * l - 1.f)) * s;
        float x = c * (1.f - fabsf(fmodf(h * 6.f, 2.f) - 1.f));
        float m = l - c * 0.5f;
        float r = 0, g = 0, b = 0;
        float h6 = h * 6.f;
        if (h6 < 1.f)
        {
            r = c;
            g = x;
        }
        else if (h6 < 2.f)
        {
            r = x;
            g = c;
        }
        else if (h6 < 3.f)
        {
            g = c;
            b = x;
        }
        else if (h6 < 4.f)
        {
            g = x;
            b = c;
        }
        else if (h6 < 5.f)
        {
            r = x;
            b = c;
        }
        else
        {
            r = c;
            b = x;
        }
        return Rgba{static_cast<uint8_t>((r + m) * 255.f),
                    static_cast<uint8_t>((g + m) * 255.f),
                    static_cast<uint8_t>((b + m) * 255.f),
                    alpha};
    }

    void PutPixel(uint8_t *buf, uint32_t w, uint32_t h, int x, int y, Rgba c)
    {
        if (x < 0 || y < 0 || x >= static_cast<int>(w) || y >= static_cast<int>(h))
            return;
        size_t idx = (static_cast<size_t>(y) * w + x) * 4;
        buf[idx + 0] = c.r;
        buf[idx + 1] = c.g;
        buf[idx + 2] = c.b;
        buf[idx + 3] = c.a;
    }

    void FillRect(uint8_t *buf, uint32_t w, uint32_t h, int x0, int y0, int x1, int y1, Rgba c)
    {
        if (x0 < 0)
            x0 = 0;
        if (y0 < 0)
            y0 = 0;
        if (x1 > static_cast<int>(w))
            x1 = w;
        if (y1 > static_cast<int>(h))
            y1 = h;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                PutPixel(buf, w, h, x, y, c);
    }

    void FillCircle(uint8_t *buf, uint32_t w, uint32_t h, int cx, int cy, int radius, Rgba c)
    {
        int r2 = radius * radius;
        for (int y = -radius; y <= radius; ++y)
            for (int x = -radius; x <= radius; ++x)
                if (x * x + y * y <= r2)
                    PutPixel(buf, w, h, cx + x, cy + y, c);
    }

    void FillTriangle(uint8_t *buf,
                      uint32_t w,
                      uint32_t h,
                      int x0,
                      int y0,
                      int x1,
                      int y1,
                      int x2,
                      int y2,
                      Rgba c)
    {
        int minx = x0 < x1 ? x0 : x1;
        if (x2 < minx)
            minx = x2;
        int maxx = x0 > x1 ? x0 : x1;
        if (x2 > maxx)
            maxx = x2;
        int miny = y0 < y1 ? y0 : y1;
        if (y2 < miny)
            miny = y2;
        int maxy = y0 > y1 ? y0 : y1;
        if (y2 > maxy)
            maxy = y2;
        auto edge = [](int ax, int ay, int bx, int by, int px, int py)
        { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); };
        for (int y = miny; y <= maxy; ++y)
            for (int x = minx; x <= maxx; ++x)
            {
                int w0 = edge(x1, y1, x2, y2, x, y);
                int w1 = edge(x2, y2, x0, y0, x, y);
                int w2 = edge(x0, y0, x1, y1, x, y);
                if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
                    PutPixel(buf, w, h, x, y, c);
            }
    }

    // Mimics upstream makeCanvasImage: border + background + caller-drawn pattern.
    void MakeCanvasImage(std::vector<uint8_t> &out,
                         uint32_t width,
                         uint32_t height,
                         Rgba border,
                         Rgba bg,
                         const std::function<void(uint8_t *, uint32_t, uint32_t)> &drawPattern)
    {
        out.assign(static_cast<size_t>(width) * height * 4, 0);
        uint8_t *buf = out.data();
        FillRect(buf, width, height, 0, 0, width, height, border);
        FillRect(buf,
                 width,
                 height,
                 kBorderSize,
                 kBorderSize,
                 static_cast<int>(width - kBorderSize),
                 static_cast<int>(height - kBorderSize),
                 bg);
        if (drawPattern)
            drawPattern(buf, width, height);
    }

    // Layer 2d: yellow border + red bg + face-like central circle + eyes.
    void DrawFace2D(uint8_t *buf, uint32_t w, uint32_t h)
    {
        int cx = static_cast<int>(w) / 2;
        int cy = static_cast<int>(h) / 2;
        int r = static_cast<int>(w) * 3 / 8;
        Rgba yellow{255, 220, 0, 255};
        Rgba black{0, 0, 0, 255};
        FillCircle(buf, w, h, cx, cy, r, yellow);
        int eyeR = r / 6;
        FillCircle(buf, w, h, cx - r / 3, cy - r / 4, eyeR, black);
        FillCircle(buf, w, h, cx + r / 3, cy - r / 4, eyeR, black);
        // Curved mouth as a horizontal rect.
        FillRect(buf, w, h, cx - r / 2, cy + r / 4, cx + r / 2, cy + r / 4 + 10, black);
    }

    // 2d-array layer pattern: (layer+2) x (layer+2) grid of white dots on bg.
    void DrawArrayPattern(uint8_t *buf, uint32_t w, uint32_t h, uint32_t layer)
    {
        uint32_t n = layer + 2;
        int inner = static_cast<int>(w) - 2 * static_cast<int>(kBorderSize);
        int start = static_cast<int>(kBorderSize);
        Rgba dot{255, 255, 255, 255};
        for (uint32_t gy = 0; gy < n; ++gy)
            for (uint32_t gx = 0; gx < n; ++gx)
            {
                int cx = start + (inner * (2 * static_cast<int>(gx) + 1)) / (2 * static_cast<int>(n));
                int cy = start + (inner * (2 * static_cast<int>(gy) + 1)) / (2 * static_cast<int>(n));
                int radius = inner / (4 * static_cast<int>(n));
                if (radius < 2)
                    radius = 2;
                FillCircle(buf, w, h, cx, cy, radius, dot);
            }
    }

    // Cube face pattern: red triangle pointing in the face axis direction + a small indicator.
    // face indices: 0=+x, 1=-x, 2=+y, 3=-y, 4=+z, 5=-z
    void DrawCubeFace(uint8_t *buf, uint32_t w, uint32_t h, uint32_t face)
    {
        int cx = static_cast<int>(w) / 2;
        int cy = static_cast<int>(h) / 2;
        int s = static_cast<int>(w) / 3;
        Rgba red{255, 0, 0, 255};
        Rgba white{255, 255, 255, 255};
        switch (face)
        {
        case 0: // +x : arrow right
            FillTriangle(buf, w, h, cx - s, cy - s, cx + s, cy, cx - s, cy + s, red);
            FillRect(buf, w, h, cx + s - 20, cy - 20, cx + s + 20, cy + 20, white);
            break;
        case 1: // -x : arrow left
            FillTriangle(buf, w, h, cx + s, cy - s, cx - s, cy, cx + s, cy + s, red);
            FillRect(buf, w, h, cx - s - 20, cy - 20, cx - s + 20, cy + 20, white);
            break;
        case 2: // +y : arrow up
            FillTriangle(buf, w, h, cx - s, cy + s, cx, cy - s, cx + s, cy + s, red);
            FillRect(buf, w, h, cx - 20, cy - s - 20, cx + 20, cy - s + 20, white);
            break;
        case 3: // -y : arrow down
            FillTriangle(buf, w, h, cx - s, cy - s, cx, cy + s, cx + s, cy - s, red);
            FillRect(buf, w, h, cx - 20, cy + s - 20, cx + 20, cy + s + 20, white);
            break;
        case 4: // +z : diamond
            FillTriangle(buf, w, h, cx, cy - s, cx + s, cy, cx - s, cy, red);
            FillTriangle(buf, w, h, cx, cy + s, cx + s, cy, cx - s, cy, red);
            FillCircle(buf, w, h, cx, cy, 20, white);
            break;
        case 5: // -z : X cross
        default:
            FillTriangle(buf, w, h, cx - s, cy - s, cx + s, cy - s + 30, cx - s, cy - s + 30, red);
            FillTriangle(buf, w, h, cx + s, cy + s, cx - s, cy + s - 30, cx + s, cy + s - 30, red);
            FillTriangle(buf, w, h, cx + s, cy - s, cx - s, cy - s + 30, cx + s, cy - s + 30, red);
            FillTriangle(buf, w, h, cx - s, cy + s, cx + s, cy + s - 30, cx - s, cy + s - 30, red);
            break;
        }
    }

    // Cube-array fallback pattern (⛔️-ish): dark bg, red border drawn by outer, red X inside.
    void DrawCubeFallbackFace(uint8_t *buf, uint32_t w, uint32_t h, uint32_t face)
    {
        Rgba red{255, 0, 0, 255};
        Rgba fg{136, 255, 255, 255};
        int cx = static_cast<int>(w) / 2;
        int cy = static_cast<int>(h) / 2;
        int r = static_cast<int>(w) / 3;
        // draw a red disc with a thick fg diagonal ("no")
        FillCircle(buf, w, h, cx, cy, r, red);
        FillCircle(buf, w, h, cx, cy, r - 20, fg);
        // diagonal bar based on face orientation
        int thick = 14;
        if (face % 2 == 0)
        {
            // top-left to bottom-right
            for (int t = -r; t <= r; ++t)
                FillRect(buf, w, h, cx + t - thick, cy + t - thick, cx + t + thick, cy + t + thick, red);
        }
        else
        {
            for (int t = -r; t <= r; ++t)
                FillRect(buf, w, h, cx + t - thick, cy - t - thick, cx + t + thick, cy - t + thick, red);
        }
    }

    struct TextureSet
    {
        WGPUTexture texture = nullptr;
        WGPUTextureView displayView = nullptr;
        WGPUBindGroup displayBindGroup = nullptr;
        WGPURenderPipeline displayPipeline = nullptr;
        ViewDim dim = ViewDim::Tex2D;
        uint32_t layers = 1;
    };

    class GenerateMipmapSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            if (!LoadShaders(ctx))
                return false;

            if (!CreateCommon(ctx))
                return false;

            if (!CreateTexture2D(ctx))
                return false;
            if (!CreateTexture2DArray(ctx))
                return false;
            if (!CreateTextureCube(ctx))
                return false;
            if (!CreateTextureCubeSlot4(ctx))
                return false;

            if (!CreateDisplayPipelines(ctx))
                return false;

            for (TextureSet &t : m_textures)
            {
                if (!GenerateMips(ctx, t))
                    return false;
            }

            for (TextureSet &t : m_textures)
            {
                if (!CreateDisplayBindGroup(ctx, t))
                    return false;
            }

            Resize(ctx, ctx.width, ctx.height);
            return m_depthView != nullptr;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseDepthTarget();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor depthDesc{};
            depthDesc.label = {"genmip_depth", WGPU_STRLEN};
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
            mat4 projection = glm::perspectiveRH_ZO(kPi * 0.5f, aspect, 0.1f, 20.f);
            projection[1][1] *= -1.f;

            float time = static_cast<float>(ctx.timing.elapsedSeconds);
            vec3 eye(0.f, 4.f + 2.5f * sinf(time), 2.f);
            mat4 view = glm::lookAtRH(eye, vec3(0.f, 0.f, 0.f), vec3(0.f, 1.f, 0.f));
            mat4 viewProj = projection * view;
            viewProj = glm::rotate(viewProj, time, vec3(1.f, 0.f, 0.f));
            viewProj = glm::rotate(viewProj, time * 0.7f, vec3(0.f, 1.f, 0.f));

            wgpuQueueWriteBuffer(ctx.queue, m_uniformBuffer, 0, glm::value_ptr(viewProj), 64);
            m_instanceIndex = static_cast<uint32_t>(time);
        }

        bool Execute(pwgpu::test::SampleContext &ctx, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_depthView)
                return true;

            WGPURenderPassEncoder pass =
                BeginRenderPass(frame, "genmip_render", {0.3, 0.3, 0.3, 1.0}, m_depthView);
            wgpuRenderPassEncoderSetVertexBuffer(pass,
                                                 0,
                                                 m_vertexBuffer,
                                                 0,
                                                 sizeof(pwgpu::test::cube::kVertices));

            const uint32_t w = ctx.width;
            const uint32_t h = ctx.height;
            const float halfW = static_cast<float>(w) * 0.5f;
            const float halfH = static_cast<float>(h) * 0.5f;

            for (size_t i = 0; i < m_textures.size(); ++i)
            {
                const TextureSet &t = m_textures[i];
                uint32_t qx = static_cast<uint32_t>(i % 2);
                uint32_t qy = static_cast<uint32_t>(i / 2);
                float vx = static_cast<float>(qx) * halfW;
                float vy = static_cast<float>(qy) * halfH;
                wgpuRenderPassEncoderSetViewport(pass, vx, vy, halfW, halfH, 0.f, 1.f);
                wgpuRenderPassEncoderSetPipeline(pass, t.displayPipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, t.displayBindGroup, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass,
                                          pwgpu::test::cube::kVertexCount,
                                          1,
                                          0,
                                          m_instanceIndex);
            }

            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseDepthTarget();

            for (WGPUBindGroup bg : m_mipGenBindGroups)
                wgpuBindGroupRelease(bg);
            m_mipGenBindGroups.clear();
            for (WGPUTextureView v : m_mipGenViews)
                wgpuTextureViewRelease(v);
            m_mipGenViews.clear();

            for (TextureSet &t : m_textures)
            {
                if (t.displayBindGroup)
                    wgpuBindGroupRelease(t.displayBindGroup);
                if (t.displayPipeline)
                    wgpuRenderPipelineRelease(t.displayPipeline);
                if (t.displayView)
                    wgpuTextureViewRelease(t.displayView);
                if (t.texture)
                    wgpuTextureRelease(t.texture);
            }
            m_textures.clear();

            for (int i = 0; i < 4; ++i)
            {
                if (m_mipPipelines[i])
                    wgpuRenderPipelineRelease(m_mipPipelines[i]);
                m_mipPipelines[i] = nullptr;
            }
            if (m_mipPipelineLayout2D)
                wgpuPipelineLayoutRelease(m_mipPipelineLayout2D);
            if (m_mipPipelineLayoutArray)
                wgpuPipelineLayoutRelease(m_mipPipelineLayoutArray);
            if (m_mipPipelineLayoutCube)
                wgpuPipelineLayoutRelease(m_mipPipelineLayoutCube);
            if (m_mipPipelineLayoutCubeArray)
                wgpuPipelineLayoutRelease(m_mipPipelineLayoutCubeArray);
            if (m_mipBgl2D)
                wgpuBindGroupLayoutRelease(m_mipBgl2D);
            if (m_mipBgl2DArray)
                wgpuBindGroupLayoutRelease(m_mipBgl2DArray);
            if (m_mipBglCube)
                wgpuBindGroupLayoutRelease(m_mipBglCube);
            if (m_mipBglCubeArray)
                wgpuBindGroupLayoutRelease(m_mipBglCubeArray);

            if (m_displayPipelineLayout[0])
                wgpuPipelineLayoutRelease(m_displayPipelineLayout[0]);
            if (m_displayPipelineLayout[1])
                wgpuPipelineLayoutRelease(m_displayPipelineLayout[1]);
            if (m_displayPipelineLayout[2])
                wgpuPipelineLayoutRelease(m_displayPipelineLayout[2]);
            if (m_displayPipelineLayout[3])
                wgpuPipelineLayoutRelease(m_displayPipelineLayout[3]);
            if (m_displayBgl2D)
                wgpuBindGroupLayoutRelease(m_displayBgl2D);
            if (m_displayBgl2DArray)
                wgpuBindGroupLayoutRelease(m_displayBgl2DArray);
            if (m_displayBglCube)
                wgpuBindGroupLayoutRelease(m_displayBglCube);
            if (m_displayBglCubeArray)
                wgpuBindGroupLayoutRelease(m_displayBglCubeArray);

            if (m_mipSampler)
                wgpuSamplerRelease(m_mipSampler);
            if (m_displaySampler)
                wgpuSamplerRelease(m_displaySampler);

            if (m_vertexBuffer)
                wgpuBufferRelease(m_vertexBuffer);
            if (m_uniformBuffer)
                wgpuBufferRelease(m_uniformBuffer);

            for (WGPUShaderModule &s : m_shaderModules)
            {
                if (s)
                    wgpuShaderModuleRelease(s);
                s = nullptr;
            }

            m_mipPipelineLayout2D = nullptr;
            m_mipPipelineLayoutArray = nullptr;
            m_mipPipelineLayoutCube = nullptr;
            m_mipPipelineLayoutCubeArray = nullptr;
            m_mipBgl2D = nullptr;
            m_mipBgl2DArray = nullptr;
            m_mipBglCube = nullptr;
            m_mipBglCubeArray = nullptr;
            for (auto &pl : m_displayPipelineLayout)
                pl = nullptr;
            m_displayBgl2D = nullptr;
            m_displayBgl2DArray = nullptr;
            m_displayBglCube = nullptr;
            m_displayBglCubeArray = nullptr;
            m_mipSampler = nullptr;
            m_displaySampler = nullptr;
            m_vertexBuffer = nullptr;
            m_uniformBuffer = nullptr;
        }

    private:
        enum ShaderIdx
        {
            SHADER_GENMIP_VS,
            SHADER_GENMIP_2D,
            SHADER_GENMIP_2DARRAY,
            SHADER_GENMIP_CUBE,
            SHADER_GENMIP_CUBEARRAY,
            SHADER_DISP_VS_2D,
            SHADER_DISP_PS_2D,
            SHADER_DISP_VS_2DARRAY,
            SHADER_DISP_PS_2DARRAY,
            SHADER_DISP_VS_CUBE,
            SHADER_DISP_PS_CUBE,
            SHADER_DISP_VS_CUBEARRAY,
            SHADER_DISP_PS_CUBEARRAY,
            SHADER_COUNT,
        };

        bool LoadShaders(pwgpu::test::SampleContext &ctx)
        {
            struct Entry
            {
                const char *file;
                const char *label;
            };
            Entry entries[SHADER_COUNT] = {
                {"generateMipmap.vert.hlsl", "genmip_vs"},
                {"generateMipmap2d.pixel.hlsl", "genmip_ps_2d"},
                {"generateMipmap2dArray.pixel.hlsl", "genmip_ps_2darr"},
                {"generateMipmapCube.pixel.hlsl", "genmip_ps_cube"},
                {"generateMipmapCubeArray.pixel.hlsl", "genmip_ps_cubearr"},
                {"texturedGeometry2d.vert.hlsl", "disp_vs_2d"},
                {"texturedGeometry2d.pixel.hlsl", "disp_ps_2d"},
                {"texturedGeometry2dArray.vert.hlsl", "disp_vs_2darr"},
                {"texturedGeometry2dArray.pixel.hlsl", "disp_ps_2darr"},
                {"texturedGeometryCube.vert.hlsl", "disp_vs_cube"},
                {"texturedGeometryCube.pixel.hlsl", "disp_ps_cube"},
                {"texturedGeometryCubeArray.vert.hlsl", "disp_vs_cubearr"},
                {"texturedGeometryCubeArray.pixel.hlsl", "disp_ps_cubearr"},
            };
            for (int i = 0; i < SHADER_COUNT; ++i)
            {
                m_shaderModules[i] = pwgpu::test::MakeRuntimeShaderModule(
                    ctx.device, (ctx.shaderDir + entries[i].file).c_str(), entries[i].label);
                if (!m_shaderModules[i])
                    return false;
            }
            return true;
        }

        bool CreateCommon(pwgpu::test::SampleContext &ctx)
        {
            m_vertexBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                sizeof(pwgpu::test::cube::kVertices),
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst),
                "genmip_vb");
            m_uniformBuffer = pwgpu::test::CreateBuffer(
                ctx.device,
                64,
                static_cast<WGPUBufferUsage>(WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst),
                "genmip_ubo");
            if (!m_vertexBuffer || !m_uniformBuffer)
                return false;
            wgpuQueueWriteBuffer(ctx.queue,
                                 m_vertexBuffer,
                                 0,
                                 pwgpu::test::cube::kVertices,
                                 sizeof(pwgpu::test::cube::kVertices));

            WGPUSamplerDescriptor samplerDesc{};
            samplerDesc.label = {"mip_sampler", WGPU_STRLEN};
            samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
            samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
            samplerDesc.magFilter = WGPUFilterMode_Linear;
            samplerDesc.minFilter = WGPUFilterMode_Linear;
            samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
            samplerDesc.lodMinClamp = 0.f;
            samplerDesc.lodMaxClamp = 0.f;
            samplerDesc.maxAnisotropy = 1;
            m_mipSampler = wgpuDeviceCreateSampler(ctx.device, &samplerDesc);
            if (!m_mipSampler)
                return false;

            WGPUSamplerDescriptor displaySamplerDesc{};
            displaySamplerDesc.label = {"display_sampler", WGPU_STRLEN};
            displaySamplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
            displaySamplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
            displaySamplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
            displaySamplerDesc.magFilter = WGPUFilterMode_Linear;
            displaySamplerDesc.minFilter = WGPUFilterMode_Linear;
            displaySamplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
            displaySamplerDesc.lodMinClamp = 0.f;
            displaySamplerDesc.lodMaxClamp = static_cast<float>(kMipLevelCount - 1);
            displaySamplerDesc.maxAnisotropy = 1;
            m_displaySampler = wgpuDeviceCreateSampler(ctx.device, &displaySamplerDesc);
            return m_displaySampler != nullptr;
        }

        WGPUTexture CreateMipmapableTexture(pwgpu::test::SampleContext &ctx,
                                            uint32_t layers,
                                            WGPUTextureViewDimension bindingViewDim,
                                            const char *label)
        {
            WGPUTextureBindingViewDimension bindingDim{};
            bindingDim.chain.sType = WGPUSType_TextureBindingViewDimension;
            bindingDim.textureBindingViewDimension = bindingViewDim;

            WGPUTextureDescriptor desc{};
            desc.label = {label, WGPU_STRLEN};
            desc.dimension = WGPUTextureDimension_2D;
            desc.size = {kImageSize, kImageSize, layers};
            desc.mipLevelCount = kMipLevelCount;
            desc.sampleCount = 1;
            desc.format = WGPUTextureFormat_RGBA8Unorm;
            desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
                         WGPUTextureUsage_RenderAttachment;
            if (bindingViewDim != WGPUTextureViewDimension_Undefined)
                desc.nextInChain = &bindingDim.chain;
            return wgpuDeviceCreateTexture(ctx.device, &desc);
        }

        bool CreateTexture2D(pwgpu::test::SampleContext &ctx)
        {
            TextureSet t{};
            t.dim = ViewDim::Tex2D;
            t.layers = 1;
            t.texture = CreateMipmapableTexture(ctx, 1, WGPUTextureViewDimension_Undefined, "mip_2d");
            if (!t.texture)
                return false;

            std::vector<uint8_t> pixels;
            Rgba border{255, 255, 0, 255}; // yellow
            Rgba bg{255, 0, 0, 255};       // red
            MakeCanvasImage(pixels, kImageSize, kImageSize, border, bg, DrawFace2D);
            pwgpu::test::UploadRGBA8Texture(ctx.queue,
                                            t.texture,
                                            pixels.data(),
                                            kImageSize,
                                            kImageSize,
                                            0,
                                            0);

            WGPUTextureViewDescriptor viewDesc{};
            viewDesc.label = {"disp_view_2d", WGPU_STRLEN};
            viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.baseMipLevel = 0;
            viewDesc.mipLevelCount = kMipLevelCount;
            viewDesc.baseArrayLayer = 0;
            viewDesc.arrayLayerCount = 1;
            viewDesc.aspect = WGPUTextureAspect_All;
            t.displayView = wgpuTextureCreateView(t.texture, &viewDesc);
            if (!t.displayView)
                return false;

            m_textures.push_back(t);
            return true;
        }

        bool CreateTexture2DArray(pwgpu::test::SampleContext &ctx)
        {
            constexpr uint32_t kLayers = 10;
            TextureSet t{};
            t.dim = ViewDim::Tex2DArray;
            t.layers = kLayers;
            t.texture = CreateMipmapableTexture(ctx, kLayers, WGPUTextureViewDimension_Undefined, "mip_2darr");
            if (!t.texture)
                return false;

            for (uint32_t layer = 0; layer < kLayers; ++layer)
            {
                float hue = static_cast<float>(layer) / static_cast<float>(kLayers);
                Rgba border = HslToRgba(hue + 0.5f, 1.f, 0.75f);
                Rgba bg = HslToRgba(hue, 0.5f, 0.5f);
                std::vector<uint8_t> pixels;
                MakeCanvasImage(pixels,
                                kImageSize,
                                kImageSize,
                                border,
                                bg,
                                [layer](uint8_t *b, uint32_t w, uint32_t h)
                                { DrawArrayPattern(b, w, h, layer); });
                pwgpu::test::UploadRGBA8Texture(ctx.queue,
                                                t.texture,
                                                pixels.data(),
                                                kImageSize,
                                                kImageSize,
                                                0,
                                                layer);
            }

            WGPUTextureViewDescriptor viewDesc{};
            viewDesc.label = {"disp_view_2darr", WGPU_STRLEN};
            viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
            viewDesc.dimension = WGPUTextureViewDimension_2DArray;
            viewDesc.baseMipLevel = 0;
            viewDesc.mipLevelCount = kMipLevelCount;
            viewDesc.baseArrayLayer = 0;
            viewDesc.arrayLayerCount = kLayers;
            viewDesc.aspect = WGPUTextureAspect_All;
            t.displayView = wgpuTextureCreateView(t.texture, &viewDesc);
            if (!t.displayView)
                return false;

            m_textures.push_back(t);
            return true;
        }

        bool CreateTextureCube(pwgpu::test::SampleContext &ctx)
        {
            constexpr uint32_t kLayers = 6;
            TextureSet t{};
            t.dim = ViewDim::TexCube;
            t.layers = kLayers;
            t.texture = CreateMipmapableTexture(ctx, kLayers, WGPUTextureViewDimension_Cube, "mip_cube");
            if (!t.texture)
                return false;

            for (uint32_t face = 0; face < kLayers; ++face)
            {
                float hue = static_cast<float>(face) / static_cast<float>(kLayers);
                Rgba border = HslToRgba(hue + 0.5f, 1.f, 0.75f);
                Rgba bg = HslToRgba(hue, 0.5f, 0.5f);
                std::vector<uint8_t> pixels;
                MakeCanvasImage(pixels,
                                kImageSize,
                                kImageSize,
                                border,
                                bg,
                                [face](uint8_t *b, uint32_t w, uint32_t h)
                                { DrawCubeFace(b, w, h, face); });
                pwgpu::test::UploadRGBA8Texture(ctx.queue,
                                                t.texture,
                                                pixels.data(),
                                                kImageSize,
                                                kImageSize,
                                                0,
                                                face);
            }

            WGPUTextureViewDescriptor viewDesc{};
            viewDesc.label = {"disp_view_cube", WGPU_STRLEN};
            viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
            viewDesc.dimension = WGPUTextureViewDimension_Cube;
            viewDesc.baseMipLevel = 0;
            viewDesc.mipLevelCount = kMipLevelCount;
            viewDesc.baseArrayLayer = 0;
            viewDesc.arrayLayerCount = kLayers;
            viewDesc.aspect = WGPUTextureAspect_All;
            t.displayView = wgpuTextureCreateView(t.texture, &viewDesc);
            if (!t.displayView)
                return false;

            m_textures.push_back(t);
            return true;
        }

        bool CreateTextureCubeSlot4(pwgpu::test::SampleContext &ctx)
        {
            if constexpr (kUseCubeArray)
            {
                constexpr uint32_t kLayers = kCubeArrayLayers;
                TextureSet t{};
                t.dim = ViewDim::TexCubeArray;
                t.layers = kLayers;
                t.texture = CreateMipmapableTexture(
                    ctx, kLayers, WGPUTextureViewDimension_CubeArray, "mip_cubearr");
                if (!t.texture)
                    return false;

                for (uint32_t layer = 0; layer < kLayers; ++layer)
                {
                    uint32_t cubeLayer = layer / 6;
                    uint32_t face = layer % 6;
                    float hue = static_cast<float>(cubeLayer) / static_cast<float>(kLayers / 6);
                    Rgba border = HslToRgba(hue + 0.5f, 1.f, 0.75f);
                    Rgba bg = HslToRgba(hue, 0.5f, 0.5f);
                    std::vector<uint8_t> pixels;
                    MakeCanvasImage(pixels,
                                    kImageSize,
                                    kImageSize,
                                    border,
                                    bg,
                                    [face](uint8_t *b, uint32_t w, uint32_t h)
                                    { DrawCubeFace(b, w, h, face); });
                    pwgpu::test::UploadRGBA8Texture(ctx.queue,
                                                    t.texture,
                                                    pixels.data(),
                                                    kImageSize,
                                                    kImageSize,
                                                    0,
                                                    layer);
                }

                WGPUTextureViewDescriptor viewDesc{};
                viewDesc.label = {"disp_view_cubearr", WGPU_STRLEN};
                viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
                viewDesc.dimension = WGPUTextureViewDimension_CubeArray;
                viewDesc.baseMipLevel = 0;
                viewDesc.mipLevelCount = kMipLevelCount;
                viewDesc.baseArrayLayer = 0;
                viewDesc.arrayLayerCount = kLayers;
                viewDesc.aspect = WGPUTextureAspect_All;
                t.displayView = wgpuTextureCreateView(t.texture, &viewDesc);
                if (!t.displayView)
                    return false;

                m_textures.push_back(t);
                return true;
            }
            else
            {
                // Upstream cube fallback: 6-face cube, red/dark-red look.
                constexpr uint32_t kLayers = 6;
                TextureSet t{};
                t.dim = ViewDim::TexCube;
                t.layers = kLayers;
                t.texture = CreateMipmapableTexture(ctx, kLayers, WGPUTextureViewDimension_Cube, "mip_cube_fb");
                if (!t.texture)
                    return false;

                Rgba border{255, 0, 0, 255};
                Rgba bg{64, 0, 0, 255};
                for (uint32_t face = 0; face < kLayers; ++face)
                {
                    std::vector<uint8_t> pixels;
                    MakeCanvasImage(pixels,
                                    kImageSize,
                                    kImageSize,
                                    border,
                                    bg,
                                    [face](uint8_t *b, uint32_t w, uint32_t h)
                                    { DrawCubeFallbackFace(b, w, h, face); });
                    pwgpu::test::UploadRGBA8Texture(ctx.queue,
                                                    t.texture,
                                                    pixels.data(),
                                                    kImageSize,
                                                    kImageSize,
                                                    0,
                                                    face);
                }

                WGPUTextureViewDescriptor viewDesc{};
                viewDesc.label = {"disp_view_cube_fb", WGPU_STRLEN};
                viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
                viewDesc.dimension = WGPUTextureViewDimension_Cube;
                viewDesc.baseMipLevel = 0;
                viewDesc.mipLevelCount = kMipLevelCount;
                viewDesc.baseArrayLayer = 0;
                viewDesc.arrayLayerCount = kLayers;
                viewDesc.aspect = WGPUTextureAspect_All;
                t.displayView = wgpuTextureCreateView(t.texture, &viewDesc);
                if (!t.displayView)
                    return false;

                m_textures.push_back(t);
                return true;
            }
        }

        WGPUBindGroupLayout MakeMipBgl(WGPUDevice device,
                                       WGPUTextureViewDimension dim,
                                       const char *label)
        {
            WGPUBindGroupLayoutEntry entries[2] = {};
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].sampler.type = WGPUSamplerBindingType_Filtering;
            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].texture.sampleType = WGPUTextureSampleType_Float;
            entries[1].texture.viewDimension = dim;
            entries[1].texture.multisampled = WGPUOptionalBool_False;
            WGPUBindGroupLayoutDescriptor desc{};
            desc.label = {label, WGPU_STRLEN};
            desc.entryCount = 2;
            desc.entries = entries;
            return wgpuDeviceCreateBindGroupLayout(device, &desc);
        }

        WGPURenderPipeline MakeMipPipeline(pwgpu::test::SampleContext &ctx,
                                           WGPUPipelineLayout layout,
                                           WGPUShaderModule psModule,
                                           const char *label)
        {
            WGPUColorTargetState colorTarget{};
            colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fs{};
            fs.module = psModule;
            fs.entryPoint = {"PSMain", WGPU_STRLEN};
            fs.targetCount = 1;
            fs.targets = &colorTarget;

            WGPURenderPipelineDescriptor desc{};
            desc.label = {label, WGPU_STRLEN};
            desc.layout = layout;
            desc.vertex.module = m_shaderModules[SHADER_GENMIP_VS];
            desc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            desc.primitive.cullMode = WGPUCullMode_None;
            desc.primitive.frontFace = WGPUFrontFace_CCW;
            desc.multisample.count = 1;
            desc.multisample.mask = 0xFFFFFFFF;
            desc.fragment = &fs;
            return wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
        }

        bool CreateDisplayPipelines(pwgpu::test::SampleContext &ctx)
        {
            // Mip-gen bind group layouts + pipelines (one per view dim).
            m_mipBgl2D = MakeMipBgl(ctx.device, WGPUTextureViewDimension_2D, "mipbgl_2d");
            m_mipBgl2DArray = MakeMipBgl(ctx.device, WGPUTextureViewDimension_2DArray, "mipbgl_2darr");
            m_mipBglCube = MakeMipBgl(ctx.device, WGPUTextureViewDimension_Cube, "mipbgl_cube");
            m_mipBglCubeArray = MakeMipBgl(ctx.device, WGPUTextureViewDimension_CubeArray, "mipbgl_cubearr");
            if (!m_mipBgl2D || !m_mipBgl2DArray || !m_mipBglCube || !m_mipBglCubeArray)
                return false;

            WGPUBindGroupLayout mipBgls[4] = {m_mipBgl2D, m_mipBgl2DArray, m_mipBglCube, m_mipBglCubeArray};
            const char *mipPlLabels[4] = {"mippl_2d", "mippl_2darr", "mippl_cube", "mippl_cubearr"};
            WGPUShaderModule mipPs[4] = {m_shaderModules[SHADER_GENMIP_2D],
                                         m_shaderModules[SHADER_GENMIP_2DARRAY],
                                         m_shaderModules[SHADER_GENMIP_CUBE],
                                         m_shaderModules[SHADER_GENMIP_CUBEARRAY]};
            for (int i = 0; i < 4; ++i)
            {
                WGPUPipelineLayoutDescriptor pld{};
                pld.label = {mipPlLabels[i], WGPU_STRLEN};
                pld.bindGroupLayoutCount = 1;
                pld.bindGroupLayouts = &mipBgls[i];
                WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(ctx.device, &pld);
                if (!pl)
                    return false;
                if (i == 0)
                    m_mipPipelineLayout2D = pl;
                else if (i == 1)
                    m_mipPipelineLayoutArray = pl;
                else if (i == 2)
                    m_mipPipelineLayoutCube = pl;
                else
                    m_mipPipelineLayoutCubeArray = pl;

                m_mipPipelines[i] = MakeMipPipeline(ctx, pl, mipPs[i], mipPlLabels[i]);
                if (!m_mipPipelines[i])
                    return false;
            }

            // Display bind group layouts + pipelines (one per view dim).
            m_displayBgl2D = MakeDisplayBgl(ctx.device, WGPUTextureViewDimension_2D, "dispbgl_2d");
            m_displayBgl2DArray = MakeDisplayBgl(ctx.device, WGPUTextureViewDimension_2DArray, "dispbgl_2darr");
            m_displayBglCube = MakeDisplayBgl(ctx.device, WGPUTextureViewDimension_Cube, "dispbgl_cube");
            m_displayBglCubeArray = MakeDisplayBgl(ctx.device, WGPUTextureViewDimension_CubeArray, "dispbgl_cubearr");
            if (!m_displayBgl2D || !m_displayBgl2DArray || !m_displayBglCube || !m_displayBglCubeArray)
                return false;

            WGPUBindGroupLayout displayBgls[4] = {
                m_displayBgl2D, m_displayBgl2DArray, m_displayBglCube, m_displayBglCubeArray};
            WGPUShaderModule displayVs[4] = {m_shaderModules[SHADER_DISP_VS_2D],
                                             m_shaderModules[SHADER_DISP_VS_2DARRAY],
                                             m_shaderModules[SHADER_DISP_VS_CUBE],
                                             m_shaderModules[SHADER_DISP_VS_CUBEARRAY]};
            WGPUShaderModule displayPs[4] = {m_shaderModules[SHADER_DISP_PS_2D],
                                             m_shaderModules[SHADER_DISP_PS_2DARRAY],
                                             m_shaderModules[SHADER_DISP_PS_CUBE],
                                             m_shaderModules[SHADER_DISP_PS_CUBEARRAY]};
            const char *displayPlLabels[4] = {"disppl_2d", "disppl_2darr", "disppl_cube", "disppl_cubearr"};

            for (int i = 0; i < 4; ++i)
            {
                WGPUPipelineLayoutDescriptor pld{};
                pld.label = {displayPlLabels[i], WGPU_STRLEN};
                pld.bindGroupLayoutCount = 1;
                pld.bindGroupLayouts = &displayBgls[i];
                m_displayPipelineLayout[i] = wgpuDeviceCreatePipelineLayout(ctx.device, &pld);
                if (!m_displayPipelineLayout[i])
                    return false;
            }

            WGPUVertexAttribute attributes[2] = {};
            attributes[0].format = WGPUVertexFormat_Float32x4;
            attributes[0].offset = pwgpu::test::cube::kPositionOffset;
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x2;
            attributes[1].offset = pwgpu::test::cube::kUvOffset;
            attributes[1].shaderLocation = 1;

            WGPUVertexBufferLayout vbl{};
            vbl.arrayStride = pwgpu::test::cube::kVertexStride;
            vbl.stepMode = WGPUVertexStepMode_Vertex;
            vbl.attributeCount = 2;
            vbl.attributes = attributes;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = ctx.surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUDepthStencilState depthState{};
            depthState.format = WGPUTextureFormat_Depth24Plus;
            depthState.depthWriteEnabled = WGPUOptionalBool_True;
            depthState.depthCompare = WGPUCompareFunction_Less;
            depthState.stencilFront.compare = WGPUCompareFunction_Always;
            depthState.stencilBack.compare = WGPUCompareFunction_Always;

            // Assign display pipelines to each texture set.
            for (TextureSet &t : m_textures)
            {
                int i = 0;
                switch (t.dim)
                {
                case ViewDim::Tex2D:
                    i = 0;
                    break;
                case ViewDim::Tex2DArray:
                    i = 1;
                    break;
                case ViewDim::TexCube:
                    i = 2;
                    break;
                case ViewDim::TexCubeArray:
                    i = 3;
                    break;
                }

                WGPUFragmentState fs{};
                fs.module = displayPs[i];
                fs.entryPoint = {"PSMain", WGPU_STRLEN};
                fs.targetCount = 1;
                fs.targets = &colorTarget;

                WGPURenderPipelineDescriptor desc{};
                desc.label = {"disp_pipe", WGPU_STRLEN};
                desc.layout = m_displayPipelineLayout[i];
                desc.vertex.module = displayVs[i];
                desc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
                desc.vertex.bufferCount = 1;
                desc.vertex.buffers = &vbl;
                desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
                desc.primitive.cullMode = WGPUCullMode_Back;
                desc.primitive.frontFace = WGPUFrontFace_CCW;
                desc.multisample.count = 1;
                desc.multisample.mask = 0xFFFFFFFF;
                desc.depthStencil = &depthState;
                desc.fragment = &fs;
                t.displayPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
                if (!t.displayPipeline)
                    return false;
            }
            return true;
        }

        WGPUBindGroupLayout MakeDisplayBgl(WGPUDevice device,
                                           WGPUTextureViewDimension dim,
                                           const char *label)
        {
            WGPUBindGroupLayoutEntry entries[3] = {};
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Vertex;
            entries[0].buffer.type = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = 64;
            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
            entries[2].binding = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].texture.sampleType = WGPUTextureSampleType_Float;
            entries[2].texture.viewDimension = dim;
            entries[2].texture.multisampled = WGPUOptionalBool_False;
            WGPUBindGroupLayoutDescriptor desc{};
            desc.label = {label, WGPU_STRLEN};
            desc.entryCount = 3;
            desc.entries = entries;
            return wgpuDeviceCreateBindGroupLayout(device, &desc);
        }

        bool CreateDisplayBindGroup(pwgpu::test::SampleContext &ctx, TextureSet &t)
        {
            WGPUBindGroupLayout layout = nullptr;
            switch (t.dim)
            {
            case ViewDim::Tex2D:
                layout = m_displayBgl2D;
                break;
            case ViewDim::Tex2DArray:
                layout = m_displayBgl2DArray;
                break;
            case ViewDim::TexCube:
                layout = m_displayBglCube;
                break;
            case ViewDim::TexCubeArray:
                layout = m_displayBglCubeArray;
                break;
            }

            WGPUBindGroupEntry bg[3] = {};
            bg[0].binding = 0;
            bg[0].buffer = m_uniformBuffer;
            bg[0].size = 64;
            bg[1].binding = 1;
            bg[1].sampler = m_displaySampler;
            bg[2].binding = 2;
            bg[2].textureView = t.displayView;

            WGPUBindGroupDescriptor desc{};
            desc.label = {"disp_bg", WGPU_STRLEN};
            desc.layout = layout;
            desc.entryCount = 3;
            desc.entries = bg;
            t.displayBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &desc);
            return t.displayBindGroup != nullptr;
        }

        bool GenerateMips(pwgpu::test::SampleContext &ctx, TextureSet &t)
        {
            if (kMipLevelCount <= 1)
                return true;

            int pipelineIdx = 0;
            WGPUTextureViewDimension bindingDim = WGPUTextureViewDimension_2D;
            WGPUBindGroupLayout bgl = m_mipBgl2D;
            switch (t.dim)
            {
            case ViewDim::Tex2D:
                pipelineIdx = 0;
                bindingDim = WGPUTextureViewDimension_2D;
                bgl = m_mipBgl2D;
                break;
            case ViewDim::Tex2DArray:
                pipelineIdx = 1;
                bindingDim = WGPUTextureViewDimension_2DArray;
                bgl = m_mipBgl2DArray;
                break;
            case ViewDim::TexCube:
                pipelineIdx = 2;
                bindingDim = WGPUTextureViewDimension_Cube;
                bgl = m_mipBglCube;
                break;
            case ViewDim::TexCubeArray:
                pipelineIdx = 3;
                bindingDim = WGPUTextureViewDimension_CubeArray;
                bgl = m_mipBglCubeArray;
                break;
            }

            WGPUCommandEncoderDescriptor encoderDesc{};
            encoderDesc.label = {"genmip_encoder", WGPU_STRLEN};
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device, &encoderDesc);
            if (!encoder)
                return false;

            std::vector<WGPUTextureView> &ownedViews = m_mipGenViews;
            std::vector<WGPUBindGroup> &ownedBgs = m_mipGenBindGroups;

            for (uint32_t baseMip = 1; baseMip < kMipLevelCount; ++baseMip)
            {
                // Source view: previous mip, all layers, with view dim matching the binding.
                WGPUTextureViewDescriptor srcViewDesc{};
                srcViewDesc.label = {"mip_src", WGPU_STRLEN};
                srcViewDesc.format = WGPUTextureFormat_RGBA8Unorm;
                srcViewDesc.dimension = bindingDim;
                srcViewDesc.baseMipLevel = baseMip - 1;
                srcViewDesc.mipLevelCount = 1;
                srcViewDesc.baseArrayLayer = 0;
                srcViewDesc.arrayLayerCount = t.layers;
                srcViewDesc.aspect = WGPUTextureAspect_All;
                WGPUTextureView srcView = wgpuTextureCreateView(t.texture, &srcViewDesc);
                if (!srcView)
                {
                    wgpuCommandEncoderRelease(encoder);
                    return false;
                }
                ownedViews.push_back(srcView);

                WGPUBindGroupEntry bgEntries[2] = {};
                bgEntries[0].binding = 0;
                bgEntries[0].sampler = m_mipSampler;
                bgEntries[1].binding = 1;
                bgEntries[1].textureView = srcView;
                WGPUBindGroupDescriptor bgDesc{};
                bgDesc.label = {"genmip_bg", WGPU_STRLEN};
                bgDesc.layout = bgl;
                bgDesc.entryCount = 2;
                bgDesc.entries = bgEntries;
                WGPUBindGroup bg = wgpuDeviceCreateBindGroup(ctx.device, &bgDesc);
                if (!bg)
                {
                    wgpuCommandEncoderRelease(encoder);
                    return false;
                }
                ownedBgs.push_back(bg);

                for (uint32_t layer = 0; layer < t.layers; ++layer)
                {
                    WGPUTextureViewDescriptor dstViewDesc{};
                    dstViewDesc.label = {"mip_dst", WGPU_STRLEN};
                    dstViewDesc.format = WGPUTextureFormat_RGBA8Unorm;
                    dstViewDesc.dimension = WGPUTextureViewDimension_2D;
                    dstViewDesc.baseMipLevel = baseMip;
                    dstViewDesc.mipLevelCount = 1;
                    dstViewDesc.baseArrayLayer = layer;
                    dstViewDesc.arrayLayerCount = 1;
                    dstViewDesc.aspect = WGPUTextureAspect_All;
                    WGPUTextureView dstView = wgpuTextureCreateView(t.texture, &dstViewDesc);
                    if (!dstView)
                    {
                        wgpuCommandEncoderRelease(encoder);
                        return false;
                    }
                    ownedViews.push_back(dstView);

                    WGPURenderPassColorAttachment colorAttachment{};
                    colorAttachment.view = dstView;
                    colorAttachment.loadOp = WGPULoadOp_Clear;
                    colorAttachment.storeOp = WGPUStoreOp_Store;
                    colorAttachment.clearValue = {0, 0, 0, 1};
                    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

                    WGPURenderPassDescriptor passDesc{};
                    passDesc.label = {"genmip_pass", WGPU_STRLEN};
                    passDesc.colorAttachmentCount = 1;
                    passDesc.colorAttachments = &colorAttachment;

                    WGPURenderPassEncoder pass =
                        wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
                    wgpuRenderPassEncoderSetPipeline(pass, m_mipPipelines[pipelineIdx]);
                    wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
                    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, layer);
                    wgpuRenderPassEncoderEnd(pass);
                    wgpuRenderPassEncoderRelease(pass);
                }
            }

            WGPUCommandBufferDescriptor cbDesc{};
            cbDesc.label = {"genmip_cb", WGPU_STRLEN};
            WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, &cbDesc);
            wgpuCommandEncoderRelease(encoder);
            if (!cb)
                return false;
            wgpuQueueSubmit(ctx.queue, 1, &cb);
            wgpuCommandBufferRelease(cb);

            // Keep bind groups + views alive until Shutdown — releasing them here
            // hits VUID-vkDestroyDescriptorPool-descriptorPool-00303 because the
            // queue hasn't necessarily finished the mip-gen command buffer yet.
            return true;
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

        WGPUShaderModule m_shaderModules[SHADER_COUNT] = {};

        WGPUBuffer m_vertexBuffer = nullptr;
        WGPUBuffer m_uniformBuffer = nullptr;
        WGPUSampler m_mipSampler = nullptr;
        WGPUSampler m_displaySampler = nullptr;

        WGPUBindGroupLayout m_mipBgl2D = nullptr;
        WGPUBindGroupLayout m_mipBgl2DArray = nullptr;
        WGPUBindGroupLayout m_mipBglCube = nullptr;
        WGPUBindGroupLayout m_mipBglCubeArray = nullptr;
        WGPUPipelineLayout m_mipPipelineLayout2D = nullptr;
        WGPUPipelineLayout m_mipPipelineLayoutArray = nullptr;
        WGPUPipelineLayout m_mipPipelineLayoutCube = nullptr;
        WGPUPipelineLayout m_mipPipelineLayoutCubeArray = nullptr;
        WGPURenderPipeline m_mipPipelines[4] = {};

        WGPUBindGroupLayout m_displayBgl2D = nullptr;
        WGPUBindGroupLayout m_displayBgl2DArray = nullptr;
        WGPUBindGroupLayout m_displayBglCube = nullptr;
        WGPUBindGroupLayout m_displayBglCubeArray = nullptr;
        WGPUPipelineLayout m_displayPipelineLayout[4] = {};

        std::vector<TextureSet> m_textures;
        std::vector<WGPUBindGroup> m_mipGenBindGroups;
        std::vector<WGPUTextureView> m_mipGenViews;

        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;

        uint32_t m_instanceIndex = 0;
    };

    pwgpu::test::SampleAppDesc MakeGenerateMipmapDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "GenerateMipmapTest";
        desc.windowTitle = "PhasmaWebGPU Generate Mipmap";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    GenerateMipmapSample sample;
    pwgpu::test::SampleApp app(sample, MakeGenerateMipmapDesc());
    return app.Run(argc, argv);
}
