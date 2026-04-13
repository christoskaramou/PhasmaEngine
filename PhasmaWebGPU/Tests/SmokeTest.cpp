#include <webgpu/webgpu.h>
#include "Base/Log.h"
#include "Base/Path.h"
#include "Base/EventSystem.h"
#include "API/RHI.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// clang-format off
static const uint32_t kDoubleShaderSpirv[] = {
    0x07230203, 0x00010000, 0x0008000b, 0x00000025, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
    0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
    0x0006000f, 0x00000005, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000b, 0x00060010, 0x00000004,
    0x00000011, 0x00000040, 0x00000001, 0x00000001, 0x00030003, 0x00000002, 0x000001c2, 0x00040005,
    0x00000004, 0x6e69616d, 0x00000000, 0x00030005, 0x00000008, 0x00786469, 0x00080005, 0x0000000b,
    0x475f6c67, 0x61626f6c, 0x766e496c, 0x7461636f, 0x496e6f69, 0x00000044, 0x00040005, 0x00000011,
    0x4274754f, 0x00006675, 0x00050006, 0x00000011, 0x00000000, 0x61746164, 0x00000000, 0x00040005,
    0x00000013, 0x4274756f, 0x00006675, 0x00040005, 0x00000018, 0x75426e49, 0x00000066, 0x00050006,
    0x00000018, 0x00000000, 0x61746164, 0x00000000, 0x00040005, 0x0000001a, 0x75426e69, 0x00000066,
    0x00040047, 0x0000000b, 0x0000000b, 0x0000001c, 0x00040047, 0x00000010, 0x00000006, 0x00000004,
    0x00030047, 0x00000011, 0x00000003, 0x00050048, 0x00000011, 0x00000000, 0x00000023, 0x00000000,
    0x00040047, 0x00000013, 0x00000021, 0x00000001, 0x00040047, 0x00000013, 0x00000022, 0x00000000,
    0x00040047, 0x00000017, 0x00000006, 0x00000004, 0x00030047, 0x00000018, 0x00000003, 0x00050048,
    0x00000018, 0x00000000, 0x00000023, 0x00000000, 0x00040047, 0x0000001a, 0x00000021, 0x00000000,
    0x00040047, 0x0000001a, 0x00000022, 0x00000000, 0x00040047, 0x00000024, 0x0000000b, 0x00000019,
    0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00040015, 0x00000006, 0x00000020,
    0x00000000, 0x00040020, 0x00000007, 0x00000007, 0x00000006, 0x00040017, 0x00000009, 0x00000006,
    0x00000003, 0x00040020, 0x0000000a, 0x00000001, 0x00000009, 0x0004003b, 0x0000000a, 0x0000000b,
    0x00000001, 0x0004002b, 0x00000006, 0x0000000c, 0x00000000, 0x00040020, 0x0000000d, 0x00000001,
    0x00000006, 0x0003001d, 0x00000010, 0x00000006, 0x0003001e, 0x00000011, 0x00000010, 0x00040020,
    0x00000012, 0x00000002, 0x00000011, 0x0004003b, 0x00000012, 0x00000013, 0x00000002, 0x00040015,
    0x00000014, 0x00000020, 0x00000001, 0x0004002b, 0x00000014, 0x00000015, 0x00000000, 0x0003001d,
    0x00000017, 0x00000006, 0x0003001e, 0x00000018, 0x00000017, 0x00040020, 0x00000019, 0x00000002,
    0x00000018, 0x0004003b, 0x00000019, 0x0000001a, 0x00000002, 0x00040020, 0x0000001c, 0x00000002,
    0x00000006, 0x0004002b, 0x00000006, 0x0000001f, 0x00000002, 0x0004002b, 0x00000006, 0x00000022,
    0x00000040, 0x0004002b, 0x00000006, 0x00000023, 0x00000001, 0x0006002c, 0x00000009, 0x00000024,
    0x00000022, 0x00000023, 0x00000023, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
    0x000200f8, 0x00000005, 0x0004003b, 0x00000007, 0x00000008, 0x00000007, 0x00050041, 0x0000000d,
    0x0000000e, 0x0000000b, 0x0000000c, 0x0004003d, 0x00000006, 0x0000000f, 0x0000000e, 0x0003003e,
    0x00000008, 0x0000000f, 0x0004003d, 0x00000006, 0x00000016, 0x00000008, 0x0004003d, 0x00000006,
    0x0000001b, 0x00000008, 0x00060041, 0x0000001c, 0x0000001d, 0x0000001a, 0x00000015, 0x0000001b,
    0x0004003d, 0x00000006, 0x0000001e, 0x0000001d, 0x00050084, 0x00000006, 0x00000020, 0x0000001e,
    0x0000001f, 0x00060041, 0x0000001c, 0x00000021, 0x00000013, 0x00000015, 0x00000016, 0x0003003e,
    0x00000021, 0x00000020, 0x000100fd, 0x00010038,
};
// clang-format on
static const size_t kDoubleShaderSpirvSize = sizeof(kDoubleShaderSpirv) / sizeof(uint32_t);

