// WebGPU RHI backend, P2 exit (docs/wiki/superpowers/specs/2026-11-06-webgpu-rhi-backend-design.md): engine passes,
// pipelines, draws, a dispatch and push constants through the RHI, with shaders from the real desktop bake
// (.testpool/tests/web_rhi_draw.py writes them into MEMFS). Checks a back-face-culled draw, a mipmapped texture through
// a combined sampler, a Load from a depth texture, a dispatch whose storage buffer the next draw reads, push constants,
// and an indirect draw with firstInstance 5; then draws 600 frames to the canvas. Prints "[draw] PASS ...",
// "[draw] N frames, M WebGPU errors" and "[draw] done: N failures, M WebGPU errors".
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Sampler.h"
#include "API/Shader.h"
#include "API/Swapchain.h"
#include "API/WebGPU/WebGpuImageImpl.h"
#include "API/WebGPU/WebGpuRhiImpl.h"
#include "Base/EventSystem.h"

#include <SDL.h>
#include <cstdio>
#include <emscripten.h>

using namespace pe;

namespace
{
    constexpr uint32_t kWidth = 96;
    constexpr uint32_t kHeight = 64;
    constexpr uint32_t kCell = 32;
    constexpr uint32_t kFrames = 600;
    constexpr uint32_t kTexSize = 16;
    constexpr uint32_t kMips = 5;
    constexpr uint32_t kFirstInstance = 5;
    constexpr float kDepth = 0.25f;
    constexpr float kLod = 2.0f;
    const vec4 kClear(0.0f, 0.0f, 0.25f, 1.0f);
    const vec4 kCanvasClear(0.0f, 1.0f, 0.0f, 1.0f);
    const vec4 kCullColor(1.0f, 0.5f, 0.0f, 1.0f);
    constexpr uint8_t kMipColors[kMips][4] = {
        {255, 0, 0, 255},
        {0, 255, 0, 255},
        {0, 0, 255, 255},
        {255, 255, 0, 255},
        {0, 255, 255, 255},
    };

    // Matches RhiDraw.hlsl's push block.
    struct Push
    {
        vec4 color{};
        float lod = 0.0f;
        float depth = 0.0f;
        float pad[2] = {};
    };

    // The 96x64 target holds six 32x32 cells, one per check; the last one keeps the pass's clear color.
    struct Cell
    {
        uint32_t x;
        uint32_t y;
    };

    constexpr Cell kCullCell{0, 0};
    constexpr Cell kTexCell{32, 0};
    constexpr Cell kDepthCell{64, 0};
    constexpr Cell kBufCell{0, 32};
    constexpr Cell kInstCell{32, 32};
    constexpr Cell kEmptyCell{64, 32};

    struct Scene
    {
        SDL_Window *window = nullptr;
        uint32_t frame = 0;
        Image *color = nullptr;
        Image *depth = nullptr;
        Image *texture = nullptr;
        Sampler *sampler = nullptr;
        Buffer *colors = nullptr;
        Buffer *drawIds = nullptr;
        Buffer *indirect = nullptr;
        // Shaders belong to the passes; the test never destroys the scene, so shared shaders are not freed twice.
        PassInfo compute;
        PassInfo depthPass;
        PassInfo cull;
        PassInfo tex;
        PassInfo depthLoad;
        PassInfo buf;
        PassInfo inst;
    };

    int s_failures = 0;

    void Check(bool ok, const std::string &what)
    {
        std::printf("[draw] %s %s\n", ok ? "PASS" : "FAIL", what.c_str());
        if (!ok)
            s_failures++;
    }

