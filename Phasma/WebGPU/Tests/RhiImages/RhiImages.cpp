// WebGPU RHI backend, P2: images, mips, blits, samplers and bind groups (docs/wiki/superpowers/specs/2026-11-06-webgpu-rhi-backend-design.md,
// decision 5) against the browser's WebGPU through the engine RHI. Prints "[images] PASS ..." / "[images] FAIL ..." and
// "[images] done: N failures, M WebGPU errors". Build: the wasm-player preset, target RhiImages (Emscripten only).
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Sampler.h"
#include "API/WebGPU/WebGpuBufferImpl.h"
#include "API/WebGPU/WebGpuImageImpl.h"
#include "API/WebGPU/WebGpuRhiImpl.h"
#include "Base/EventSystem.h"

#include <cstdio>

using namespace pe;

namespace
{
    int s_failures = 0;

    void Check(bool ok, const char *what)
    {
        std::printf("[images] %s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok)
            s_failures++;
    }

    void WaitFuture(WGPUFuture future)
    {
        WGPUFutureWaitInfo info{};
        info.future = future;
        wgpuInstanceWaitAny(WebGpuRhiImpl::Get()->m_instance, 1, &info, 10'000'000'000ull);
    }

    // Validation errors arrive through the device's uncaptured-error callback; wait for the queue so they are in.
    uint32_t ErrorsSoFar()
    {
        WGPUQueueWorkDoneCallbackInfo doneCb{};
        doneCb.mode = WGPUCallbackMode_WaitAnyOnly;
        doneCb.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void *, void *) {};
        WaitFuture(wgpuQueueOnSubmittedWorkDone(WebGpuRhiImpl::Get()->m_queue, doneCb));
        return WebGpuRhiImpl::Get()->m_errorCount;
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

    // Reads one mip of one layer outside the engine (readback is off on the web) as tightly packed block rows.
    std::vector<uint8_t> ReadBack(Image *image, uint32_t mip, uint32_t layer, uint32_t blockWidth, uint32_t blockBytes)
    {
        WebGpuRhiImpl *web = WebGpuRhiImpl::Get();
        const uint32_t width = std::max(1u, image->GetWidth() >> mip);
        const uint32_t height = std::max(1u, image->GetHeight() >> mip);
        const uint32_t blocksWide = (width + blockWidth - 1) / blockWidth;
        const uint32_t blocksHigh = (height + blockWidth - 1) / blockWidth;
        const uint32_t tightRow = blocksWide * blockBytes;
        const uint32_t alignedRow = (tightRow + 255) / 256 * 256;

        WGPUBufferDescriptor desc{};
        desc.size = static_cast<uint64_t>(alignedRow) * blocksHigh;
        desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        WGPUBuffer readback = wgpuDeviceCreateBuffer(web->m_device, &desc);

        WGPUTexelCopyTextureInfo source{};
        source.texture = WebGpuImageImpl::From(image)->m_texture;
        source.mipLevel = mip;
        source.origin = {0, 0, layer};
        WGPUTexelCopyBufferInfo destination{};
        destination.buffer = readback;
        destination.layout.bytesPerRow = alignedRow;
        destination.layout.rowsPerImage = blocksHigh;
        const WGPUExtent3D extent{blocksWide * blockWidth, blocksHigh * blockWidth, 1};

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
        std::vector<uint8_t> out(static_cast<size_t>(tightRow) * blocksHigh);
        for (uint32_t row = 0; row < blocksHigh; ++row)
            std::memcpy(out.data() + row * tightRow, data + row * alignedRow, tightRow);

        wgpuBufferUnmap(readback);
        wgpuBufferDestroy(readback);
        wgpuBufferRelease(readback);
        return out;
    }

    std::vector<uint8_t> Pattern(size_t size, uint8_t seed)
    {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; ++i)
            data[i] = static_cast<uint8_t>(i * 7 + seed);

        return data;
    }

    Image *MakeImage(uint32_t width, uint32_t height, ::PeFormat format, PeImageUsageFlags usage, const char *name,
                     uint32_t mips = 1, uint32_t layers = 1)
    {
        ImageDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.mipLevels = mips;
        desc.arrayLayers = layers;
        desc.format = format;
        desc.usage = usage;
        desc.name = name;
        return Image::Create(desc);
    }

    constexpr PeImageUsageFlags kCopyUsage = PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_TRANSFER_SRC | PE_IMAGE_USAGE_TRANSFER_DST;

    void TestUploads()
    {
        // 30 texels of rgba8 are 120-byte rows, so the upload must repack them to 256-byte rows.
        Image *plain = MakeImage(30, 20, PE_FORMAT_R8G8B8A8_UNORM, kCopyUsage, "upload_plain");
        const std::vector<uint8_t> plainData = Pattern(30 * 20 * 4, 1);
        Submit([&](CommandBuffer *cmd)
               { cmd->CopyDataToImageStaged(plain, const_cast<uint8_t *>(plainData.data()), plainData.size(), 0, 1, 0); });
        Check(ReadBack(plain, 0, 0, 1, 4) == plainData, "upload: 30x20 rgba8 rows repacked to 256 bytes");

        // Mip 1 of a 2-layer 16x16 array: two 8x8 layers in one call.
        Image *array = MakeImage(16, 16, PE_FORMAT_R8G8B8A8_UNORM, kCopyUsage, "upload_array", 2, 2);
        const std::vector<uint8_t> arrayData = Pattern(8 * 8 * 4 * 2, 9);
        Submit([&](CommandBuffer *cmd)
               { cmd->CopyDataToImageStaged(array, const_cast<uint8_t *>(arrayData.data()), arrayData.size(), 0, 2, 1); });
        const std::vector<uint8_t> layer1 = ReadBack(array, 1, 1, 1, 4);
        Check(std::equal(layer1.begin(), layer1.end(), arrayData.begin() + 8 * 8 * 4), "upload: array layer 1 of mip 1");

        if (WebGpuRhiImpl::Get()->m_textureCompressionBC)
        {
            Image *bc = MakeImage(8, 8, PE_FORMAT_BC1_RGBA_UNORM, kCopyUsage, "upload_bc1");
            const std::vector<uint8_t> bcData = Pattern(2 * 2 * 8, 3);
            Submit([&](CommandBuffer *cmd)
                   { cmd->CopyDataToImageStaged(bc, const_cast<uint8_t *>(bcData.data()), bcData.size(), 0, 1, 0); });
            Check(ReadBack(bc, 0, 0, 4, 8) == bcData, "upload: BC1 blocks");
            Image::Destroy(bc);
        }

        // CopyImage copies every mip and layer.
        Image *copy = MakeImage(30, 20, PE_FORMAT_R8G8B8A8_UNORM, kCopyUsage, "upload_copy");
        Submit([&](CommandBuffer *cmd)
               { cmd->CopyImage(plain, copy); });
        Check(ReadBack(copy, 0, 0, 1, 4) == plainData, "CopyImage: the copy matches its source");

        Image::Destroy(plain);
        Image::Destroy(array);
        Image::Destroy(copy);
    }

    void TestRenderTarget()
    {
        Image *target = MakeImage(16, 16, PE_FORMAT_R8G8B8A8_UNORM, kCopyUsage | PE_IMAGE_USAGE_COLOR_ATTACHMENT, "rtv_target");
        target->CreateRTV();
        target->SetClearColor(vec4(0.0f, 1.0f, 0.0f, 1.0f));
        Submit([&](CommandBuffer *cmd)
               { cmd->ClearColors({target}); });

        const std::vector<uint8_t> pixels = ReadBack(target, 0, 0, 1, 4);
        Check(pixels[0] == 0 && pixels[1] == 255 && pixels[2] == 0 && pixels[3] == 255, "RTV: ClearColors on an owned image");
        Image::Destroy(target);
    }

    bool Near(uint8_t value, uint8_t want, uint8_t tolerance)
    {
        return std::abs(static_cast<int>(value) - static_cast<int>(want)) <= tolerance;
    }

    // A 0/255 checkerboard averages to about 128 in every mip of the linear blit chain.
    void TestMipChain()
    {
        Image *image = MakeImage(16, 16, PE_FORMAT_R8G8B8A8_UNORM, kCopyUsage, "mip_chain", 5);
        std::vector<uint8_t> checker(16 * 16 * 4);
        for (uint32_t y = 0; y < 16; ++y)
        {
            for (uint32_t x = 0; x < 16; ++x)
            {
                const uint8_t v = ((x + y) & 1) ? 255 : 0;
                uint8_t *p = &checker[(y * 16 + x) * 4];
                p[0] = v;
                p[1] = v;
                p[2] = v;
                p[3] = 255;
            }
        }

        Submit([&](CommandBuffer *cmd)
               {
                   cmd->CopyDataToImageStaged(image, checker.data(), checker.size(), 0, 1, 0);
                   cmd->GenerateMipMaps(image); });

        bool grey = true;
        for (uint32_t mip = 1; mip < 5; ++mip)
        {
            const std::vector<uint8_t> pixels = ReadBack(image, mip, 0, 1, 4);
            for (size_t i = 0; i < pixels.size(); i += 4)
                grey = grey && Near(pixels[i], 128, 2) && pixels[i + 3] == 255;
        }

        const std::vector<uint8_t> top = ReadBack(image, 0, 0, 1, 4);
        Check(top == checker, "GenerateMipMaps: mip 0 is untouched");
        Check(grey, "GenerateMipMaps: mips 1-4 average the 0/255 checkerboard to 128");

        // A second call is a no-op: the image remembers its mips are generated, so a white mip 0 does not reach mip 4.
        const std::vector<uint8_t> white(checker.size(), 255);
        Submit([&](CommandBuffer *cmd)
               {
                   cmd->CopyDataToImageStaged(image, const_cast<uint8_t *>(white.data()), white.size(), 0, 1, 0);
                   cmd->GenerateMipMaps(image); });
        Check(Near(ReadBack(image, 4, 0, 1, 4)[0], 128, 2), "GenerateMipMaps: a second call records nothing");
        Image::Destroy(image);
    }

    // Nearest blits copy texel for texel; reversed destination offsets flip that axis.
    void TestBlit()
    {
        Image *src = MakeImage(4, 4, PE_FORMAT_R8G8B8A8_UNORM, kCopyUsage, "blit_src");
        Image *straight = MakeImage(4, 4, PE_FORMAT_R8G8B8A8_UNORM, kCopyUsage, "blit_straight");
        Image *flipped = MakeImage(4, 4, PE_FORMAT_R8G8B8A8_UNORM, kCopyUsage, "blit_flipped");
        const std::vector<uint8_t> data = Pattern(4 * 4 * 4, 5);

        ImageBlit region{};
        region.srcOffsets[1] = {4, 4, 1};
        region.dstOffsets[1] = {4, 4, 1};
        ImageBlit flip = region;
        flip.dstOffsets[0] = {0, 4, 0};
        flip.dstOffsets[1] = {4, 0, 1};

        Submit([&](CommandBuffer *cmd)
               {
                   cmd->CopyDataToImageStaged(src, const_cast<uint8_t *>(data.data()), data.size(), 0, 1, 0);
                   cmd->BlitImage(src, straight, region, PE_FILTER_NEAREST);
                   cmd->BlitImage(src, flipped, flip, PE_FILTER_NEAREST); });

        Check(ReadBack(straight, 0, 0, 1, 4) == data, "BlitImage: a nearest 4x4 blit copies every texel");

        std::vector<uint8_t> rowsFlipped(data.size());
        for (uint32_t y = 0; y < 4; ++y)
            std::memcpy(&rowsFlipped[y * 16], &data[(3 - y) * 16], 16);

        Check(ReadBack(flipped, 0, 0, 1, 4) == rowsFlipped, "BlitImage: reversed destination Y offsets flip the rows");
        Image::Destroy(src);
        Image::Destroy(straight);
        Image::Destroy(flipped);
    }

    void TestSamplers()
    {
        SamplerDesc nearestAniso{};
        nearestAniso.magFilter = PE_FILTER_NEAREST; // anisotropy must fall back to 1
        SamplerDesc border{};
        border.addressModeU = PE_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        border.minLod = -1000.0f;
        SamplerDesc compare{};
        compare.compareEnable = true;
        compare.compareOp = PE_COMPARE_OP_LESS_OR_EQUAL;

        Sampler *a = Sampler::Create(nearestAniso, "sampler_nearest_aniso");
        Sampler *b = Sampler::Create(border, "sampler_border");
        Sampler *c = Sampler::Create(compare, "sampler_compare");
        Check(a && b && c, "samplers: anisotropy with nearest, border and comparison samplers create");
        Sampler::Destroy(a);
        Sampler::Destroy(b);
        Sampler::Destroy(c);
    }

    DescriptorBindingInfo Binding(uint32_t binding, PeBindingType type, PeImageLayout layout = PE_IMAGE_LAYOUT_UNDEFINED)
    {
        DescriptorBindingInfo info{};
        info.binding = binding;
        info.type = type;
        info.imageLayout = layout;
        info.name = "b" + std::to_string(binding);
        return info;
    }

    template <typename F>
    bool Throws(F &&f)
    {
        try
        {
            f();
        }
        catch (...)
        {
            return true;
        }

        return false;
    }

    void TestBindGroups()
    {
        Image *color = MakeImage(8, 8, PE_FORMAT_R8G8B8A8_UNORM, kCopyUsage, "bind_color");
        color->CreateSRV(PE_IMAGE_VIEW_TYPE_2D, -1);
        Image *depth = MakeImage(8, 8, PE_FORMAT_D32_SFLOAT, PE_IMAGE_USAGE_SAMPLED | PE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT, "bind_depth");
        depth->CreateSRV(PE_IMAGE_VIEW_TYPE_2D, -1);
        Sampler *sampler = Sampler::Create(SamplerDesc{}, "bind_sampler");
        Buffer *uniform = Buffer::Create({.size = 64,
                                          .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER,
                                          .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT,
                                          .name = "bind_uniform"});

        const uint32_t errorsBefore = ErrorsSoFar();

        // b2 (read-only storage) and b3 (sampler) stay unset, so they bind dummies.
        const std::vector<DescriptorBindingInfo> infos = {
            Binding(0, PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
            Binding(1, PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
            Binding(2, PE_DESCRIPTOR_TYPE_STRUCTURED_BUFFER),
            Binding(3, PE_DESCRIPTOR_TYPE_SAMPLER),
            Binding(4, PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE, PE_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL),
        };
        Descriptor *descriptor = Descriptor::Create(infos, PE_SHADER_STAGE_FRAGMENT, false, "bind_descriptor");
        descriptor->SetBuffer(0, uniform);
        descriptor->SetImageView(1, color->GetSRV(), sampler);
        descriptor->SetImageView(4, depth->GetSRV());
        descriptor->Update();
        Check(ErrorsSoFar() == errorsBefore, "bind groups: uniform, combined, depth texture and two dummy slots build");

        // A later Update with one binding keeps the others.
        descriptor->SetImageView(1, color->GetSRV());
        descriptor->Update();
        Check(ErrorsSoFar() == errorsBefore, "bind groups: a partial Update keeps the earlier bindings");

        // Rule 2 through a bind group: binding the group makes the submit upload the mapped uniform buffer.
        uniform->Map();
        std::memset(uniform->Data(), 0x5A, uniform->Size());
        const uint64_t before = WebGpuBufferImpl::UploadedBytes();
        Submit([&](CommandBuffer *cmd)
               { cmd->BindDescriptors(1, &descriptor); });
        const uint64_t uploaded = WebGpuBufferImpl::UploadedBytes() - before;
        std::printf("[images] uniform upload through a bound group: %llu bytes (buffer %zu)\n",
                    static_cast<unsigned long long>(uploaded),
                    uniform->Size());
        Check(uploaded == uniform->Size(), "rule 2: binding a group uploads its mapped buffer");
        uniform->Unmap();

        // Rule 3: a host-visible buffer in a read-write storage binding.
        Buffer *hostStorage = Buffer::Create({.size = 64,
                                              .usage = PE_BUFFER_USAGE_STORAGE_BUFFER,
                                              .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                                              .name = "bind_host_storage"});
        Descriptor *rw = Descriptor::Create({Binding(0, PE_DESCRIPTOR_TYPE_STORAGE_BUFFER)}, PE_SHADER_STAGE_COMPUTE, false, "bind_rw");
        rw->SetBuffer(0, hostStorage);
        Check(Throws([&]
                     { rw->Update(); }),
              "rule 3: a host-visible buffer in read-write storage is an error");

        // A depth view in a float texture binding fails WebGPU validation, so the backend rejects it first.
        Descriptor *floatTexture = Descriptor::Create({Binding(0, PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE)}, PE_SHADER_STAGE_FRAGMENT, false, "bind_float");
        floatTexture->SetImageView(0, depth->GetSRV());
        Check(Throws([&]
                     { floatTexture->Update(); }),
              "a depth view in a float texture binding is an error");

        Descriptor::Destroy(descriptor);
        Descriptor::Destroy(rw);
        Descriptor::Destroy(floatTexture);
        Buffer::Destroy(uniform);
        Buffer::Destroy(hostStorage);
        Sampler::Destroy(sampler);
        Image::Destroy(color);
        Image::Destroy(depth);
    }
} // namespace

int main()
{
    try
    {
        EventSystem::Init(); // CommandBuffer::Wait dispatches AfterCommandWait
        RHII.Init(nullptr, PE_GRAPHICS_API_WEBGPU);

        TestUploads();
        TestRenderTarget();
        TestMipChain();
        TestBlit();
        TestSamplers();
        TestBindGroups();

        WGPUQueueWorkDoneCallbackInfo doneCb{};
        doneCb.mode = WGPUCallbackMode_WaitAnyOnly;
        doneCb.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void *, void *) {};
        WaitFuture(wgpuQueueOnSubmittedWorkDone(WebGpuRhiImpl::Get()->m_queue, doneCb));
        const uint32_t errors = WebGpuRhiImpl::Get()->m_errorCount;
        if (errors)
            s_failures++;

        RHII.Destroy();
        std::printf("[images] done: %d failures, %u WebGPU errors\n", s_failures, errors);
    }
    catch (const std::exception &e)
    {
        std::printf("[images] FAIL exception: %s\n[images] done: aborted\n", e.what());
    }

    return 0;
}