static int g_errors = 0;

#define CHECK(cond, msg)                                \
    do                                                  \
    {                                                   \
        if (!(cond))                                    \
        {                                               \
            fprintf(stderr, "FAIL: %s  (%s:%d)\n", msg, \
                    __FILE__, __LINE__);                \
            g_errors++;                                 \
        }                                               \
        else                                            \
        {                                               \
            printf("  OK: %s\n", msg);                  \
        }                                               \
    }                                                   \
    while (0)

static void uncapturedError(WGPUDevice const *device, WGPUErrorType type,
                            WGPUStringView message, void *userdata1, void *userdata2)
{
    (void)device;
    (void)userdata1;
    (void)userdata2;
    fprintf(stderr, "[WebGPU uncaptured error type=%d] %.*s\n",
            static_cast<int>(type),
            message.data ? static_cast<int>(message.length) : 0,
            message.data ? message.data : "");
    g_errors++;
}

int main(int /*argc*/, char * /*argv*/[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== PhasmaWebGPU Smoke Test ===\n\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    uint32_t windowFlags = SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN;
    SDL_Window *window = SDL_CreateWindow("WebGPU Smoke Test", 0, 0, 64, 64, windowFlags);
    if (!window)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    pe::EventSystem::Init();
    pe::RHII.Init(window);
    printf("[RHI] Vulkan initialized on %s\n\n", pe::RHII.GetGpuName().c_str());

    // 1. Instance

    printf("--- 1. Instance ---\n");

    WGPUInstance instance = wgpuCreateInstance(nullptr);
    CHECK(instance != nullptr, "wgpuCreateInstance returns non-null");

    // 2. Adapter

    printf("\n--- 2. Adapter ---\n");

    WGPUAdapter adapter = nullptr;
    WGPURequestAdapterCallbackInfo adapterCb{};
    adapterCb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter a,
                            WGPUStringView msg, void *u1, void *u2)
    {
        if (status != WGPURequestAdapterStatus_Success)
        {
            fprintf(stderr, "  requestAdapter failed (status=%d): %.*s\n",
                    static_cast<int>(status),
                    msg.data ? static_cast<int>(msg.length) : 0,
                    msg.data ? msg.data : "");
        }
        *static_cast<WGPUAdapter *>(u1) = (status == WGPURequestAdapterStatus_Success) ? a : nullptr;
    };
    adapterCb.userdata1 = &adapter;
    wgpuInstanceRequestAdapter(instance, nullptr, adapterCb);
    CHECK(adapter != nullptr, "requestAdapter succeeds");

    if (adapter)
    {
        WGPUAdapterInfo info{};
        wgpuAdapterGetInfo(adapter, &info);
        printf("  GPU: %.*s  vendor: %.*s  backend: Vulkan\n",
               static_cast<int>(info.device.length), info.device.data,
               static_cast<int>(info.vendor.length), info.vendor.data);
        wgpuAdapterInfoFreeMembers(info);
    }

    // 3. Device + Queue

    printf("\n--- 3. Device ---\n");

    WGPUDevice device = nullptr;
    WGPUUncapturedErrorCallbackInfo errCb{};
    errCb.callback = uncapturedError;
    WGPUDeviceDescriptor devDesc{};
    devDesc.uncapturedErrorCallbackInfo = errCb;
    devDesc.label = {nullptr, WGPU_STRLEN};

    WGPURequestDeviceCallbackInfo devCb{};
    devCb.callback = [](WGPURequestDeviceStatus status, WGPUDevice d,
                        WGPUStringView msg, void *u1, void *u2)
    {
        (void)msg;
        *static_cast<WGPUDevice *>(u1) = (status == WGPURequestDeviceStatus_Success) ? d : nullptr;
    };
    devCb.userdata1 = &device;
    wgpuAdapterRequestDevice(adapter, &devDesc, devCb);
    CHECK(device != nullptr, "requestDevice succeeds");

    WGPUQueue queue = device ? wgpuDeviceGetQueue(device) : nullptr;
    CHECK(queue != nullptr, "device has a queue");

    if (!device || !queue)
    {
        fprintf(stderr, "Cannot proceed without device/queue.\n");
        pe::RHII.Destroy();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // 4. Buffers

    printf("\n--- 4. Buffers ---\n");

    const uint32_t kCount = 64;
    const uint64_t kBufSize = kCount * sizeof(uint32_t);

    WGPUBufferDescriptor inDesc{};
    inDesc.label = {"input", WGPU_STRLEN};
    inDesc.size = kBufSize;
    inDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    inDesc.mappedAtCreation = WGPU_TRUE;
    WGPUBuffer inputBuf = wgpuDeviceCreateBuffer(device, &inDesc);
    CHECK(inputBuf != nullptr, "input buffer created");

    {
        void *mapped = wgpuBufferGetMappedRange(inputBuf, 0, kBufSize);
        CHECK(mapped != nullptr, "input buffer mapped range");
        if (mapped)
        {
            auto *p = static_cast<uint32_t *>(mapped);
            for (uint32_t i = 0; i < kCount; i++)
                p[i] = i + 1;
        }
        wgpuBufferUnmap(inputBuf);
    }

    WGPUBufferDescriptor outDesc{};
    outDesc.label = {"output", WGPU_STRLEN};
    outDesc.size = kBufSize;
    outDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuf = wgpuDeviceCreateBuffer(device, &outDesc);
    CHECK(outputBuf != nullptr, "output buffer created");

    WGPUBufferDescriptor readDesc{};
    readDesc.label = {"readback", WGPU_STRLEN};
    readDesc.size = kBufSize;
    readDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    WGPUBuffer readbackBuf = wgpuDeviceCreateBuffer(device, &readDesc);
    CHECK(readbackBuf != nullptr, "readback buffer created");

    // 5. Bind Group Layout + Pipeline Layout

    printf("\n--- 5. Bind Group Layout + Pipeline Layout ---\n");

    WGPUBindGroupLayoutEntry bglEntries[2] = {};
    bglEntries[0].binding = 0;
    bglEntries[0].visibility = WGPUShaderStage_Compute;
    bglEntries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    bglEntries[0].buffer.minBindingSize = kBufSize;

    bglEntries[1].binding = 1;
    bglEntries[1].visibility = WGPUShaderStage_Compute;
    bglEntries[1].buffer.type = WGPUBufferBindingType_Storage;
    bglEntries[1].buffer.minBindingSize = kBufSize;

    WGPUBindGroupLayoutDescriptor bglDesc{};
    bglDesc.label = {"compute_bgl", WGPU_STRLEN};
    bglDesc.entryCount = 2;
    bglDesc.entries = bglEntries;
    WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    CHECK(bgl != nullptr, "bind group layout created");

    WGPUPipelineLayoutDescriptor plDesc{};
    plDesc.label = {"compute_pl", WGPU_STRLEN};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &bgl;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);
    CHECK(pipelineLayout != nullptr, "pipeline layout created");

    // 6. Shader Module (SPIR-V)

    printf("\n--- 6. Shader Module ---\n");

    WGPUShaderSourceSPIRV spirvSource{};
    spirvSource.chain.sType = WGPUSType_ShaderSourceSPIRV;
    spirvSource.code = kDoubleShaderSpirv;
    spirvSource.codeSize = static_cast<uint32_t>(kDoubleShaderSpirvSize);

    WGPUShaderModuleDescriptor smDesc{};
    smDesc.label = {"double_shader", WGPU_STRLEN};
    smDesc.nextInChain = &spirvSource.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &smDesc);
    CHECK(shaderModule != nullptr, "shader module created");

    // 7. Compute Pipeline

    printf("\n--- 7. Compute Pipeline ---\n");

    WGPUComputePipelineDescriptor cpDesc{};
    cpDesc.label = {"double_pipeline", WGPU_STRLEN};
    cpDesc.layout = pipelineLayout;
    cpDesc.compute.module = shaderModule;
    cpDesc.compute.entryPoint = {"main", WGPU_STRLEN};
    WGPUComputePipeline computePipeline = wgpuDeviceCreateComputePipeline(device, &cpDesc);
    CHECK(computePipeline != nullptr, "compute pipeline created");

    // 8. Bind Group

    printf("\n--- 8. Bind Group ---\n");

    WGPUBindGroupEntry bgEntries[2] = {};
    bgEntries[0].binding = 0;
    bgEntries[0].buffer = inputBuf;
    bgEntries[0].offset = 0;
    bgEntries[0].size = kBufSize;

    bgEntries[1].binding = 1;
    bgEntries[1].buffer = outputBuf;
    bgEntries[1].offset = 0;
    bgEntries[1].size = kBufSize;

    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.label = {"compute_bg", WGPU_STRLEN};
    bgDesc.layout = bgl;
    bgDesc.entryCount = 2;
    bgDesc.entries = bgEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
    CHECK(bindGroup != nullptr, "bind group created");

    // 9. Command Encoding + Dispatch

    printf("\n--- 9. Command Encoding + Dispatch ---\n");

    WGPUCommandEncoderDescriptor ceDesc{};
    ceDesc.label = {"smoke_encoder", WGPU_STRLEN};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &ceDesc);
    CHECK(encoder != nullptr, "command encoder created");

    WGPUComputePassDescriptor passDesc{};
    passDesc.label = {"smoke_compute_pass", WGPU_STRLEN};
    WGPUComputePassEncoder computePass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    CHECK(computePass != nullptr, "compute pass begun");

    wgpuComputePassEncoderSetPipeline(computePass, computePipeline);
    wgpuComputePassEncoderSetBindGroup(computePass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(computePass, 1, 1, 1);
    wgpuComputePassEncoderEnd(computePass);

    wgpuCommandEncoderCopyBufferToBuffer(encoder, outputBuf, 0, readbackBuf, 0, kBufSize);

    WGPUCommandBufferDescriptor cbDesc{};
    cbDesc.label = {"smoke_cmdbuf", WGPU_STRLEN};
    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &cbDesc);
    CHECK(cmdBuf != nullptr, "command buffer finished");

    // 10. Queue Submit

    printf("\n--- 10. Queue Submit ---\n");
    fflush(stdout);

    printf("  Submitting...\n");
    fflush(stdout);
    wgpuQueueSubmit(queue, 1, &cmdBuf);
    printf("  Queue submit completed.\n");
    fflush(stdout);

    // 11. Readback + Verify

    printf("\n--- 11. Readback + Verify ---\n");

    bool mapDone = false;
    WGPUBufferMapCallbackInfo mapCb{};
    mapCb.callback = [](WGPUMapAsyncStatus status, WGPUStringView msg,
                        void *u1, void *u2)
    {
        (void)msg;
        (void)u2;
        *static_cast<bool *>(u1) = (status == WGPUMapAsyncStatus_Success);
    };
    mapCb.userdata1 = &mapDone;
    wgpuBufferMapAsync(readbackBuf, WGPUMapMode_Read, 0, kBufSize, mapCb);
    wgpuInstanceProcessEvents(instance);
    CHECK(mapDone, "readback buffer map succeeded");

    if (mapDone)
    {
        const void *data = wgpuBufferGetConstMappedRange(readbackBuf, 0, kBufSize);
        CHECK(data != nullptr, "readback mapped range non-null");

        if (data)
        {
            const auto *results = static_cast<const uint32_t *>(data);
            bool allCorrect = true;
            for (uint32_t i = 0; i < kCount; i++)
            {
                uint32_t expected = (i + 1) * 2;
                if (results[i] != expected)
                {
                    fprintf(stderr, "  MISMATCH at [%u]: got %u, expected %u\n",
                            i, results[i], expected);
                    allCorrect = false;
                }
            }
            CHECK(allCorrect, "all 64 output values are input*2");
        }
    }
    wgpuBufferUnmap(readbackBuf);

    // 12. Cleanup

    printf("\n--- 12. Cleanup ---\n");

    wgpuCommandBufferRelease(cmdBuf);
    wgpuComputePassEncoderRelease(computePass);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);
    wgpuComputePipelineRelease(computePipeline);
    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuBufferRelease(readbackBuf);
    wgpuBufferRelease(outputBuf);
    wgpuBufferRelease(inputBuf);
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);
    printf("  All WebGPU objects released.\n");

    pe::RHII.WaitDeviceIdle();
    pe::RHII.Destroy();
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("\n=== %s (%d error%s) ===\n",
           g_errors == 0 ? "ALL PASSED" : "FAILED",
           g_errors, g_errors == 1 ? "" : "s");
    return g_errors == 0 ? 0 : 1;
}
