#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

#include "../Common/CubeGeometry.h"
#include "../Common/SampleApp.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"


#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#include "rapidjson/document.h"

#include <cmath>
#include <cstring>

// 1:1 C++ port of webgpu-samples/sample/textRenderingMsdf.
// Inlines the msdfText.ts helper classes (MsdfFont / MsdfText / MsdfTextRenderer)
// as local types below so the whole sample lives in one .cpp file.

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265358979323846f;

    // ------------------------------------------------------------------
    // Font / text data structures (mirror msdfText.ts).
    // ------------------------------------------------------------------

    struct MsdfChar
    {
        int id = 0;
        int charIndex = 0;
        float width = 0.f;
        float height = 0.f;
        float xoffset = 0.f;
        float yoffset = 0.f;
        float xadvance = 0.f;
        float x = 0.f;
        float y = 0.f;
    };

    // charCode -> per-character kerning adjustments.
    using KerningMap = std::unordered_map<int, std::unordered_map<int, float>>;

    struct MsdfFont
    {
        float lineHeight = 0.f;
        float scaleW = 512.f;
        float scaleH = 512.f;
        std::unordered_map<int, MsdfChar> chars;
        KerningMap kernings;
        MsdfChar defaultChar{};

        WGPUTexture atlasTexture = nullptr;
        WGPUTextureView atlasView = nullptr;
        WGPUBuffer charsBuffer = nullptr;
        WGPUBindGroup fontBindGroup = nullptr;

        const MsdfChar *GetChar(int charCode) const
        {
            auto it = chars.find(charCode);
            if (it == chars.end())
                return &defaultChar;
            return &it->second;
        }

        // Returns xadvance, including kerning to nextCharCode when >= 0.
        float GetXAdvance(int charCode, int nextCharCode) const
        {
            const MsdfChar *ch = GetChar(charCode);
            if (nextCharCode >= 0)
            {
                auto it = kernings.find(charCode);
                if (it != kernings.end())
                {
                    auto kit = it->second.find(nextCharCode);
                    if (kit != it->second.end())
                        return ch->xadvance + kit->second;
                }
            }
            return ch->xadvance;
        }
    };

    struct MsdfTextMeasurements
    {
        float width = 0.f;
        float height = 0.f;
        std::vector<float> lineWidths;
        uint32_t printedCharCount = 0;
    };

    struct MsdfText
    {
        WGPUBuffer textBuffer = nullptr;
        WGPUBindGroup textBindGroup = nullptr;
        MsdfTextMeasurements measurements{};
        const MsdfFont *font = nullptr;
        // The first 24 floats of the storage buffer are managed here
        // (transform mat4 + color vec4 + scale f32 + 3 floats padding).
        float header[24] = {};
        bool dirty = true;

        void SetTransform(const mat4 &m)
        {
            std::memcpy(&header[0], glm::value_ptr(m), sizeof(float) * 16);
            dirty = true;
        }
        void SetColor(float r, float g, float b, float a)
        {
            header[16] = r;
            header[17] = g;
            header[18] = b;
            header[19] = a;
            dirty = true;
        }
        void SetPixelScale(float s)
        {
            header[20] = s;
            dirty = true;
        }
    };

    struct MsdfTextFormatOpts
    {
        bool centered = false;
        bool setPixelScale = false;
        float pixelScale = 1.f / 512.f;
        bool setColor = false;
        float color[4] = {1.f, 1.f, 1.f, 1.f};
    };

    // ------------------------------------------------------------------
    // File helpers.
    // ------------------------------------------------------------------

    bool ReadFileToString(const std::string &path, std::string &out)
    {
        FILE *f = std::fopen(path.c_str(), "rb");
        if (!f)
        {
            fprintf(stderr, "[TextMsdf] Failed to open %s\n", path.c_str());
            return false;
        }
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz < 0)
        {
            std::fclose(f);
            return false;
        }
        out.resize(static_cast<size_t>(sz));
        if (sz > 0 && std::fread(out.data(), 1, out.size(), f) != out.size())
        {
            std::fclose(f);
            return false;
        }
        std::fclose(f);
        return true;
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
            fprintf(stderr, "[TextMsdf] stbi_load %s failed: %s\n",
                    path.c_str(), stbi_failure_reason());
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------------
    // Text measurement (mirrors MsdfTextRenderer.measureText).
    // Calls `onChar(x, y, line, char)` for every printable glyph.
    // ------------------------------------------------------------------

    template <typename F>
    MsdfTextMeasurements MeasureText(const MsdfFont &font,
                                     const std::string &text,
                                     F &&onChar)
    {
        MsdfTextMeasurements out{};
        float textOffsetX = 0.f;
        float textOffsetY = 0.f;
        int line = 0;
        float maxWidth = 0.f;

        const size_t len = text.size();
        int nextCharCode = len > 0 ? static_cast<unsigned char>(text[0]) : -1;
        for (size_t i = 0; i < len; ++i)
        {
            int charCode = nextCharCode;
            nextCharCode = (i + 1 < len) ? static_cast<unsigned char>(text[i + 1]) : -1;

            switch (charCode)
            {
            case 10: // Newline
                out.lineWidths.push_back(textOffsetX);
                line++;
                maxWidth = std::max(maxWidth, textOffsetX);
                textOffsetX = 0.f;
                textOffsetY -= font.lineHeight;
                // fallthrough as in the TS source (which has no break on case 10).
            case 13: // CR
                break;
            case 32: // Space
                textOffsetX += font.GetXAdvance(charCode, -1);
                break;
            default:
            {
                const MsdfChar *ch = font.GetChar(charCode);
                onChar(textOffsetX, textOffsetY, line, *ch);
                textOffsetX += font.GetXAdvance(charCode, nextCharCode);
                out.printedCharCount++;
            }
            break;
            }
        }
        out.lineWidths.push_back(textOffsetX);
        maxWidth = std::max(maxWidth, textOffsetX);

        out.width = maxWidth;
        out.height = static_cast<float>(out.lineWidths.size()) * font.lineHeight;
        return out;
    }

    // ------------------------------------------------------------------
    // Sample.
    // ------------------------------------------------------------------

    class TextRenderingMsdfSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            // Load shaders.
            m_cubeVS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "cube.vert.hlsl").c_str(), "cube_vs");
            m_cubePS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "cube.pixel.hlsl").c_str(), "cube_ps");
            m_textVS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "msdfText.vert.hlsl").c_str(), "msdf_vs");
            m_textPS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "msdfText.pixel.hlsl").c_str(), "msdf_ps");
            if (!m_cubeVS || !m_cubePS || !m_textVS || !m_textPS)
                return false;

            // Cube vertex buffer + MVP UBO.
            m_cubeVB = pwgpu::test::CreateBuffer(ctx.device,
                                                 sizeof(pwgpu::test::cube::kVertices),
                                                 WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                                                 "cube_vb");
            m_cubeUBO = pwgpu::test::CreateBuffer(ctx.device, 64,
                                                  WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                                                  "cube_ubo");
            if (!m_cubeVB || !m_cubeUBO)
                return false;
            wgpuQueueWriteBuffer(ctx.queue, m_cubeVB, 0,
                                 pwgpu::test::cube::kVertices,
                                 sizeof(pwgpu::test::cube::kVertices));

            // Text renderer resources (sampler, camera UBO, layouts, pipeline).
            if (!CreateTextRendererResources(ctx))
                return false;

            // Load the MSDF font (JSON + PNG atlas).
            if (!CreateFont(ctx, pwgpu::test::GetAssetPath(ctx.exeDir,
                                                           "font/ya-hei-ascii-msdf.json")))
                return false;

            // Cube pipeline (matches upstream basic.vert + vertexPositionColor.frag).
            if (!CreateCubePipeline(ctx))
                return false;

            // Build the exact eight strings used by upstream main.ts.
            BuildUpstreamStrings(ctx);

            Resize(ctx, ctx.width, ctx.height);
            return m_depthTexture != nullptr && m_depthView != nullptr;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseDepthTarget();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor d{};
            d.label = {"msdf_depth", WGPU_STRLEN};
            d.dimension = WGPUTextureDimension_2D;
            d.size = {width, height, 1};
            d.mipLevelCount = 1;
            d.sampleCount = 1;
            d.format = WGPUTextureFormat_Depth24Plus;
            d.usage = WGPUTextureUsage_RenderAttachment;
            m_depthTexture = wgpuDeviceCreateTexture(ctx.device, &d);
            if (!m_depthTexture)
                return;

            WGPUTextureViewDescriptor dv{};
            dv.format = WGPUTextureFormat_Depth24Plus;
            dv.dimension = WGPUTextureViewDimension_2D;
            dv.mipLevelCount = 1;
            dv.arrayLayerCount = 1;
            dv.aspect = WGPUTextureAspect_DepthOnly;
            m_depthView = wgpuTextureCreateView(m_depthTexture, &dv);
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            const float aspect = ctx.height > 0
                                     ? static_cast<float>(ctx.width) /
                                           static_cast<float>(ctx.height)
                                     : 1.0f;
            mat4 projection = glm::perspectiveRH_ZO((2.f * kPi) / 5.f, aspect, 1.f, 100.f);
            projection[1][1] *= -1.f; // Vulkan Y-down NDC.

            const double now = ctx.timing.elapsedSeconds;
            const float rot = static_cast<float>(now / 5.0);

            mat4 view = glm::translate(mat4(1.f), vec3(0.f, 0.f, -5.f));

            mat4 model = glm::translate(mat4(1.f), vec3(0.f, 2.f, -3.f));
            model = glm::rotate(model, 1.f, vec3(sinf(rot), cosf(rot), 0.f));

            mat4 mvp = projection * view * model;
            wgpuQueueWriteBuffer(ctx.queue, m_cubeUBO, 0, glm::value_ptr(mvp), 64);

            // Camera uniform for the text renderer (projection + view, 2 x mat4).
            float cameraData[32];
            std::memcpy(&cameraData[0], glm::value_ptr(projection), 64);
            std::memcpy(&cameraData[16], glm::value_ptr(view), 64);
            wgpuQueueWriteBuffer(ctx.queue, m_cameraUBO, 0, cameraData, sizeof(cameraData));

            // Six face-label transforms (must match upstream getTextTransform order).
            mat4 textTransforms[6];
            textTransforms[0] = glm::translate(mat4(1.f), vec3(0.f, 0.f, 1.1f));
            {
                mat4 t = glm::translate(mat4(1.f), vec3(0.f, 0.f, -1.1f));
                textTransforms[1] = glm::rotate(t, kPi, vec3(0.f, 1.f, 0.f));
            }
            {
                mat4 t = glm::translate(mat4(1.f), vec3(1.1f, 0.f, 0.f));
                textTransforms[2] = glm::rotate(t, kPi / 2.f, vec3(0.f, 1.f, 0.f));
            }
            {
                mat4 t = glm::translate(mat4(1.f), vec3(-1.1f, 0.f, 0.f));
                textTransforms[3] = glm::rotate(t, -kPi / 2.f, vec3(0.f, 1.f, 0.f));
            }
            {
                mat4 t = glm::translate(mat4(1.f), vec3(0.f, 1.1f, 0.f));
                textTransforms[4] = glm::rotate(t, -kPi / 2.f, vec3(1.f, 0.f, 0.f));
            }
            {
                mat4 t = glm::translate(mat4(1.f), vec3(0.f, -1.1f, 0.f));
                textTransforms[5] = glm::rotate(t, kPi / 2.f, vec3(1.f, 0.f, 0.f));
            }
            for (int i = 0; i < 6; ++i)
            {
                mat4 m = model * textTransforms[i];
                m_texts[i].SetTransform(m);
            }

            // Title text + crawling large text — mirrors the main.ts update block.
            const float crawl = std::fmod(static_cast<float>(now / 2.5), 14.f);
            mat4 textMatrix = mat4(1.f);
            textMatrix = glm::rotate(textMatrix, -kPi / 8.f, vec3(1.f, 0.f, 0.f));
            textMatrix = glm::translate(textMatrix, vec3(0.f, crawl - 3.f, 0.f));
            m_texts[kTitleIdx].SetTransform(textMatrix);
            textMatrix = glm::translate(textMatrix, vec3(-3.f, -0.1f, 0.f));
            m_texts[kLargeIdx].SetTransform(textMatrix);

            // Flush any dirty per-text storage headers.
            for (auto &t : m_texts)
            {
                if (t.dirty)
                {
                    wgpuQueueWriteBuffer(ctx.queue, t.textBuffer, 0, t.header, sizeof(t.header));
                    t.dirty = false;
                }
            }
        }

        bool Execute(pwgpu::test::SampleContext &, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_depthView)
                return true;

            WGPURenderPassColorAttachment colorAttachment{};
            colorAttachment.view = frame.surfaceView;
            colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

            WGPURenderPassDepthStencilAttachment depthAttachment{};
            depthAttachment.view = m_depthView;
            depthAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthAttachment.depthStoreOp = WGPUStoreOp_Store;
            depthAttachment.depthClearValue = 1.0f;
            depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
            depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;

            WGPURenderPassDescriptor rp{};
            rp.label = {"msdf_pass", WGPU_STRLEN};
            rp.colorAttachmentCount = 1;
            rp.colorAttachments = &colorAttachment;
            rp.depthStencilAttachment = &depthAttachment;

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(frame.encoder, &rp);

            // Cube.
            wgpuRenderPassEncoderSetPipeline(pass, m_cubePipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, m_cubeBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_cubeVB, 0,
                                                 sizeof(pwgpu::test::cube::kVertices));
            wgpuRenderPassEncoderDraw(pass, pwgpu::test::cube::kVertexCount, 1, 0, 0);

            // Text strings (order matches upstream: 6 face labels, title, largeText).
            wgpuRenderPassEncoderSetPipeline(pass, m_textPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, m_font.fontBindGroup, 0, nullptr);
            for (const auto &t : m_texts)
            {
                if (t.measurements.printedCharCount == 0)
                    continue;
                wgpuRenderPassEncoderSetBindGroup(pass, 1, t.textBindGroup, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 4, t.measurements.printedCharCount, 0, 0);
            }

            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseDepthTarget();

            for (auto &t : m_texts)
            {
                if (t.textBindGroup)
                    wgpuBindGroupRelease(t.textBindGroup);
                if (t.textBuffer)
                    wgpuBufferRelease(t.textBuffer);
            }
            m_texts.clear();

            if (m_font.fontBindGroup)
                wgpuBindGroupRelease(m_font.fontBindGroup);
            if (m_font.charsBuffer)
                wgpuBufferRelease(m_font.charsBuffer);
            if (m_font.atlasView)
                wgpuTextureViewRelease(m_font.atlasView);
            if (m_font.atlasTexture)
                wgpuTextureRelease(m_font.atlasTexture);
            m_font = {};

            if (m_textPipeline)
                wgpuRenderPipelineRelease(m_textPipeline);
            if (m_textPipelineLayout)
                wgpuPipelineLayoutRelease(m_textPipelineLayout);
            if (m_textBGLayout)
                wgpuBindGroupLayoutRelease(m_textBGLayout);
            if (m_fontBGLayout)
                wgpuBindGroupLayoutRelease(m_fontBGLayout);
            if (m_sampler)
                wgpuSamplerRelease(m_sampler);
            if (m_cameraUBO)
                wgpuBufferRelease(m_cameraUBO);

            if (m_cubePipeline)
                wgpuRenderPipelineRelease(m_cubePipeline);
            if (m_cubePipelineLayout)
                wgpuPipelineLayoutRelease(m_cubePipelineLayout);
            if (m_cubeBindGroup)
                wgpuBindGroupRelease(m_cubeBindGroup);
            if (m_cubeBGLayout)
                wgpuBindGroupLayoutRelease(m_cubeBGLayout);

            if (m_textPS)
                wgpuShaderModuleRelease(m_textPS);
            if (m_textVS)
                wgpuShaderModuleRelease(m_textVS);
            if (m_cubePS)
                wgpuShaderModuleRelease(m_cubePS);
            if (m_cubeVS)
                wgpuShaderModuleRelease(m_cubeVS);

            if (m_cubeUBO)
                wgpuBufferRelease(m_cubeUBO);
            if (m_cubeVB)
                wgpuBufferRelease(m_cubeVB);
        }

    private:
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

        bool CreateTextRendererResources(pwgpu::test::SampleContext &ctx)
        {
            // Sampler.
            WGPUSamplerDescriptor s{};
            s.label = {"msdf_sampler", WGPU_STRLEN};
            s.addressModeU = WGPUAddressMode_ClampToEdge;
            s.addressModeV = WGPUAddressMode_ClampToEdge;
            s.addressModeW = WGPUAddressMode_ClampToEdge;
            s.magFilter = WGPUFilterMode_Linear;
            s.minFilter = WGPUFilterMode_Linear;
            s.mipmapFilter = WGPUMipmapFilterMode_Linear;
            s.lodMinClamp = 0.f;
            s.lodMaxClamp = 32.f;
            s.compare = WGPUCompareFunction_Undefined;
            s.maxAnisotropy = 16;
            m_sampler = wgpuDeviceCreateSampler(ctx.device, &s);
            if (!m_sampler)
                return false;

            // Camera UBO: projection (mat4) + view (mat4) = 128 bytes.
            m_cameraUBO = pwgpu::test::CreateBuffer(ctx.device, 128,
                                                    WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                                                    "msdf_camera_ubo");
            if (!m_cameraUBO)
                return false;

            // Font bind-group layout (group 0): texture, sampler, chars storage.
            WGPUBindGroupLayoutEntry fontEntries[3] = {};
            fontEntries[0].binding = 0;
            fontEntries[0].visibility = WGPUShaderStage_Fragment;
            fontEntries[0].texture.sampleType = WGPUTextureSampleType_Float;
            fontEntries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
            fontEntries[0].texture.multisampled = WGPUOptionalBool_False;
            fontEntries[1].binding = 1;
            fontEntries[1].visibility = WGPUShaderStage_Fragment;
            fontEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;
            fontEntries[2].binding = 2;
            fontEntries[2].visibility = WGPUShaderStage_Vertex;
            fontEntries[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

            WGPUBindGroupLayoutDescriptor fontBGLDesc{};
            fontBGLDesc.label = {"msdf_font_bgl", WGPU_STRLEN};
            fontBGLDesc.entryCount = 3;
            fontBGLDesc.entries = fontEntries;
            m_fontBGLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &fontBGLDesc);
            if (!m_fontBGLayout)
                return false;

            // Text bind-group layout (group 1): camera UBO + per-text storage.
            WGPUBindGroupLayoutEntry textEntries[2] = {};
            textEntries[0].binding = 0;
            textEntries[0].visibility = WGPUShaderStage_Vertex;
            textEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
            textEntries[0].buffer.minBindingSize = 128;
            textEntries[1].binding = 1;
            textEntries[1].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            textEntries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

            WGPUBindGroupLayoutDescriptor textBGLDesc{};
            textBGLDesc.label = {"msdf_text_bgl", WGPU_STRLEN};
            textBGLDesc.entryCount = 2;
            textBGLDesc.entries = textEntries;
            m_textBGLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &textBGLDesc);
            if (!m_textBGLayout)
                return false;

            // Pipeline layout (font + text bind groups).
            WGPUBindGroupLayout bgls[2] = {m_fontBGLayout, m_textBGLayout};
            WGPUPipelineLayoutDescriptor plDesc{};
            plDesc.label = {"msdf_pl", WGPU_STRLEN};
            plDesc.bindGroupLayoutCount = 2;
            plDesc.bindGroupLayouts = bgls;
            m_textPipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &plDesc);
            if (!m_textPipelineLayout)
                return false;

            // Pipeline: triangle-strip (4 verts) with MSDF premultiplied blending.
            WGPUBlendState blend{};
            blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
            blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
            blend.color.operation = WGPUBlendOperation_Add;
            blend.alpha.srcFactor = WGPUBlendFactor_One;
            blend.alpha.dstFactor = WGPUBlendFactor_One;
            blend.alpha.operation = WGPUBlendOperation_Add;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = ctx.surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;
            colorTarget.blend = &blend;

            WGPUFragmentState fs{};
            fs.module = m_textPS;
            fs.entryPoint = {"PSMain", WGPU_STRLEN};
            fs.targetCount = 1;
            fs.targets = &colorTarget;

            WGPUDepthStencilState depth{};
            depth.format = WGPUTextureFormat_Depth24Plus;
            depth.depthWriteEnabled = WGPUOptionalBool_False;
            depth.depthCompare = WGPUCompareFunction_Less;
            depth.stencilFront.compare = WGPUCompareFunction_Always;
            depth.stencilBack.compare = WGPUCompareFunction_Always;

            WGPURenderPipelineDescriptor pipe{};
            pipe.label = {"msdf_pipe", WGPU_STRLEN};
            pipe.layout = m_textPipelineLayout;
            pipe.vertex.module = m_textVS;
            pipe.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipe.vertex.bufferCount = 0;
            pipe.primitive.topology = WGPUPrimitiveTopology_TriangleStrip;
            pipe.primitive.stripIndexFormat = WGPUIndexFormat_Uint32;
            pipe.primitive.cullMode = WGPUCullMode_None;
            pipe.primitive.frontFace = WGPUFrontFace_CCW;
            pipe.multisample.count = 1;
            pipe.multisample.mask = 0xFFFFFFFFu;
            pipe.depthStencil = &depth;
            pipe.fragment = &fs;
            m_textPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipe);
            return m_textPipeline != nullptr;
        }

        bool CreateFont(pwgpu::test::SampleContext &ctx, const std::string &fontJsonPath)
        {
            std::string buf;
            if (!ReadFileToString(fontJsonPath, buf))
                return false;

            rapidjson::Document doc;
            doc.Parse(buf.c_str());
            if (doc.HasParseError() || !doc.IsObject())
            {
                fprintf(stderr, "[TextMsdf] JSON parse failed: %s\n", fontJsonPath.c_str());
                return false;
            }

            const auto &common = doc["common"];
            m_font.lineHeight = common["lineHeight"].GetFloat();
            m_font.scaleW = common["scaleW"].GetFloat();
            m_font.scaleH = common["scaleH"].GetFloat();

            // Locate the atlas PNG (first page).
            std::string pageName = "ya-hei-ascii.png";
            if (doc.HasMember("pages") && doc["pages"].IsArray() && doc["pages"].Size() > 0)
                pageName = doc["pages"][0].GetString();

            // Assets are copied into exeDir/font/, same as the JSON.
            std::string pngPath = pwgpu::test::GetAssetPath(ctx.exeDir, ("font/" + pageName).c_str());

            LoadedImage img{};
            if (!LoadImageRGBA8(pngPath, img))
                return false;

            if (!CreateAtlasTexture(ctx, img))
            {
                stbi_image_free(img.pixels);
                return false;
            }
            stbi_image_free(img.pixels);

            // Parse chars + build GPU chars buffer (8 floats per char, matches WGSL Char).
            const auto &charsJson = doc["chars"].GetArray();
            const uint32_t charCount = charsJson.Size();
            std::vector<float> charsData(charCount * 8);

            const float u = 1.0f / m_font.scaleW;
            const float v = 1.0f / m_font.scaleH;

            uint32_t offset = 0;
            for (uint32_t i = 0; i < charCount; ++i)
            {
                const auto &cj = charsJson[i];
                MsdfChar c{};
                c.id = cj["id"].GetInt();
                c.charIndex = static_cast<int>(i);
                c.x = cj["x"].GetFloat();
                c.y = cj["y"].GetFloat();
                c.width = cj["width"].GetFloat();
                c.height = cj["height"].GetFloat();
                c.xoffset = cj["xoffset"].GetFloat();
                c.yoffset = cj["yoffset"].GetFloat();
                c.xadvance = cj["xadvance"].GetFloat();
                m_font.chars[c.id] = c;

                charsData[offset + 0] = c.x * u;      // texOffset.x
                charsData[offset + 1] = c.y * v;      // texOffset.y
                charsData[offset + 2] = c.width * u;  // texExtent.x
                charsData[offset + 3] = c.height * v; // texExtent.y
                charsData[offset + 4] = c.width;      // size.x
                charsData[offset + 5] = c.height;     // size.y
                charsData[offset + 6] = c.xoffset;    // offset.x
                charsData[offset + 7] = -c.yoffset;   // offset.y (flipped)
                offset += 8;
            }
            if (!m_font.chars.empty())
                m_font.defaultChar = m_font.chars.begin()->second;

            // Kernings.
            if (doc.HasMember("kernings") && doc["kernings"].IsArray())
            {
                for (const auto &k : doc["kernings"].GetArray())
                {
                    int first = k["first"].GetInt();
                    int second = k["second"].GetInt();
                    float amount = k["amount"].GetFloat();
                    m_font.kernings[first][second] = amount;
                }
            }

            // Upload the chars storage buffer.
            const uint64_t charsBytes = static_cast<uint64_t>(charsData.size()) * sizeof(float);
            m_font.charsBuffer = pwgpu::test::CreateBuffer(ctx.device, charsBytes,
                                                           WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
                                                           "msdf_chars");
            if (!m_font.charsBuffer)
                return false;
            wgpuQueueWriteBuffer(ctx.queue, m_font.charsBuffer, 0, charsData.data(), charsBytes);

            // Font bind group.
            WGPUBindGroupEntry fg[3] = {};
            fg[0].binding = 0;
            fg[0].textureView = m_font.atlasView;
            fg[1].binding = 1;
            fg[1].sampler = m_sampler;
            fg[2].binding = 2;
            fg[2].buffer = m_font.charsBuffer;
            fg[2].size = charsBytes;

            WGPUBindGroupDescriptor bg{};
            bg.label = {"msdf_font_bg", WGPU_STRLEN};
            bg.layout = m_fontBGLayout;
            bg.entryCount = 3;
            bg.entries = fg;
            m_font.fontBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bg);
            return m_font.fontBindGroup != nullptr;
        }

        bool CreateAtlasTexture(pwgpu::test::SampleContext &ctx, const LoadedImage &img)
        {
            WGPUTextureDescriptor d{};
            d.label = {"msdf_atlas", WGPU_STRLEN};
            d.dimension = WGPUTextureDimension_2D;
            d.size = {static_cast<uint32_t>(img.width), static_cast<uint32_t>(img.height), 1};
            d.mipLevelCount = 1;
            d.sampleCount = 1;
            d.format = WGPUTextureFormat_RGBA8Unorm;
            d.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            m_font.atlasTexture = wgpuDeviceCreateTexture(ctx.device, &d);
            if (!m_font.atlasTexture)
                return false;

            pwgpu::test::UploadRGBA8Texture(ctx.queue, m_font.atlasTexture, img.pixels,
                                            static_cast<uint32_t>(img.width),
                                            static_cast<uint32_t>(img.height));

            m_font.atlasView = pwgpu::test::CreateTextureView(m_font.atlasTexture,
                                                              WGPUTextureFormat_RGBA8Unorm);
            return m_font.atlasView != nullptr;
        }

        bool CreateCubePipeline(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry bgle{};
            bgle.binding = 0;
            bgle.visibility = WGPUShaderStage_Vertex;
            bgle.buffer.type = WGPUBufferBindingType_Uniform;
            bgle.buffer.minBindingSize = 64;

            WGPUBindGroupLayoutDescriptor bglDesc{};
            bglDesc.label = {"cube_bgl", WGPU_STRLEN};
            bglDesc.entryCount = 1;
            bglDesc.entries = &bgle;
            m_cubeBGLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);
            if (!m_cubeBGLayout)
                return false;

            WGPUPipelineLayoutDescriptor plDesc{};
            plDesc.label = {"cube_pl", WGPU_STRLEN};
            plDesc.bindGroupLayoutCount = 1;
            plDesc.bindGroupLayouts = &m_cubeBGLayout;
            m_cubePipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &plDesc);
            if (!m_cubePipelineLayout)
                return false;

            WGPUBindGroupEntry bge{};
            bge.binding = 0;
            bge.buffer = m_cubeUBO;
            bge.size = 64;

            WGPUBindGroupDescriptor bgDesc{};
            bgDesc.label = {"cube_bg", WGPU_STRLEN};
            bgDesc.layout = m_cubeBGLayout;
            bgDesc.entryCount = 1;
            bgDesc.entries = &bge;
            m_cubeBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bgDesc);
            if (!m_cubeBindGroup)
                return false;

            WGPUVertexAttribute attrs[2] = {};
            attrs[0].format = WGPUVertexFormat_Float32x4;
            attrs[0].offset = pwgpu::test::cube::kPositionOffset;
            attrs[0].shaderLocation = 0;
            attrs[1].format = WGPUVertexFormat_Float32x2;
            attrs[1].offset = pwgpu::test::cube::kUvOffset;
            attrs[1].shaderLocation = 1;

            WGPUVertexBufferLayout vbl{};
            vbl.arrayStride = pwgpu::test::cube::kVertexStride;
            vbl.stepMode = WGPUVertexStepMode_Vertex;
            vbl.attributeCount = 2;
            vbl.attributes = attrs;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = ctx.surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fs{};
            fs.module = m_cubePS;
            fs.entryPoint = {"PSMain", WGPU_STRLEN};
            fs.targetCount = 1;
            fs.targets = &colorTarget;

            WGPUDepthStencilState depth{};
            depth.format = WGPUTextureFormat_Depth24Plus;
            depth.depthWriteEnabled = WGPUOptionalBool_True;
            depth.depthCompare = WGPUCompareFunction_Less;
            depth.stencilFront.compare = WGPUCompareFunction_Always;
            depth.stencilBack.compare = WGPUCompareFunction_Always;

            WGPURenderPipelineDescriptor pipe{};
            pipe.label = {"cube_pipe", WGPU_STRLEN};
            pipe.layout = m_cubePipelineLayout;
            pipe.vertex.module = m_cubeVS;
            pipe.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipe.vertex.bufferCount = 1;
            pipe.vertex.buffers = &vbl;
            pipe.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipe.primitive.cullMode = WGPUCullMode_Back;
            pipe.primitive.frontFace = WGPUFrontFace_CCW;
            pipe.multisample.count = 1;
            pipe.multisample.mask = 0xFFFFFFFFu;
            pipe.depthStencil = &depth;
            pipe.fragment = &fs;
            m_cubePipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipe);
            return m_cubePipeline != nullptr;
        }

        // Create a formatted text entry. Mirrors MsdfTextRenderer.formatText.
        // `text` is ASCII (the upstream font is ASCII anyway).
        void FormatText(pwgpu::test::SampleContext &ctx,
                        const std::string &text,
                        const MsdfTextFormatOpts &opts)
        {
            MsdfText out{};
            out.font = &m_font;

            // 24 floats (header) + 4 floats * (text.length + 6) char entries, to match
            // the upstream allocation (which reserves a little slack).
            const uint32_t reservedChars = static_cast<uint32_t>(text.size()) + 6u;
            const uint64_t bufBytes = (24ull + 4ull * reservedChars) * sizeof(float);
            out.textBuffer = pwgpu::test::CreateBuffer(ctx.device, bufBytes,
                                                       WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
                                                       "msdf_text_buf");
            if (!out.textBuffer)
                return;

            // Header defaults (identity transform, white, pixelScale 1/512).
            std::memset(out.header, 0, sizeof(out.header));
            out.header[0] = out.header[5] = out.header[10] = out.header[15] = 1.f;
            out.header[16] = out.header[17] = out.header[18] = out.header[19] = 1.f;
            out.header[20] = 1.f / 512.f;

            // Measure + append character entries (offset=24 to skip header, in floats).
            std::vector<float> charsData(4u * reservedChars, 0.f);
            uint32_t charOffset = 0;

            if (opts.centered)
            {
                auto measurements = MeasureText(m_font, text, [](float, float, int, const MsdfChar &) {});
                out.measurements = MeasureText(
                    m_font, text,
                    [&](float tx, float ty, int line, const MsdfChar &c)
                    {
                        float lineOffset = measurements.width * -0.5f -
                                           (measurements.width - measurements.lineWidths[line]) * -0.5f;
                        charsData[charOffset + 0] = tx + lineOffset;
                        charsData[charOffset + 1] = ty + measurements.height * 0.5f;
                        charsData[charOffset + 2] = static_cast<float>(c.charIndex);
                        charOffset += 4;
                    });
                out.measurements = measurements;
            }
            else
            {
                out.measurements = MeasureText(
                    m_font, text,
                    [&](float tx, float ty, int, const MsdfChar &c)
                    {
                        charsData[charOffset + 0] = tx;
                        charsData[charOffset + 1] = ty;
                        charsData[charOffset + 2] = static_cast<float>(c.charIndex);
                        charOffset += 4;
                    });
            }

            if (opts.setPixelScale)
                out.header[20] = opts.pixelScale;
            if (opts.setColor)
            {
                out.header[16] = opts.color[0];
                out.header[17] = opts.color[1];
                out.header[18] = opts.color[2];
                out.header[19] = opts.color[3];
            }

            // Upload header + char entries in one go.
            std::vector<float> fullBuf(24 + charsData.size(), 0.f);
            std::memcpy(fullBuf.data(), out.header, sizeof(out.header));
            std::memcpy(fullBuf.data() + 24, charsData.data(), charsData.size() * sizeof(float));
            wgpuQueueWriteBuffer(ctx.queue, out.textBuffer, 0, fullBuf.data(),
                                 fullBuf.size() * sizeof(float));
            out.dirty = false;

            // Per-text bind group (camera + this text's storage buffer).
            WGPUBindGroupEntry entries[2] = {};
            entries[0].binding = 0;
            entries[0].buffer = m_cameraUBO;
            entries[0].size = 128;
            entries[1].binding = 1;
            entries[1].buffer = out.textBuffer;
            entries[1].size = bufBytes;

            WGPUBindGroupDescriptor bg{};
            bg.label = {"msdf_text_bg", WGPU_STRLEN};
            bg.layout = m_textBGLayout;
            bg.entryCount = 2;
            bg.entries = entries;
            out.textBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bg);

            m_texts.push_back(std::move(out));
        }

        // Build the eight strings used by upstream main.ts.
        void BuildUpstreamStrings(pwgpu::test::SampleContext &ctx)
        {
            auto label = [&](const char *text, float r, float g, float b)
            {
                MsdfTextFormatOpts o{};
                o.centered = true;
                o.setPixelScale = true;
                o.pixelScale = 1.f / 128.f;
                o.setColor = true;
                o.color[0] = r;
                o.color[1] = g;
                o.color[2] = b;
                o.color[3] = 1.f;
                FormatText(ctx, text, o);
            };

            label("Front", 1.f, 0.f, 0.f);
            label("Back", 0.f, 1.f, 1.f);
            label("Right", 0.f, 1.f, 0.f);
            label("Left", 1.f, 0.f, 1.f);
            label("Top", 0.f, 0.f, 1.f);
            label("Bottom", 1.f, 1.f, 0.f);

            // Title "WebGPU".
            {
                MsdfTextFormatOpts o{};
                o.centered = true;
                o.setPixelScale = true;
                o.pixelScale = 1.f / 128.f;
                FormatText(ctx, "WebGPU", o);
            }

            // Large crawl text (exact upstream string, leading newline preserved).
            static const char *kLargeText =
                "\n"
                "WebGPU exposes an API for performing operations, such as rendering\n"
                "and computation, on a Graphics Processing Unit.\n"
                "\n"
                "Graphics Processing Units, or GPUs for short, have been essential\n"
                "in enabling rich rendering and computational applications in personal\n"
                "computing. WebGPU is an API that exposes the capabilities of GPU\n"
                "hardware for the Web. The API is designed from the ground up to\n"
                "efficiently map to (post-2014) native GPU APIs. WebGPU is not related\n"
                "to WebGL and does not explicitly target OpenGL ES.\n"
                "\n"
                "WebGPU sees physical GPU hardware as GPUAdapters. It provides a\n"
                "connection to an adapter via GPUDevice, which manages resources, and\n"
                "the device's GPUQueues, which execute commands. GPUDevice may have\n"
                "its own memory with high-speed access to the processing units.\n"
                "GPUBuffer and GPUTexture are the physical resources backed by GPU\n"
                "memory. GPUCommandBuffer and GPURenderBundle are containers for\n"
                "user-recorded commands. GPUShaderModule contains shader code. The\n"
                "other resources, such as GPUSampler or GPUBindGroup, configure the\n"
                "way physical resources are used by the GPU.\n"
                "\n"
                "GPUs execute commands encoded in GPUCommandBuffers by feeding data\n"
                "through a pipeline, which is a mix of fixed-function and programmable\n"
                "stages. Programmable stages execute shaders, which are special\n"
                "programs designed to run on GPU hardware. Most of the state of a\n"
                "pipeline is defined by a GPURenderPipeline or a GPUComputePipeline\n"
                "object. The state not included in these pipeline objects is set\n"
                "during encoding with commands, such as beginRenderPass() or\n"
                "setBlendConstant().";
            {
                MsdfTextFormatOpts o{};
                o.setPixelScale = true;
                o.pixelScale = 1.f / 256.f;
                FormatText(ctx, kLargeText, o);
            }
        }

        // ------------------------------------------------------------------
        // Members.
        // ------------------------------------------------------------------

        // Upstream string order: 0..5 = face labels, 6 = title, 7 = large text.
        static constexpr int kTitleIdx = 6;
        static constexpr int kLargeIdx = 7;

        WGPUShaderModule m_cubeVS = nullptr;
        WGPUShaderModule m_cubePS = nullptr;
        WGPUShaderModule m_textVS = nullptr;
        WGPUShaderModule m_textPS = nullptr;

        WGPUBuffer m_cubeVB = nullptr;
        WGPUBuffer m_cubeUBO = nullptr;
        WGPUBindGroupLayout m_cubeBGLayout = nullptr;
        WGPUPipelineLayout m_cubePipelineLayout = nullptr;
        WGPUBindGroup m_cubeBindGroup = nullptr;
        WGPURenderPipeline m_cubePipeline = nullptr;

        WGPUBuffer m_cameraUBO = nullptr;
        WGPUSampler m_sampler = nullptr;
        WGPUBindGroupLayout m_fontBGLayout = nullptr;
        WGPUBindGroupLayout m_textBGLayout = nullptr;
        WGPUPipelineLayout m_textPipelineLayout = nullptr;
        WGPURenderPipeline m_textPipeline = nullptr;

        MsdfFont m_font{};
        std::vector<MsdfText> m_texts;

        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "TextRenderingMsdfTest";
        desc.windowTitle = "PhasmaWebGPU TextRenderingMsdf";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    TextRenderingMsdfSample sample;
    pwgpu::test::SampleApp app(sample, MakeDesc());
    return app.Run(argc, argv);
}
