#include "../Common/SampleApp.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"


namespace
{
    // Upstream canvas is a literal 200x200 low-res target; we render at whatever
    // surface size SDL gives us and scale the 4x4 grid proportionally. The grid
    // math — 4 cells of stride=canvas/4, cell size=stride-gap — matches upstream.
    constexpr uint32_t kInitialSize = 800;

    constexpr uint32_t kMatrixCount = 15;       // 4*4 grid minus bottom-right preview cell
    constexpr uint32_t kConfigBufferSize = 128; // upstream uses 128 (64 vp + 8 off + 4 flange + 4 hf + pad)
    constexpr uint32_t kTextureMipLevels = 4;
    constexpr uint32_t kTextureBaseSize = 16;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kCameraDist = 3.0f;

    // Upstream @override constants inlined in the HLSL, repeated here only for the
    // grid-cell dimensions the C++ side needs for setViewport.
    constexpr float kUpstreamCanvasSize = 200.0f;
    constexpr float kUpstreamViewportStride = 50.0f; // floor(200/4)
    constexpr float kUpstreamViewportSize = 48.0f;   // stride - 2 (1px gap each side)

    class SamplerParametersSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_squareVS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "texturedSquare.vert.hlsl").c_str(),
                "sampler_params_square_vs");
            m_squarePS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "texturedSquare.pixel.hlsl").c_str(),
                "sampler_params_square_ps");
            m_showVS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "showTexture.vert.hlsl").c_str(),
                "sampler_params_show_vs");
            m_showPS = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device,
                (ctx.shaderDir + "showTexture.pixel.hlsl").c_str(),
                "sampler_params_show_ps");
            if (!m_squareVS || !m_squarePS || !m_showVS || !m_showPS)
                return false;

            if (!CreateCheckerboardTexture(ctx))
                return false;

            if (!CreateSampler(ctx))
                return false;

            if (!CreateBuffers(ctx))
                return false;

            UploadViewProjection(ctx);
            UploadMatrices(ctx);

            if (!CreateSquarePipeline(ctx))
                return false;
            if (!CreateShowPipeline(ctx))
                return false;

            if (!CreateBindGroups(ctx))
                return false;

            return true;
        }

        void Resize(pwgpu::test::SampleContext &, uint32_t, uint32_t) override {}

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            const float t = static_cast<float>(ctx.timing.elapsedSeconds) * 0.5f;
            const float animAmp = 0.1f;
            struct
            {
                float ox, oy;
                float flangeSize;
                float highlightFlange;
            } tail{};
            tail.ox = glm::cos(t) * animAmp;
            tail.oy = glm::sin(t) * animAmp;
            tail.flangeSize = 0.5f; // (2^1 - 1) / 2
            tail.highlightFlange = 0.0f;
            wgpuQueueWriteBuffer(ctx.queue, m_configBuffer, 64, &tail, sizeof(tail));
        }

        bool Execute(pwgpu::test::SampleContext &ctx, pwgpu::test::SampleFrame &frame) override
        {
            WGPURenderPassColorAttachment colorAttachment{};
            colorAttachment.view = frame.surfaceView;
            colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = {0.2, 0.2, 0.2, 1.0};

            WGPURenderPassDescriptor passDesc{};
            passDesc.label = {"sampler_params_pass", WGPU_STRLEN};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttachment;

            WGPURenderPassEncoder pass =
                wgpuCommandEncoderBeginRenderPass(frame.encoder, &passDesc);

            // Map upstream's fixed 200x200 grid onto the actual surface size.
            const float scale = static_cast<float>(ctx.width) / kUpstreamCanvasSize;
            const float stride = kUpstreamViewportStride * scale;
            const float vpSize = kUpstreamViewportSize * scale;

            // 15 textured-square draws across cells 0..14 of the 4x4 grid.
            wgpuRenderPassEncoderSetPipeline(pass, m_squarePipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, m_squareBindGroup, 0, nullptr);
            for (uint32_t i = 0; i < kMatrixCount; ++i)
            {
                const float vpX = stride * static_cast<float>(i % 4) + 1.0f * scale;
                const float vpY = stride * static_cast<float>(i / 4) + 1.0f * scale;
                wgpuRenderPassEncoderSetViewport(pass, vpX, vpY, vpSize, vpSize, 0.0f, 1.0f);
                wgpuRenderPassEncoderDraw(pass, 6, 1, 0, i);
            }

            // Bottom-right cell: 4 "raw mip" previews via textureLoad.
            wgpuRenderPassEncoderSetPipeline(pass, m_showPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, m_showBindGroup, 0, nullptr);
            const float lastVp = 3.0f * stride + 1.0f * scale;
            const float s32 = 32.0f * scale;
            const float s16 = 16.0f * scale;
            const float s8 = 8.0f * scale;
            const float s4 = 4.0f * scale;
            const float s24 = 24.0f * scale;

            wgpuRenderPassEncoderSetViewport(pass, lastVp, lastVp, s32, s32, 0.0f, 1.0f);
            wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
            wgpuRenderPassEncoderSetViewport(pass, lastVp + s32, lastVp, s16, s16, 0.0f, 1.0f);
            wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 1);
            wgpuRenderPassEncoderSetViewport(pass, lastVp + s32, lastVp + s16, s8, s8, 0.0f, 1.0f);
            wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 2);
            wgpuRenderPassEncoderSetViewport(pass, lastVp + s32, lastVp + s24, s4, s4, 0.0f, 1.0f);
            wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 3);

            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            auto releaseBG = [](WGPUBindGroup &bg)
            { if (bg) { wgpuBindGroupRelease(bg); bg = nullptr; } };
            auto releaseBGL = [](WGPUBindGroupLayout &l)
            { if (l) { wgpuBindGroupLayoutRelease(l); l = nullptr; } };
            auto releasePL = [](WGPUPipelineLayout &l)
            { if (l) { wgpuPipelineLayoutRelease(l); l = nullptr; } };
            auto releasePipe = [](WGPURenderPipeline &p)
            { if (p) { wgpuRenderPipelineRelease(p); p = nullptr; } };
            auto releaseBuf = [](WGPUBuffer &b)
            { if (b) { wgpuBufferRelease(b); b = nullptr; } };
            auto releaseTexView = [](WGPUTextureView &v)
            { if (v) { wgpuTextureViewRelease(v); v = nullptr; } };
            auto releaseTex = [](WGPUTexture &t)
            { if (t) { wgpuTextureRelease(t); t = nullptr; } };
            auto releaseSampler = [](WGPUSampler &s)
            { if (s) { wgpuSamplerRelease(s); s = nullptr; } };
            auto releaseModule = [](WGPUShaderModule &m)
            { if (m) { wgpuShaderModuleRelease(m); m = nullptr; } };

            releaseBG(m_squareBindGroup);
            releaseBG(m_showBindGroup);
            releaseBGL(m_squareBGL);
            releaseBGL(m_showBGL);
            releasePL(m_squarePL);
            releasePL(m_showPL);
            releasePipe(m_squarePipeline);
            releasePipe(m_showPipeline);
            releaseBuf(m_configBuffer);
            releaseBuf(m_matrixBuffer);
            releaseTexView(m_textureView);
            releaseTex(m_texture);
            releaseSampler(m_sampler);
            releaseModule(m_squareVS);
            releaseModule(m_squarePS);
            releaseModule(m_showVS);
            releaseModule(m_showPS);
        }

    private:
        bool CreateCheckerboardTexture(pwgpu::test::SampleContext &ctx)
        {
            WGPUTextureDescriptor texDesc{};
            texDesc.label = {"sampler_params_checker", WGPU_STRLEN};
            texDesc.dimension = WGPUTextureDimension_2D;
            texDesc.size = {kTextureBaseSize, kTextureBaseSize, 1};
            texDesc.mipLevelCount = kTextureMipLevels;
            texDesc.sampleCount = 1;
            texDesc.format = WGPUTextureFormat_RGBA8Unorm;
            texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            m_texture = wgpuDeviceCreateTexture(ctx.device, &texDesc);
            if (!m_texture)
                return false;

            static const uint8_t kColorForLevel[kTextureMipLevels][4] = {
                {255, 255, 255, 255},
                {30, 136, 229, 255},
                {255, 193, 7, 255},
                {216, 27, 96, 255},
            };

            // Generate one checkered mip at a time and upload with padded bytesPerRow.
            for (uint32_t mip = 0; mip < kTextureMipLevels; ++mip)
            {
                const uint32_t size = 1u << (kTextureMipLevels - mip); // 16,8,4,2
                std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4, 0);
                for (uint32_t y = 0; y < size; ++y)
                {
                    for (uint32_t x = 0; x < size; ++x)
                    {
                        uint8_t *p = pixels.data() + (static_cast<size_t>(y) * size + x) * 4;
                        if ((x + y) & 1u)
                        {
                            p[0] = kColorForLevel[mip][0];
                            p[1] = kColorForLevel[mip][1];
                            p[2] = kColorForLevel[mip][2];
                            p[3] = kColorForLevel[mip][3];
                        }
                        else
                        {
                            p[0] = 0;
                            p[1] = 0;
                            p[2] = 0;
                            p[3] = 255;
                        }
                    }
                }
                pwgpu::test::UploadRGBA8Texture(ctx.queue,
                                                m_texture,
                                                pixels.data(),
                                                size,
                                                size,
                                                mip,
                                                0);
            }

            // View covering all mip levels (both pipelines read the full chain).
            WGPUTextureViewDescriptor viewDesc{};
            viewDesc.label = {"sampler_params_checker_view", WGPU_STRLEN};
            viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.baseMipLevel = 0;
            viewDesc.mipLevelCount = kTextureMipLevels;
            viewDesc.baseArrayLayer = 0;
            viewDesc.arrayLayerCount = 1;
            viewDesc.aspect = WGPUTextureAspect_All;
            m_textureView = wgpuTextureCreateView(m_texture, &viewDesc);
            return m_textureView != nullptr;
        }

        bool CreateSampler(pwgpu::test::SampleContext &ctx)
        {
            // Upstream's initial preset: all linear, clamp-to-edge, lod [0,4], aniso=1.
            WGPUSamplerDescriptor desc{};
            desc.label = {"sampler_params_initial", WGPU_STRLEN};
            desc.addressModeU = WGPUAddressMode_ClampToEdge;
            desc.addressModeV = WGPUAddressMode_ClampToEdge;
            desc.addressModeW = WGPUAddressMode_ClampToEdge;
            desc.magFilter = WGPUFilterMode_Linear;
            desc.minFilter = WGPUFilterMode_Linear;
            desc.mipmapFilter = WGPUMipmapFilterMode_Linear;
            desc.lodMinClamp = 0.0f;
            desc.lodMaxClamp = 4.0f;
            desc.compare = WGPUCompareFunction_Undefined;
            desc.maxAnisotropy = 1;
            m_sampler = wgpuDeviceCreateSampler(ctx.device, &desc);
            return m_sampler != nullptr;
        }

        bool CreateBuffers(pwgpu::test::SampleContext &ctx)
        {
            m_configBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       kConfigBufferSize,
                                                       WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                                                       "sampler_params_config");
            m_matrixBuffer = pwgpu::test::CreateBuffer(ctx.device,
                                                       static_cast<uint64_t>(kMatrixCount) * 64,
                                                       WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
                                                       "sampler_params_matrices");
            return m_configBuffer && m_matrixBuffer;
        }

        void UploadViewProjection(pwgpu::test::SampleContext &ctx)
        {
            const float fov = 2.0f * glm::atan(1.0f / kCameraDist);
            glm::mat4 proj = glm::perspectiveRH_ZO(fov, 1.0f, 0.1f, 100.0f);
            glm::mat4 viewProj = glm::translate(proj, glm::vec3(0.0f, 0.0f, -kCameraDist));
            viewProj[1][1] *= -1.0f; // Vulkan Y-flip
            wgpuQueueWriteBuffer(ctx.queue, m_configBuffer, 0, &viewProj[0][0], 64);
        }

        void UploadMatrices(pwgpu::test::SampleContext &ctx)
        {
            // Row k uses scale sk; 4th row has only 3 entries (the 4th cell is the mip preview).
            const float scales[4] = {2.0f, 1.0f, 0.9f, 0.3f};
            // Upstream's 4 per-row rotations (wgpu-matrix: rotationZ/identity/rotationX/rotationX).
            auto rotZ = [](float a)
            { return glm::rotate(glm::mat4(1.0f), a, glm::vec3(0.0f, 0.0f, 1.0f)); };
            auto rotX = [](float a)
            { return glm::rotate(glm::mat4(1.0f), a, glm::vec3(1.0f, 0.0f, 0.0f)); };
            const glm::mat4 rotations[4] = {
                rotZ(kPi / 16.0f),
                glm::mat4(1.0f),
                rotX(-kPi * 0.3f),
                rotX(-kPi * 0.42f),
            };

            glm::mat4 matrices[kMatrixCount];
            uint32_t idx = 0;
            for (uint32_t row = 0; row < 4; ++row)
            {
                const uint32_t cols = (row == 3) ? 3 : 4;
                const glm::mat4 s = glm::scale(glm::mat4(1.0f), glm::vec3(scales[row], scales[row], 1.0f));
                for (uint32_t col = 0; col < cols; ++col)
                    matrices[idx++] = rotations[col] * s; // wgpu-matrix: mat4.scale(rot, ...) = rot * scaleMat
            }

            wgpuQueueWriteBuffer(ctx.queue,
                                 m_matrixBuffer,
                                 0,
                                 &matrices[0][0][0],
                                 sizeof(matrices));
        }

        bool CreateSquarePipeline(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry entries[4] = {};
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            entries[0].buffer.type = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = kConfigBufferSize;

            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Vertex;
            entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            entries[1].buffer.minBindingSize = static_cast<uint64_t>(kMatrixCount) * 64;

            entries[2].binding = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

            entries[3].binding = 3;
            entries[3].visibility = WGPUShaderStage_Fragment;
            entries[3].texture.sampleType = WGPUTextureSampleType_Float;
            entries[3].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[3].texture.multisampled = WGPUOptionalBool_False;

            WGPUBindGroupLayoutDescriptor bglDesc{};
            bglDesc.label = {"sampler_params_square_bgl", WGPU_STRLEN};
            bglDesc.entryCount = 4;
            bglDesc.entries = entries;
            m_squareBGL = wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);
            if (!m_squareBGL)
                return false;

            WGPUPipelineLayoutDescriptor plDesc{};
            plDesc.label = {"sampler_params_square_pl", WGPU_STRLEN};
            plDesc.bindGroupLayoutCount = 1;
            plDesc.bindGroupLayouts = &m_squareBGL;
            m_squarePL = wgpuDeviceCreatePipelineLayout(ctx.device, &plDesc);
            if (!m_squarePL)
                return false;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = ctx.surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = m_squarePS;
            fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            WGPURenderPipelineDescriptor pipeDesc{};
            pipeDesc.label = {"sampler_params_square_pipe", WGPU_STRLEN};
            pipeDesc.layout = m_squarePL;
            pipeDesc.vertex.module = m_squareVS;
            pipeDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipeDesc.vertex.bufferCount = 0;
            pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipeDesc.primitive.cullMode = WGPUCullMode_None;
            pipeDesc.primitive.frontFace = WGPUFrontFace_CCW;
            pipeDesc.multisample.count = 1;
            pipeDesc.multisample.mask = 0xFFFFFFFF;
            pipeDesc.fragment = &fragmentState;
            m_squarePipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipeDesc);
            return m_squarePipeline != nullptr;
        }

        bool CreateShowPipeline(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry entry{};
            entry.binding = 0;
            entry.visibility = WGPUShaderStage_Fragment;
            entry.texture.sampleType = WGPUTextureSampleType_Float;
            entry.texture.viewDimension = WGPUTextureViewDimension_2D;
            entry.texture.multisampled = WGPUOptionalBool_False;

            WGPUBindGroupLayoutDescriptor bglDesc{};
            bglDesc.label = {"sampler_params_show_bgl", WGPU_STRLEN};
            bglDesc.entryCount = 1;
            bglDesc.entries = &entry;
            m_showBGL = wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);
            if (!m_showBGL)
                return false;

            WGPUPipelineLayoutDescriptor plDesc{};
            plDesc.label = {"sampler_params_show_pl", WGPU_STRLEN};
            plDesc.bindGroupLayoutCount = 1;
            plDesc.bindGroupLayouts = &m_showBGL;
            m_showPL = wgpuDeviceCreatePipelineLayout(ctx.device, &plDesc);
            if (!m_showPL)
                return false;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = ctx.surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState fragmentState{};
            fragmentState.module = m_showPS;
            fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
            fragmentState.targetCount = 1;
            fragmentState.targets = &colorTarget;

            WGPURenderPipelineDescriptor pipeDesc{};
            pipeDesc.label = {"sampler_params_show_pipe", WGPU_STRLEN};
            pipeDesc.layout = m_showPL;
            pipeDesc.vertex.module = m_showVS;
            pipeDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
            pipeDesc.vertex.bufferCount = 0;
            pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            pipeDesc.primitive.cullMode = WGPUCullMode_None;
            pipeDesc.primitive.frontFace = WGPUFrontFace_CCW;
            pipeDesc.multisample.count = 1;
            pipeDesc.multisample.mask = 0xFFFFFFFF;
            pipeDesc.fragment = &fragmentState;
            m_showPipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &pipeDesc);
            return m_showPipeline != nullptr;
        }

        bool CreateBindGroups(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupEntry squareEntries[4] = {};
            squareEntries[0].binding = 0;
            squareEntries[0].buffer = m_configBuffer;
            squareEntries[0].size = kConfigBufferSize;
            squareEntries[1].binding = 1;
            squareEntries[1].buffer = m_matrixBuffer;
            squareEntries[1].size = static_cast<uint64_t>(kMatrixCount) * 64;
            squareEntries[2].binding = 2;
            squareEntries[2].sampler = m_sampler;
            squareEntries[3].binding = 3;
            squareEntries[3].textureView = m_textureView;

            WGPUBindGroupDescriptor squareBG{};
            squareBG.label = {"sampler_params_square_bg", WGPU_STRLEN};
            squareBG.layout = m_squareBGL;
            squareBG.entryCount = 4;
            squareBG.entries = squareEntries;
            m_squareBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &squareBG);
            if (!m_squareBindGroup)
                return false;

            WGPUBindGroupEntry showEntry{};
            showEntry.binding = 0;
            showEntry.textureView = m_textureView;

            WGPUBindGroupDescriptor showBG{};
            showBG.label = {"sampler_params_show_bg", WGPU_STRLEN};
            showBG.layout = m_showBGL;
            showBG.entryCount = 1;
            showBG.entries = &showEntry;
            m_showBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &showBG);
            return m_showBindGroup != nullptr;
        }

        WGPUShaderModule m_squareVS = nullptr;
        WGPUShaderModule m_squarePS = nullptr;
        WGPUShaderModule m_showVS = nullptr;
        WGPUShaderModule m_showPS = nullptr;
        WGPUTexture m_texture = nullptr;
        WGPUTextureView m_textureView = nullptr;
        WGPUSampler m_sampler = nullptr;
        WGPUBuffer m_configBuffer = nullptr;
        WGPUBuffer m_matrixBuffer = nullptr;
        WGPUBindGroupLayout m_squareBGL = nullptr;
        WGPUBindGroupLayout m_showBGL = nullptr;
        WGPUPipelineLayout m_squarePL = nullptr;
        WGPUPipelineLayout m_showPL = nullptr;
        WGPURenderPipeline m_squarePipeline = nullptr;
        WGPURenderPipeline m_showPipeline = nullptr;
        WGPUBindGroup m_squareBindGroup = nullptr;
        WGPUBindGroup m_showBindGroup = nullptr;
    };

    pwgpu::test::SampleAppDesc MakeSamplerParametersDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "SamplerParametersTest";
        desc.windowTitle = "Phasma WebGPU Sampler Parameters";
        desc.initialWidth = kInitialSize;
        desc.initialHeight = kInitialSize;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);

    SamplerParametersSample sample;
    pwgpu::test::SampleApp app(sample, MakeSamplerParametersDesc());
    return app.Run(argc, argv);
}