    void WaitFuture(WGPUFuture future)
    {
        WGPUFutureWaitInfo info{};
        info.future = future;
        wgpuInstanceWaitAny(WebGpuRhiImpl::Get()->m_instance, 1, &info, 10'000'000'000ull);
    }

    template <typename Record>
    void Submit(Record &&record)
    {
        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        record(cmd);
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();
    }

    Shader *Load(const char *entry, PeShaderStageFlags stage)
    {
        return Shader::Create({.sourcePath = "/RhiDraw/RhiDraw.hlsl", .entryPoint = entry, .stage = stage});
    }

    Image *MakeImage(uint32_t width,
                     uint32_t height,
                     ::PeFormat format,
                     PeImageUsageFlags usage,
                     const char *name,
                     uint32_t mips = 1)
    {
        ImageDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.mipLevels = mips;
        desc.format = format;
        desc.usage = usage;
        desc.name = name;
        return Image::Create(desc);
    }

    Buffer *MakeBuffer(size_t size, PeBufferUsageFlags usage, const char *name)
    {
        return Buffer::Create({.size = size, .usage = usage, .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY, .name = name});
    }

    void SetupPass(PassInfo &info, const char *name, Shader *vertex, Shader *fragment, PeCullMode cull)
    {
        info.name = name;
        info.pVertShader = vertex;
        info.pFragShader = fragment;
        info.cullMode = cull;
        info.colorFormats = {PE_FORMAT_R8G8B8A8_UNORM};
        info.depthFormat = PE_FORMAT_UNDEFINED;
        info.depthTestEnable = false;
        info.depthWriteEnable = false;
        info.Update();
    }

    // Each frame in flight has its own set 0; give every copy the same resource.
    template <typename Bind>
    void BindSet0(PassInfo &info, Bind &&bind)
    {
        for (uint32_t frame = 0; frame < RHII.GetSwapchainImageCount(); ++frame)
        {
            Descriptor *set0 = info.GetDescriptors(frame)[0];
            bind(set0);
            set0->Update();
        }
    }

    void CreateResources(Scene &s)
    {
        s.color = MakeImage(kWidth, kHeight, PE_FORMAT_R8G8B8A8_UNORM,
                            PE_IMAGE_USAGE_COLOR_ATTACHMENT | PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_SAMPLED, "draw_color");
        s.color->CreateRTV();
        s.color->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
        s.color->SetClearColor(kClear);

        s.depth = MakeImage(kWidth, kHeight, PE_FORMAT_D32_SFLOAT,
                            PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT | PE_IMAGE_USAGE_SAMPLED, "draw_depth");
        s.depth->CreateRTV();
        s.depth->CreateSRV(PE_IMAGE_VIEW_TYPE_2D);
        s.depth->SetClearColor(vec4(1.0f, 0.0f, 0.0f, 0.0f));

        s.texture = MakeImage(kTexSize, kTexSize, PE_FORMAT_R8G8B8A8_UNORM,
                              PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_TRANSFER_DST, "draw_texture", kMips);
        s.texture->CreateSRV(PE_IMAGE_VIEW_TYPE_2D, -1);
        s.sampler = Sampler::Create(SamplerDesc{}, "draw_sampler");

        s.colors = MakeBuffer(4 * sizeof(vec4), PE_BUFFER_USAGE_STORAGE_BUFFER, "draw_colors");
        s.drawIds = MakeBuffer(8 * sizeof(uint32_t), PE_BUFFER_USAGE_VERTEX_BUFFER, "draw_ids");
        s.indirect = MakeBuffer(sizeof(PeDrawIndirectCommand), PE_BUFFER_USAGE_INDIRECT_BUFFER, "draw_indirect");
    }

    // Every mip a different solid color, so the sampled color names the mip the sampler picked.
    void Upload(Scene &s)
    {
        std::vector<std::vector<uint8_t>> mips(kMips);
        for (uint32_t mip = 0; mip < kMips; ++mip)
        {
            const uint32_t size = kTexSize >> mip;
            for (uint32_t texel = 0; texel < size * size; ++texel)
                mips[mip].insert(mips[mip].end(), kMipColors[mip], kMipColors[mip] + 4);
        }

        uint32_t ids[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        PeDrawIndirectCommand args{};
        args.vertexCount = 6;
        args.instanceCount = 1;
        args.firstInstance = kFirstInstance;
        Submit([&](CommandBuffer *cmd)
               {
                   for (uint32_t mip = 0; mip < kMips; ++mip)
                       cmd->CopyDataToImageStaged(s.texture, mips[mip].data(), mips[mip].size(), 0, 1, mip);

                   cmd->CopyBufferStaged(s.drawIds, ids, sizeof(ids), 0);
                   cmd->CopyBufferStaged(s.indirect, &args, sizeof(args), 0); });
    }

    void CreatePasses(Scene &s)
    {
        Shader *quadVS = Load("QuadVS", PE_SHADER_STAGE_VERTEX);
        Shader *colorPS = Load("ColorPS", PE_SHADER_STAGE_FRAGMENT);

        s.compute.name = "draw_compute";
        s.compute.pCompShader = Load("mainCS", PE_SHADER_STAGE_COMPUTE);
        s.compute.Update();
        BindSet0(s.compute, [&](Descriptor *set)
                 { set->SetBuffer(0, s.colors); });

        s.depthPass.name = "draw_depth";
        s.depthPass.pVertShader = Load("DepthVS", PE_SHADER_STAGE_VERTEX);
        s.depthPass.cullMode = PE_CULL_MODE_NONE;
        s.depthPass.depthFormat = PE_FORMAT_D32_SFLOAT;
        s.depthPass.depthTestEnable = true;
        s.depthPass.depthWriteEnable = true;
        s.depthPass.depthCompareOp = PE_COMPARE_OP_LESS;
        s.depthPass.Update();

        SetupPass(s.cull, "draw_cull", Load("CullVS", PE_SHADER_STAGE_VERTEX), colorPS, PE_CULL_MODE_BACK);
        SetupPass(s.tex, "draw_tex", quadVS, Load("TexPS", PE_SHADER_STAGE_FRAGMENT), PE_CULL_MODE_BACK);
        SetupPass(s.depthLoad, "draw_depth_load", quadVS, Load("DepthLoadPS", PE_SHADER_STAGE_FRAGMENT), PE_CULL_MODE_BACK);
        SetupPass(s.buf, "draw_buffer", quadVS, Load("BufPS", PE_SHADER_STAGE_FRAGMENT), PE_CULL_MODE_BACK);
        SetupPass(s.inst, "draw_instance", Load("InstVS", PE_SHADER_STAGE_VERTEX), colorPS, PE_CULL_MODE_BACK);

        BindSet0(s.tex, [&](Descriptor *set)
                 { set->SetImageView(2, s.texture->GetSRV(), s.sampler); });
        BindSet0(s.depthLoad, [&](Descriptor *set)
                 { set->SetImageView(3, s.depth->GetSRV()); });
        BindSet0(s.buf, [&](Descriptor *set)
                 { set->SetBuffer(1, s.colors); });
    }

    void SendPush(CommandBuffer *cmd, const Push &push)
    {
        cmd->SetConstants(push);
        cmd->PushConstants();
    }

    void DrawCell(CommandBuffer *cmd, PassInfo &pass, Cell cell, const Push &push)
    {
        cmd->SetViewport(static_cast<float>(cell.x), static_cast<float>(cell.y), kCell, kCell, 0.0f, 1.0f);
        cmd->SetScissor(static_cast<int>(cell.x), static_cast<int>(cell.y), kCell, kCell);
        cmd->BindPipeline(pass);
        SendPush(cmd, push);
        cmd->Draw(6, 1, 0, 0);
    }

    // The dispatch writes the push color at half strength; the depth pass writes kDepth over the whole target.
    void RecordComputeAndDepth(CommandBuffer *cmd, Scene &s, const vec4 &computeColor)
    {
        Push push{};
        push.color = computeColor;
        cmd->BindPipeline(s.compute);
        SendPush(cmd, push);
        cmd->Dispatch(1, 1, 1);

        Attachment depth{};
        depth.image = s.depth;
        cmd->BeginPass(1, &depth, "draw_depth");
        cmd->SetViewport(0.0f, 0.0f, kWidth, kHeight, 0.0f, 1.0f);
        cmd->SetScissor(0, 0, kWidth, kHeight);
        cmd->BindPipeline(s.depthPass);
        push.depth = kDepth;
        SendPush(cmd, push);
        cmd->Draw(6, 1, 0, 0);
        cmd->EndPass();
    }

    void RecordOffscreen(CommandBuffer *cmd, Scene &s, const vec4 &computeColor)
    {
        RecordComputeAndDepth(cmd, s, computeColor);

        Attachment color{};
        color.image = s.color;
        cmd->BeginPass(1, &color, "draw_color");

        Push push{};
        push.color = kCullColor;
        push.lod = kLod;
        DrawCell(cmd, s.cull, kCullCell, push);
        DrawCell(cmd, s.tex, kTexCell, push);
        DrawCell(cmd, s.depthLoad, kDepthCell, push);
        DrawCell(cmd, s.buf, kBufCell, push);

        cmd->SetViewport(static_cast<float>(kInstCell.x), static_cast<float>(kInstCell.y), kCell, kCell, 0.0f, 1.0f);
        cmd->SetScissor(static_cast<int>(kInstCell.x), static_cast<int>(kInstCell.y), kCell, kCell);
        cmd->BindPipeline(s.inst);
        SendPush(cmd, push);
        cmd->BindVertexBuffer(s.drawIds, 0, 1, 1); // PE_DRAW_ID sits at binding 1
        cmd->DrawIndirect(s.indirect, 0, 1, sizeof(PeDrawIndirectCommand));
        cmd->EndPass();
    }

    // Reads the color target outside the engine (readback is off on the web) as tightly packed RGBA rows.
    std::vector<uint8_t> ReadColor(Scene &s)
    {
        WebGpuRhiImpl *web = WebGpuRhiImpl::Get();
        constexpr uint32_t kRow = (kWidth * 4 + 255) / 256 * 256; // WebGPU copies need 256-byte rows
        WGPUBufferDescriptor desc{};
        desc.size = static_cast<uint64_t>(kRow) * kHeight;
        desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        WGPUBuffer readback = wgpuDeviceCreateBuffer(web->m_device, &desc);

        WGPUTexelCopyTextureInfo source{};
        source.texture = WebGpuImageImpl::From(s.color)->m_texture;
        WGPUTexelCopyBufferInfo destination{};
        destination.buffer = readback;
        destination.layout.bytesPerRow = kRow;
        destination.layout.rowsPerImage = kHeight;
        const WGPUExtent3D extent{kWidth, kHeight, 1};

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(web->m_device, nullptr);
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(web->m_queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);

        WGPUBufferMapCallbackInfo mapCb{};
        mapCb.mode = WGPUCallbackMode_WaitAnyOnly;
        mapCb.callback = [](WGPUMapAsyncStatus, WGPUStringView, void *, void *) {};
        WaitFuture(wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, desc.size, mapCb));

        const auto *data = static_cast<const uint8_t *>(wgpuBufferGetConstMappedRange(readback, 0, desc.size));
        std::vector<uint8_t> out(static_cast<size_t>(kWidth) * kHeight * 4);
        for (uint32_t row = 0; row < kHeight; ++row)
            std::memcpy(out.data() + row * kWidth * 4, data + row * kRow, kWidth * 4);

        wgpuBufferUnmap(readback);
        wgpuBufferRelease(readback);
        return out;
    }

    std::array<uint8_t, 4> ToBytes(const vec4 &c)
    {
        const auto byte = [](float v)
        {
            return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
        };
        return {byte(c.x), byte(c.y), byte(c.z), byte(c.w)};
    }

    // Checks the pixel at (x, y) of `cell` against `want`, within 2 per channel.
    void CheckPixel(const std::vector<uint8_t> &pixels, Cell cell, uint32_t x, uint32_t y, std::array<uint8_t, 4> want,
                    const std::string &what)
    {
        const uint8_t *p = &pixels[((cell.y + y) * kWidth + cell.x + x) * 4];
        bool near = true;
        for (int c = 0; c < 4; ++c)
            near &= std::abs(static_cast<int>(p[c]) - want[c]) <= 2;

        char got[48];
        std::snprintf(got, sizeof(got), " (got %u,%u,%u,%u)", p[0], p[1], p[2], p[3]);
        Check(near, what + got);
    }

    void CheckFrame(Scene &s, const vec4 &computeColor, const std::string &label)
    {
        Submit([&](CommandBuffer *cmd)
               { RecordOffscreen(cmd, s, computeColor); });
        const std::vector<uint8_t> pixels = ReadColor(s);
        const std::array<uint8_t, 4> mip2{kMipColors[2][0], kMipColors[2][1], kMipColors[2][2], kMipColors[2][3]};
        const vec4 buffered(computeColor.x * 0.5f, computeColor.y * 0.5f, computeColor.z * 0.5f, 1.0f);

        CheckPixel(pixels, kCullCell, 4, 8, ToBytes(kCullColor), label + ": back-face culling keeps the clockwise triangle");
        CheckPixel(pixels, kCullCell, 28, 8, ToBytes(kClear), label + ": back-face culling drops the counter-clockwise one");
        CheckPixel(pixels, kTexCell, 16, 16, mip2, label + ": a combined sampler at LOD 2 reads mip 2");
        CheckPixel(pixels, kDepthCell, 16, 16, ToBytes(vec4(kDepth, 0, 0, 1)), label + ": Load from the depth texture");
        CheckPixel(pixels, kBufCell, 16, 16, ToBytes(buffered), label + ": the draw reads what the dispatch wrote");
        CheckPixel(pixels, kInstCell, 16, 16, {kFirstInstance * 40, 0, 0, 255}, label + ": indirect draw with firstInstance 5");
        CheckPixel(pixels, kEmptyCell, 16, 16, ToBytes(kClear), label + ": the pass clears its target");
    }

    // The color target fills the canvas's left half; the right half keeps the canvas clear color.
    void Frame(Scene &s)
    {
        Swapchain *swapchain = RHII.GetSwapchain();
        swapchain->WaitForNextFrame();
        Image *canvas = swapchain->GetImage(swapchain->AquireNextImage(nullptr));
        canvas->SetClearColor(kCanvasClear);

        ImageBlit region{};
        region.srcOffsets[1] = {static_cast<int32_t>(kWidth), static_cast<int32_t>(kHeight), 1};
        const int32_t halfWidth = static_cast<int32_t>(canvas->GetWidth() / 2);
        region.dstOffsets[1] = {halfWidth, static_cast<int32_t>(canvas->GetHeight()), 1};

        const float phase = static_cast<float>(s.frame % 60) / 60.0f;
        Submit([&](CommandBuffer *cmd)
               {
                   RecordOffscreen(cmd, s, vec4(phase, 0.5f, 1.0f - phase, 1.0f));
                   cmd->ClearColors({canvas});
                   cmd->BlitImage(s.color, canvas, region, PE_FILTER_NEAREST); });
        swapchain->Present();
        RHII.NextFrame();
    }

    void Finish(Scene *s)
    {
        // Read the canvas inside the frame that drew it, as RhiSwapchain does.
        // clang-format off
        EM_ASM({
            const c = Module.canvas;
            const copy = document.createElement("canvas");
            copy.width = c.width;
            copy.height = c.height;
            const g = copy.getContext("2d");
            g.drawImage(c, 0, 0);
            const at = (fx, fy) => g.getImageData(Math.floor(c.width * fx), Math.floor(c.height * fy), 1, 1).data.join(",");
            out(`[probe] left ${at(0.25, 0.75)} right ${at(0.75, 0.5)}`);
        });
        // clang-format on

        const uint32_t errors = WebGpuRhiImpl::Get()->m_errorCount;
        std::printf("[draw] %u frames, %u WebGPU errors\n", s->frame, errors);
        std::printf("[draw] done: %d failures, %u WebGPU errors\n", s_failures + (errors ? 1 : 0), errors);
    }
} // namespace

int main()
{
    try
    {
        SDL_Init(SDL_INIT_VIDEO);
        SDL_Window *window = SDL_CreateWindow("RhiDraw", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 360, 0);
        EventSystem::Init(); // CommandBuffer::Wait dispatches AfterCommandWait
        RHII.Init(window, PE_GRAPHICS_API_WEBGPU);
        RHII.InitSwapchain();

        auto *s = new Scene();
        s->window = window;
        CreateResources(*s);
        Upload(*s);
        CreatePasses(*s);

        // Two frames with different push data: each submit must see its own ring slots.
        CheckFrame(*s, vec4(0.8f, 0.4f, 0.2f, 1.0f), "frame A");
        RHII.NextFrame();
        CheckFrame(*s, vec4(0.2f, 0.6f, 1.0f, 1.0f), "frame B");
        RHII.NextFrame();

        emscripten_set_main_loop_arg(
            [](void *arg)
            {
                auto *scene = static_cast<Scene *>(arg);
                try
                {
                    Frame(*scene);
                }
                catch (const std::exception &e)
                {
                    std::printf("[draw] FAIL exception at frame %u: %s\n[draw] done: aborted\n", scene->frame, e.what());
                    emscripten_cancel_main_loop();
                    return;
                }

                if (++scene->frame < kFrames)
                    return;

                emscripten_cancel_main_loop();
                Finish(scene);
            },
            s,
            0,
            false);
    }
    catch (const std::exception &e)
    {
        std::printf("[draw] FAIL exception: %s\n[draw] done: aborted\n", e.what());
    }

    return 0;
}
