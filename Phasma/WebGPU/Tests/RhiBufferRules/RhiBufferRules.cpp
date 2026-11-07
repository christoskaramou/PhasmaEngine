// WebGPU RHI backend, P2: the host-visible buffer rules of spec decision 5
// (docs/wiki/superpowers/specs/2026-11-06-webgpu-rhi-backend-design.md), run against the browser's WebGPU through the
// engine RHI. Prints "[rules] PASS ..." / "[rules] FAIL ..." lines and "[rules] done: N failures, M WebGPU errors".
// Build: the wasm-player preset, target RhiBufferRules (Emscripten only).
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/WebGPU/WebGpuBufferImpl.h"
#include "API/WebGPU/WebGpuRhiImpl.h"
#include "Base/EventSystem.h"

#include <cstdio>

using namespace pe;

namespace
{
    int s_failures = 0;

    void Check(bool ok, const char *what)
    {
        std::printf("[rules] %s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok)
            s_failures++;
    }

    void CheckBytes(uint64_t got, uint64_t want, const char *what)
    {
        const bool ok = got == want;
        std::printf("[rules] %s %s: uploaded %llu bytes, want %llu\n",
                    ok ? "PASS" : "FAIL",
                    what,
                    static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
        if (!ok)
            s_failures++;
    }

    void WaitFuture(WGPUFuture future)
    {
        WGPUFutureWaitInfo info{};
        info.future = future;
        wgpuInstanceWaitAny(WebGpuRhiImpl::Get()->m_instance, 1, &info, 10'000'000'000ull);
    }

    // Copies the buffer into a MapRead buffer outside the engine (readback is off on the web) and returns its bytes.
    std::vector<uint8_t> ReadBack(Buffer *buffer)
    {
        WebGpuRhiImpl *web = WebGpuRhiImpl::Get();
        const size_t size = WebGpuBufferImpl::From(buffer)->m_paddedSize;

        WGPUBufferDescriptor desc{};
        desc.size = size;
        desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        WGPUBuffer readback = wgpuDeviceCreateBuffer(web->m_device, &desc);

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(web->m_device, nullptr);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, WebGpuBufferImpl::From(buffer)->m_buffer, 0, readback, 0, size);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(web->m_queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);

        WGPUBufferMapCallbackInfo mapCb{};
        mapCb.mode = WGPUCallbackMode_WaitAnyOnly;
        mapCb.callback = [](WGPUMapAsyncStatus, WGPUStringView, void *, void *) {};
        WaitFuture(wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, size, mapCb));

        const auto *data = static_cast<const uint8_t *>(wgpuBufferGetConstMappedRange(readback, 0, size));
        std::vector<uint8_t> out(data, data + buffer->Size());
        wgpuBufferUnmap(readback);
        wgpuBufferDestroy(readback);
        wgpuBufferRelease(readback);
        return out;
    }

    Buffer *MakeBuffer(size_t size, PeMemoryUsage memory, PeBufferUsageFlags usage, const char *name)
    {
        return Buffer::Create({.size = size, .usage = usage, .memoryUsage = memory, .name = name});
    }

    void Fill(void *data, size_t offset, size_t size, uint8_t value)
    {
        std::memset(static_cast<uint8_t *>(data) + offset, value, size);
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

    // Rule 1: Unmap uploads only what no Flush uploaded since Map, and Flush ranges merge.
    void TestRule1()
    {
        Buffer *buffer = MakeBuffer(1024, PE_MEMORY_USAGE_CPU_TO_GPU, PE_BUFFER_USAGE_STORAGE_BUFFER, "rule1");

        uint64_t before = WebGpuBufferImpl::UploadedBytes();
        buffer->Map();
        Fill(buffer->Data(), 0, 256, 0x11);
        buffer->Flush(256, 0);
        Fill(buffer->Data(), 256, 768, 0x22);
        buffer->Unmap();
        CheckBytes(WebGpuBufferImpl::UploadedBytes() - before, 1024, "rule 1: Map-write-Flush-Unmap uploads each byte once");

        std::vector<uint8_t> gpu = ReadBack(buffer);
        Check(gpu[0] == 0x11 && gpu[255] == 0x11 && gpu[256] == 0x22 && gpu[1023] == 0x22,
              "rule 1: the GPU holds the flushed and the unflushed writes");

        // Touching flushes merge ([0,100) + [100,200)); a separate one stays apart; Unmap fills the two gaps.
        before = WebGpuBufferImpl::UploadedBytes();
        buffer->Map();
        Fill(buffer->Data(), 0, 1024, 0x33);
        buffer->Flush(100, 0);
        buffer->Flush(100, 100);
        buffer->Flush(8, 300);
        buffer->Unmap();
        CheckBytes(WebGpuBufferImpl::UploadedBytes() - before, 1024, "rule 1: merged flush ranges, gaps uploaded by Unmap");

        gpu = ReadBack(buffer);
        Check(std::all_of(gpu.begin(), gpu.end(), [](uint8_t b)
                          { return b == 0x33; }),
              "rule 1: every byte reached the GPU after merged flushes");

        Buffer::Destroy(buffer);
    }

    // Rule 2: a mapped non-staging buffer uploads whole before a submit that references it, and only then.
    void TestRule2()
    {
        constexpr PeBufferUsageFlags storage = PE_BUFFER_USAGE_STORAGE_BUFFER;
        Buffer *referenced = MakeBuffer(512, PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT, storage, "rule2_referenced");
        Buffer *unreferenced = MakeBuffer(512, PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT, storage, "rule2_unreferenced");
        Buffer *target = MakeBuffer(512, PE_MEMORY_USAGE_GPU_ONLY, storage, "rule2_target");

        referenced->Map();
        unreferenced->Map();
        Fill(referenced->Data(), 0, 512, 0x44); // raw-pointer writes: no Flush, no Unmap
        Fill(unreferenced->Data(), 0, 512, 0x55);

        const uint64_t before = WebGpuBufferImpl::UploadedBytes();
        Submit([&](CommandBuffer *cmd)
               { cmd->CopyBuffer(referenced, target, 512, 0, 0); });
        CheckBytes(WebGpuBufferImpl::UploadedBytes() - before, 512, "rule 2: only the referenced mapped buffer uploads");

        std::vector<uint8_t> gpu = ReadBack(target);
        Check(std::all_of(gpu.begin(), gpu.end(), [](uint8_t b)
                          { return b == 0x44; }),
              "rule 2: raw-pointer writes reach the GPU before the submit's copy");

        // The next submit sees the latest shadow, not the first one.
        Fill(referenced->Data(), 0, 512, 0x66);
        Submit([&](CommandBuffer *cmd)
               { cmd->CopyBuffer(referenced, target, 512, 0, 0); });
        gpu = ReadBack(target);
        Check(gpu[0] == 0x66 && gpu[511] == 0x66, "rule 2: a later rewrite uploads at the next referencing submit");

        referenced->Unmap();
        unreferenced->Unmap();
        Buffer::Destroy(referenced);
        Buffer::Destroy(unreferenced);
        Buffer::Destroy(target);
    }

    // Staging (TRANSFER_SRC) never uploads whole: CopyBufferStaged sends only its flushed bytes.
    void TestStaging()
    {
        Buffer *target = MakeBuffer(256, PE_MEMORY_USAGE_GPU_ONLY, PE_BUFFER_USAGE_STORAGE_BUFFER, "staging_target");
        std::vector<uint8_t> data(40, 0x77);

        const uint64_t before = WebGpuBufferImpl::UploadedBytes();
        Submit([&](CommandBuffer *cmd)
               { cmd->CopyBufferStaged(target, data.data(), data.size(), 64); });
        CheckBytes(WebGpuBufferImpl::UploadedBytes() - before, 40, "staging: only the staged bytes upload");

        std::vector<uint8_t> gpu = ReadBack(target);
        Check(gpu[63] == 0 && gpu[64] == 0x77 && gpu[103] == 0x77 && gpu[104] == 0,
              "staging: CopyBufferStaged lands at its offset");

        Buffer::Destroy(target);
    }

    void TestFill()
    {
        Buffer *target = MakeBuffer(256, PE_MEMORY_USAGE_GPU_ONLY, PE_BUFFER_USAGE_STORAGE_BUFFER, "fill_target");
        Submit([&](CommandBuffer *cmd)
               {
                   cmd->FillBuffer(target, 0, 256, 0xABCDEF01u);
                   cmd->FillBuffer(target, 128, 64, 0); });

        std::vector<uint8_t> gpu = ReadBack(target);
        uint32_t first = 0;
        uint32_t cleared = 1;
        uint32_t last = 0;
        std::memcpy(&first, gpu.data(), 4);
        std::memcpy(&cleared, gpu.data() + 128, 4);
        std::memcpy(&last, gpu.data() + 252, 4);
        Check(first == 0xABCDEF01u && cleared == 0 && last == 0xABCDEF01u, "FillBuffer: pattern and zero fills");

        Buffer::Destroy(target);
    }

    // Rule 3: a host-visible buffer is never GPU-written, so copying into one fails at once.
    void TestRule3()
    {
        Buffer *source = MakeBuffer(64, PE_MEMORY_USAGE_GPU_ONLY, PE_BUFFER_USAGE_STORAGE_BUFFER, "rule3_source");
        Buffer *hostVisible = MakeBuffer(64, PE_MEMORY_USAGE_CPU_TO_GPU, PE_BUFFER_USAGE_STORAGE_BUFFER, "rule3_host");

        bool rejected = false;
        Queue *queue = RHII.GetMainQueue();
        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        try
        {
            cmd->CopyBuffer(source, hostVisible, 64, 0, 0);
        }
        catch (...)
        {
            rejected = true;
        }
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();
        Check(rejected, "rule 3: copying into a host-visible buffer is an error");

        Buffer::Destroy(source);
        Buffer::Destroy(hostVisible);
    }
} // namespace

int main()
{
    try
    {
        EventSystem::Init(); // CommandBuffer::Wait dispatches AfterCommandWait
        RHII.Init(nullptr, PE_GRAPHICS_API_WEBGPU);
        Check(WebGpuRhiImpl::Get() && WebGpuRhiImpl::Get()->m_device, "RHI::Init created a WebGPU device");

        TestRule1();
        TestRule2();
        TestStaging();
        TestFill();
        TestRule3();

        WGPUQueueWorkDoneCallbackInfo doneCb{};
        doneCb.mode = WGPUCallbackMode_WaitAnyOnly;
        doneCb.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void *, void *) {};
        WaitFuture(wgpuQueueOnSubmittedWorkDone(WebGpuRhiImpl::Get()->m_queue, doneCb));
        const uint32_t errors = WebGpuRhiImpl::Get()->m_errorCount;
        if (errors)
            s_failures++;

        RHII.Destroy();
        std::printf("[rules] done: %d failures, %u WebGPU errors\n", s_failures, errors);
    }
    catch (const std::exception &e)
    {
        std::printf("[rules] FAIL exception: %s\n[rules] done: aborted\n", e.what());
    }

    return 0;
}
