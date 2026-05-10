#include <webgpu/webgpu.h>
#include "Base/Log.h"
#include "Base/Path.h"
#include "Base/EventSystem.h"
#include "API/GraphicsApiSelection.h"
#include "API/ImageView.h"
#include "API/RHI.h"
#include "RenderPass.h"
#include "Texture.h"
#include "Common/RunOptions.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12ImageViewImpl.h"
#endif
#include <SDL.h>

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

static const char *BackendName(WGPUBackendType backend)
{
    switch (backend)
    {
    case WGPUBackendType_D3D12:
        return "D3D12";
    case WGPUBackendType_Vulkan:
        return "Vulkan";
    default:
        return "Unknown";
    }
}

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== PhasmaWebGPU Smoke Test ===\n\n");

    const pwgpu::test::RunOptions runOptions = pwgpu::test::ParseRunOptions(argc, argv);
    if (!runOptions.Succeeded())
    {
        fprintf(stderr, "%s\n", runOptions.error.c_str());
        return 1;
    }

    const pe::GraphicsApiSelection apiSelection = pe::ResolveGraphicsApi(argc, argv);
    if (!apiSelection.Succeeded())
    {
        fprintf(stderr, "%s\n", apiSelection.error.c_str());
        return 1;
    }
    PeGraphicsApi api = apiSelection.api;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    std::string displayError;
    if (!pwgpu::test::ValidateDisplayIndex(runOptions.displayIndex, displayError))
    {
        fprintf(stderr, "%s\n", displayError.c_str());
        SDL_Quit();
        return 1;
    }

    uint32_t windowFlags = SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN;
    if (api == PE_GRAPHICS_API_DX12)
        windowFlags &= ~SDL_WINDOW_VULKAN;
    SDL_Window *window = SDL_CreateWindow("WebGPU Smoke Test",
                                          SDL_WINDOWPOS_CENTERED_DISPLAY(runOptions.displayIndex),
                                          SDL_WINDOWPOS_CENTERED_DISPLAY(runOptions.displayIndex),
                                          64,
                                          64,
                                          windowFlags);
    if (!window)
    {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    pe::EventSystem::Init();
    pe::RHII.Init(window, api);
    printf("[RHI] %s initialized on %s\n\n", PeGraphicsApiName(pe::RHII.GetApi()), pe::RHII.GetGpuName().c_str());

    // 1. Instance

    printf("--- 1. Instance ---\n");

    WGPUInstance instance = wgpuCreateInstance(nullptr);
    CHECK(instance != nullptr, "wgpuCreateInstance returns non-null");

    // 2. Adapter

    printf("\n--- 2. Adapter ---\n");

    WGPUAdapter adapter = nullptr;
    WGPURequestAdapterCallbackInfo adapterCb{};
    adapterCb.mode = WGPUCallbackMode_AllowSpontaneous;
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
        printf("  GPU: %.*s  vendor: %.*s  backend: %s\n",
               static_cast<int>(info.device.length), info.device.data,
               static_cast<int>(info.vendor.length), info.vendor.data,
               BackendName(info.backendType));
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
    devCb.mode = WGPUCallbackMode_AllowSpontaneous;
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
    const uint64_t kInputBufSize = kBufSize * 2;

    WGPUBufferDescriptor inDesc{};
    inDesc.label = {"input", WGPU_STRLEN};
    inDesc.size = kInputBufSize;
    inDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    inDesc.mappedAtCreation = WGPU_TRUE;
    WGPUBuffer inputBuf = wgpuDeviceCreateBuffer(device, &inDesc);
    CHECK(inputBuf != nullptr, "input buffer created");

    {
        void *mapped = wgpuBufferGetMappedRange(inputBuf, 0, kInputBufSize);
        CHECK(mapped != nullptr, "input buffer mapped range");
        if (mapped)
        {
            auto *p = static_cast<uint32_t *>(mapped);
            for (uint32_t i = 0; i < kCount; i++)
            {
                p[i] = i + 1;
                p[kCount + i] = 1000 + i;
            }
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
    // SPIR-V variable at (set=0, binding=0) has no NonWritable decoration,
    // so per W3C §17.2 it must be matched by a read-write storage entry.
    bglEntries[0].buffer.type = WGPUBufferBindingType_Storage;
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
    const int errorsBeforeComputePipeline = g_errors;
    WGPUComputePipeline computePipeline = wgpuDeviceCreateComputePipeline(device, &cpDesc);
    if (g_errors == errorsBeforeComputePipeline)
        CHECK(computePipeline != nullptr, "compute pipeline created");

    if (g_errors > 0)
    {
        fprintf(stderr, "Stopping after compute pipeline creation error.\n");
        wgpuDeviceDestroy(device);
        if (computePipeline)
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
        pe::RHII.WaitDeviceIdle();
        pe::RHII.Destroy();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

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
    mapCb.mode = WGPUCallbackMode_AllowProcessEvents;
    mapCb.callback = [](WGPUMapAsyncStatus status, WGPUStringView msg,
                        void *u1, void *u2)
    {
        (void)msg;
        (void)u2;
        *static_cast<bool *>(u1) = (status == WGPUMapAsyncStatus_Success);
    };
    mapCb.userdata1 = &mapDone;
    WGPUFuture mapFuture = wgpuBufferMapAsync(readbackBuf, WGPUMapMode_Read, 0, kBufSize, mapCb);
    WGPUFutureWaitInfo waitInfo{mapFuture, WGPU_FALSE};
    wgpuInstanceWaitAny(instance, 1, &waitInfo, UINT64_MAX);
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

    // 12. Dynamic offset bind group

    printf("\n--- 12. Dynamic Offset Bind Group ---\n");

    WGPUBindGroupLayoutEntry dynBglEntries[2] = {};
    dynBglEntries[0].binding = 0;
    dynBglEntries[0].visibility = WGPUShaderStage_Compute;
    dynBglEntries[0].buffer.type = WGPUBufferBindingType_Storage;
    dynBglEntries[0].buffer.hasDynamicOffset = WGPU_TRUE;
    dynBglEntries[0].buffer.minBindingSize = kBufSize;

    dynBglEntries[1].binding = 1;
    dynBglEntries[1].visibility = WGPUShaderStage_Compute;
    dynBglEntries[1].buffer.type = WGPUBufferBindingType_Storage;
    dynBglEntries[1].buffer.minBindingSize = kBufSize;

    WGPUBindGroupLayoutDescriptor dynBglDesc{};
    dynBglDesc.label = {"dynamic_compute_bgl", WGPU_STRLEN};
    dynBglDesc.entryCount = 2;
    dynBglDesc.entries = dynBglEntries;
    WGPUBindGroupLayout dynBgl = wgpuDeviceCreateBindGroupLayout(device, &dynBglDesc);
    CHECK(dynBgl != nullptr, "dynamic bind group layout created");

    WGPUPipelineLayoutDescriptor dynPlDesc{};
    dynPlDesc.label = {"dynamic_compute_pl", WGPU_STRLEN};
    dynPlDesc.bindGroupLayoutCount = 1;
    dynPlDesc.bindGroupLayouts = &dynBgl;
    WGPUPipelineLayout dynPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &dynPlDesc);
    CHECK(dynPipelineLayout != nullptr, "dynamic pipeline layout created");

    WGPUComputePipelineDescriptor dynCpDesc{};
    dynCpDesc.label = {"dynamic_double_pipeline", WGPU_STRLEN};
    dynCpDesc.layout = dynPipelineLayout;
    dynCpDesc.compute.module = shaderModule;
    dynCpDesc.compute.entryPoint = {"main", WGPU_STRLEN};
    const int errorsBeforeDynamicPipeline = g_errors;
    WGPUComputePipeline dynComputePipeline = wgpuDeviceCreateComputePipeline(device, &dynCpDesc);
    if (g_errors == errorsBeforeDynamicPipeline)
        CHECK(dynComputePipeline != nullptr, "dynamic compute pipeline created");

    WGPUBindGroupEntry dynBgEntries[2] = {};
    dynBgEntries[0].binding = 0;
    dynBgEntries[0].buffer = inputBuf;
    dynBgEntries[0].offset = 0;
    dynBgEntries[0].size = kBufSize;

    dynBgEntries[1].binding = 1;
    dynBgEntries[1].buffer = outputBuf;
    dynBgEntries[1].offset = 0;
    dynBgEntries[1].size = kBufSize;

    WGPUBindGroupDescriptor dynBgDesc{};
    dynBgDesc.label = {"dynamic_compute_bg", WGPU_STRLEN};
    dynBgDesc.layout = dynBgl;
    dynBgDesc.entryCount = 2;
    dynBgDesc.entries = dynBgEntries;
    WGPUBindGroup dynBindGroup = wgpuDeviceCreateBindGroup(device, &dynBgDesc);
    CHECK(dynBindGroup != nullptr, "dynamic bind group created");

    if (dynComputePipeline && dynBindGroup)
    {
        WGPUCommandEncoderDescriptor dynCeDesc{};
        dynCeDesc.label = {"dynamic_smoke_encoder", WGPU_STRLEN};
        WGPUCommandEncoder dynEncoder = wgpuDeviceCreateCommandEncoder(device, &dynCeDesc);
        CHECK(dynEncoder != nullptr, "dynamic command encoder created");

        WGPUComputePassDescriptor dynPassDesc{};
        dynPassDesc.label = {"dynamic_smoke_compute_pass", WGPU_STRLEN};
        WGPUComputePassEncoder dynPass =
            dynEncoder ? wgpuCommandEncoderBeginComputePass(dynEncoder, &dynPassDesc) : nullptr;
        CHECK(dynPass != nullptr, "dynamic compute pass begun");

        if (dynPass)
        {
            const uint32_t dynamicOffset = static_cast<uint32_t>(kBufSize);
            wgpuComputePassEncoderSetPipeline(dynPass, dynComputePipeline);
            wgpuComputePassEncoderSetBindGroup(dynPass, 0, dynBindGroup, 1, &dynamicOffset);
            wgpuComputePassEncoderDispatchWorkgroups(dynPass, 1, 1, 1);
            wgpuComputePassEncoderEnd(dynPass);
        }

        if (dynEncoder)
            wgpuCommandEncoderCopyBufferToBuffer(dynEncoder, outputBuf, 0, readbackBuf, 0, kBufSize);

        WGPUCommandBufferDescriptor dynCbDesc{};
        dynCbDesc.label = {"dynamic_smoke_cmdbuf", WGPU_STRLEN};
        WGPUCommandBuffer dynCmdBuf =
            dynEncoder ? wgpuCommandEncoderFinish(dynEncoder, &dynCbDesc) : nullptr;
        CHECK(dynCmdBuf != nullptr, "dynamic command buffer finished");

        if (dynCmdBuf)
        {
            wgpuQueueSubmit(queue, 1, &dynCmdBuf);

            mapDone = false;
            WGPUFuture dynMapFuture =
                wgpuBufferMapAsync(readbackBuf, WGPUMapMode_Read, 0, kBufSize, mapCb);
            WGPUFutureWaitInfo dynWaitInfo{dynMapFuture, WGPU_FALSE};
            wgpuInstanceWaitAny(instance, 1, &dynWaitInfo, UINT64_MAX);
            CHECK(mapDone, "dynamic readback buffer map succeeded");

            if (mapDone)
            {
                const void *data = wgpuBufferGetConstMappedRange(readbackBuf, 0, kBufSize);
                CHECK(data != nullptr, "dynamic readback mapped range non-null");

                if (data)
                {
                    const auto *results = static_cast<const uint32_t *>(data);
                    bool allCorrect = true;
                    for (uint32_t i = 0; i < kCount; i++)
                    {
                        uint32_t expected = (1000 + i) * 2;
                        if (results[i] != expected)
                        {
                            fprintf(stderr,
                                    "  DYNAMIC MISMATCH at [%u]: got %u, expected %u\n",
                                    i, results[i], expected);
                            allCorrect = false;
                        }
                    }
                    CHECK(allCorrect, "dynamic offset selects second input slice");
                }
            }
            wgpuBufferUnmap(readbackBuf);
        }

        if (dynCmdBuf)
            wgpuCommandBufferRelease(dynCmdBuf);
        if (dynPass)
            wgpuComputePassEncoderRelease(dynPass);
        if (dynEncoder)
            wgpuCommandEncoderRelease(dynEncoder);
    }

    if (dynBindGroup)
        wgpuBindGroupRelease(dynBindGroup);
    if (dynComputePipeline)
        wgpuComputePipelineRelease(dynComputePipeline);
    if (dynPipelineLayout)
        wgpuPipelineLayoutRelease(dynPipelineLayout);
    if (dynBgl)
        wgpuBindGroupLayoutRelease(dynBgl);

    // 13. 3D color attachment depthSlice

    printf("\n--- 13. 3D Color Attachment depthSlice ---\n");

    WGPUTextureDescriptor tex3dDesc{};
    tex3dDesc.label = {"color_attachment_3d", WGPU_STRLEN};
    tex3dDesc.usage = WGPUTextureUsage_RenderAttachment;
    tex3dDesc.dimension = WGPUTextureDimension_3D;
    tex3dDesc.size = {2, 2, 4};
    tex3dDesc.format = WGPUTextureFormat_RGBA8Unorm;
    tex3dDesc.mipLevelCount = 1;
    tex3dDesc.sampleCount = 1;
    WGPUTexture tex3d = wgpuDeviceCreateTexture(device, &tex3dDesc);
    CHECK(tex3d != nullptr, "3D render attachment texture created");

    WGPUTextureViewDescriptor view3dDesc{};
    view3dDesc.label = {"color_attachment_3d_view", WGPU_STRLEN};
    view3dDesc.format = WGPUTextureFormat_RGBA8Unorm;
    view3dDesc.dimension = WGPUTextureViewDimension_3D;
    view3dDesc.aspect = WGPUTextureAspect_All;
    view3dDesc.baseMipLevel = 0;
    view3dDesc.mipLevelCount = 1;
    view3dDesc.baseArrayLayer = 0;
    view3dDesc.arrayLayerCount = 1;
    view3dDesc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTextureView view3d = tex3d ? wgpuTextureCreateView(tex3d, &view3dDesc) : nullptr;
    CHECK(view3d != nullptr, "3D render attachment view created");

    WGPUCommandBuffer pass3dCmdBuf = nullptr;
    if (view3d)
    {
        const int errorsBefore3d = g_errors;

        WGPUCommandEncoderDescriptor pass3dEncoderDesc{};
        pass3dEncoderDesc.label = {"depth_slice_encoder", WGPU_STRLEN};
        WGPUCommandEncoder pass3dEncoder = wgpuDeviceCreateCommandEncoder(device, &pass3dEncoderDesc);
        CHECK(pass3dEncoder != nullptr, "3D depthSlice command encoder created");

        WGPURenderPassColorAttachment color3d{};
        color3d.view = view3d;
        color3d.depthSlice = 2;
        color3d.loadOp = WGPULoadOp_Clear;
        color3d.storeOp = WGPUStoreOp_Store;
        color3d.clearValue = {0.25, 0.5, 0.75, 1.0};

        WGPURenderPassDescriptor pass3dDesc{};
        pass3dDesc.label = {"depth_slice_render_pass", WGPU_STRLEN};
        pass3dDesc.colorAttachmentCount = 1;
        pass3dDesc.colorAttachments = &color3d;
        WGPURenderPassEncoder pass3d =
            pass3dEncoder ? wgpuCommandEncoderBeginRenderPass(pass3dEncoder, &pass3dDesc) : nullptr;
        CHECK(pass3d != nullptr, "3D depthSlice render pass begun");

        if (pass3d)
        {
            CHECK(!pass3d->ownedSliceViews.empty(), "3D depthSlice owns a slice attachment view");
            if (!pass3d->ownedSliceViews.empty())
            {
                const pe::ImageViewDesc &sliceDesc = pass3d->ownedSliceViews.front()->GetDesc();
                CHECK(sliceDesc.baseMipLevel == 0, "3D depthSlice view targets mip 0");
                CHECK(sliceDesc.baseArrayLayer == 2, "3D depthSlice view targets slice 2");
                CHECK(sliceDesc.layerCount == 1, "3D depthSlice view targets one slice");
#if defined(PE_WIN32)
                if (pe::RHII.GetApi() == PE_GRAPHICS_API_DX12)
                {
                    CHECK(pe::Dx12ImageViewImpl::From(pass3d->ownedSliceViews.front())->GetKind() ==
                              pe::Dx12ImageViewKind::Rtv,
                          "DX12 3D depthSlice view is an RTV");
                }
#endif
            }
            wgpuRenderPassEncoderEnd(pass3d);
        }

        WGPUCommandBufferDescriptor pass3dCbDesc{};
        pass3dCbDesc.label = {"depth_slice_cmd", WGPU_STRLEN};
        pass3dCmdBuf = pass3dEncoder ? wgpuCommandEncoderFinish(pass3dEncoder, &pass3dCbDesc) : nullptr;
        CHECK(pass3dCmdBuf != nullptr, "3D depthSlice command buffer finished");
        CHECK(g_errors == errorsBefore3d, "3D depthSlice pass encoded without validation errors");

        if (pass3d)
            wgpuRenderPassEncoderRelease(pass3d);
        if (pass3dEncoder)
            wgpuCommandEncoderRelease(pass3dEncoder);
    }

    if (pass3dCmdBuf)
    {
        wgpuQueueSubmit(queue, 1, &pass3dCmdBuf);
        wgpuCommandBufferRelease(pass3dCmdBuf);
    }

    if (view3d)
        wgpuTextureViewRelease(view3d);
    if (tex3d)
        wgpuTextureRelease(tex3d);

    // 14. Sampled/storage-only texture zero init

    printf("\n--- 14. Sampled/Storage Texture Zero Init ---\n");

    WGPUTextureDescriptor sampledZeroDesc{};
    sampledZeroDesc.label = {"zero_init_sampled", WGPU_STRLEN};
    sampledZeroDesc.usage = WGPUTextureUsage_TextureBinding;
    sampledZeroDesc.dimension = WGPUTextureDimension_2D;
    sampledZeroDesc.size = {2, 2, 1};
    sampledZeroDesc.format = WGPUTextureFormat_RGBA8Unorm;
    sampledZeroDesc.mipLevelCount = 1;
    sampledZeroDesc.sampleCount = 1;
    WGPUTexture sampledZeroTex = wgpuDeviceCreateTexture(device, &sampledZeroDesc);
    CHECK(sampledZeroTex != nullptr, "sampled-only zero-init texture created");

    WGPUTextureViewDescriptor sampledZeroViewDesc{};
    sampledZeroViewDesc.label = {"zero_init_sampled_view", WGPU_STRLEN};
    sampledZeroViewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    sampledZeroViewDesc.dimension = WGPUTextureViewDimension_2D;
    sampledZeroViewDesc.aspect = WGPUTextureAspect_All;
    sampledZeroViewDesc.baseMipLevel = 0;
    sampledZeroViewDesc.mipLevelCount = 1;
    sampledZeroViewDesc.baseArrayLayer = 0;
    sampledZeroViewDesc.arrayLayerCount = 1;
    sampledZeroViewDesc.usage = WGPUTextureUsage_TextureBinding;
    WGPUTextureView sampledZeroView =
        sampledZeroTex ? wgpuTextureCreateView(sampledZeroTex, &sampledZeroViewDesc) : nullptr;
    CHECK(sampledZeroView != nullptr, "sampled-only zero-init view created");

    WGPUTextureDescriptor storageZeroDesc{};
    storageZeroDesc.label = {"zero_init_storage", WGPU_STRLEN};
    storageZeroDesc.usage = WGPUTextureUsage_StorageBinding;
    storageZeroDesc.dimension = WGPUTextureDimension_2D;
    storageZeroDesc.size = {2, 2, 1};
    storageZeroDesc.format = WGPUTextureFormat_R32Uint;
    storageZeroDesc.mipLevelCount = 1;
    storageZeroDesc.sampleCount = 1;
    WGPUTexture storageZeroTex = wgpuDeviceCreateTexture(device, &storageZeroDesc);
    CHECK(storageZeroTex != nullptr, "storage-only zero-init texture created");

    WGPUTextureViewDescriptor storageZeroViewDesc{};
    storageZeroViewDesc.label = {"zero_init_storage_view", WGPU_STRLEN};
    storageZeroViewDesc.format = WGPUTextureFormat_R32Uint;
    storageZeroViewDesc.dimension = WGPUTextureViewDimension_2D;
    storageZeroViewDesc.aspect = WGPUTextureAspect_All;
    storageZeroViewDesc.baseMipLevel = 0;
    storageZeroViewDesc.mipLevelCount = 1;
    storageZeroViewDesc.baseArrayLayer = 0;
    storageZeroViewDesc.arrayLayerCount = 1;
    storageZeroViewDesc.usage = WGPUTextureUsage_StorageBinding;
    WGPUTextureView storageZeroView =
        storageZeroTex ? wgpuTextureCreateView(storageZeroTex, &storageZeroViewDesc) : nullptr;
    CHECK(storageZeroView != nullptr, "storage-only zero-init view created");

    const char *zeroInitWgsl = R"(
@group(0) @binding(0) var sampled_tex: texture_2d<f32>;
@group(0) @binding(1) var storage_tex: texture_storage_2d<r32uint, read_write>;
@group(0) @binding(2) var<storage, read_write> result: array<u32>;

@compute @workgroup_size(1)
fn main() {
    let sampled_value = textureLoad(sampled_tex, vec2<i32>(0, 0), 0);
    let storage_value = textureLoad(storage_tex, vec2<i32>(0, 0));
    result[0] = select(0u, 1u, all(sampled_value == vec4<f32>(0.0, 0.0, 0.0, 0.0)));
    result[1] = select(0u, 1u, storage_value.x == 0u);
}
)";

    WGPUShaderSourceWGSL zeroInitSource{};
    zeroInitSource.chain.sType = WGPUSType_ShaderSourceWGSL;
    zeroInitSource.code = {zeroInitWgsl, WGPU_STRLEN};

    WGPUShaderModuleDescriptor zeroInitShaderDesc{};
    zeroInitShaderDesc.label = {"zero_init_shader", WGPU_STRLEN};
    zeroInitShaderDesc.nextInChain = &zeroInitSource.chain;
    WGPUShaderModule zeroInitShader = wgpuDeviceCreateShaderModule(device, &zeroInitShaderDesc);
    CHECK(zeroInitShader != nullptr, "zero-init shader module created");

    WGPUBindGroupLayoutEntry zeroInitEntries[3] = {};
    zeroInitEntries[0].binding = 0;
    zeroInitEntries[0].visibility = WGPUShaderStage_Compute;
    zeroInitEntries[0].texture.sampleType = WGPUTextureSampleType_Float;
    zeroInitEntries[0].texture.viewDimension = WGPUTextureViewDimension_2D;

    zeroInitEntries[1].binding = 1;
    zeroInitEntries[1].visibility = WGPUShaderStage_Compute;
    zeroInitEntries[1].storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
    zeroInitEntries[1].storageTexture.format = WGPUTextureFormat_R32Uint;
    zeroInitEntries[1].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    zeroInitEntries[2].binding = 2;
    zeroInitEntries[2].visibility = WGPUShaderStage_Compute;
    zeroInitEntries[2].buffer.type = WGPUBufferBindingType_Storage;
    zeroInitEntries[2].buffer.minBindingSize = sizeof(uint32_t) * 2;

    WGPUBindGroupLayoutDescriptor zeroInitBglDesc{};
    zeroInitBglDesc.label = {"zero_init_bgl", WGPU_STRLEN};
    zeroInitBglDesc.entryCount = 3;
    zeroInitBglDesc.entries = zeroInitEntries;
    WGPUBindGroupLayout zeroInitBgl = wgpuDeviceCreateBindGroupLayout(device, &zeroInitBglDesc);
    CHECK(zeroInitBgl != nullptr, "zero-init bind group layout created");

    WGPUPipelineLayoutDescriptor zeroInitPlDesc{};
    zeroInitPlDesc.label = {"zero_init_pl", WGPU_STRLEN};
    zeroInitPlDesc.bindGroupLayoutCount = 1;
    zeroInitPlDesc.bindGroupLayouts = &zeroInitBgl;
    WGPUPipelineLayout zeroInitPipelineLayout =
        zeroInitBgl ? wgpuDeviceCreatePipelineLayout(device, &zeroInitPlDesc) : nullptr;
    CHECK(zeroInitPipelineLayout != nullptr, "zero-init pipeline layout created");

    WGPUComputePipelineDescriptor zeroInitCpDesc{};
    zeroInitCpDesc.label = {"zero_init_pipeline", WGPU_STRLEN};
    zeroInitCpDesc.layout = zeroInitPipelineLayout;
    zeroInitCpDesc.compute.module = zeroInitShader;
    zeroInitCpDesc.compute.entryPoint = {"main", WGPU_STRLEN};
    const int errorsBeforeZeroInitPipeline = g_errors;
    WGPUComputePipeline zeroInitPipeline =
        (zeroInitShader && zeroInitPipelineLayout)
            ? wgpuDeviceCreateComputePipeline(device, &zeroInitCpDesc)
            : nullptr;
    if (g_errors == errorsBeforeZeroInitPipeline)
        CHECK(zeroInitPipeline != nullptr, "zero-init compute pipeline created");

    WGPUBindGroupEntry zeroInitBgEntries[3] = {};
    zeroInitBgEntries[0].binding = 0;
    zeroInitBgEntries[0].textureView = sampledZeroView;
    zeroInitBgEntries[1].binding = 1;
    zeroInitBgEntries[1].textureView = storageZeroView;
    zeroInitBgEntries[2].binding = 2;
    zeroInitBgEntries[2].buffer = outputBuf;
    zeroInitBgEntries[2].offset = 0;
    zeroInitBgEntries[2].size = sizeof(uint32_t) * 2;

    WGPUBindGroupDescriptor zeroInitBgDesc{};
    zeroInitBgDesc.label = {"zero_init_bg", WGPU_STRLEN};
    zeroInitBgDesc.layout = zeroInitBgl;
    zeroInitBgDesc.entryCount = 3;
    zeroInitBgDesc.entries = zeroInitBgEntries;
    WGPUBindGroup zeroInitBindGroup =
        (zeroInitBgl && sampledZeroView && storageZeroView)
            ? wgpuDeviceCreateBindGroup(device, &zeroInitBgDesc)
            : nullptr;
    CHECK(zeroInitBindGroup != nullptr, "zero-init bind group created");

    if (zeroInitPipeline && zeroInitBindGroup)
    {
        WGPUCommandEncoderDescriptor zeroInitCeDesc{};
        zeroInitCeDesc.label = {"zero_init_encoder", WGPU_STRLEN};
        WGPUCommandEncoder zeroInitEncoder = wgpuDeviceCreateCommandEncoder(device, &zeroInitCeDesc);
        CHECK(zeroInitEncoder != nullptr, "zero-init command encoder created");

        WGPUComputePassDescriptor zeroInitPassDesc{};
        zeroInitPassDesc.label = {"zero_init_pass", WGPU_STRLEN};
        WGPUComputePassEncoder zeroInitPass =
            zeroInitEncoder ? wgpuCommandEncoderBeginComputePass(zeroInitEncoder, &zeroInitPassDesc) : nullptr;
        CHECK(zeroInitPass != nullptr, "zero-init compute pass begun");

        if (zeroInitPass)
        {
            wgpuComputePassEncoderSetPipeline(zeroInitPass, zeroInitPipeline);
            wgpuComputePassEncoderSetBindGroup(zeroInitPass, 0, zeroInitBindGroup, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(zeroInitPass, 1, 1, 1);
            wgpuComputePassEncoderEnd(zeroInitPass);
        }

        if (zeroInitEncoder)
            wgpuCommandEncoderCopyBufferToBuffer(zeroInitEncoder, outputBuf, 0, readbackBuf, 0, sizeof(uint32_t) * 2);

        WGPUCommandBufferDescriptor zeroInitCbDesc{};
        zeroInitCbDesc.label = {"zero_init_cmd", WGPU_STRLEN};
        WGPUCommandBuffer zeroInitCmdBuf =
            zeroInitEncoder ? wgpuCommandEncoderFinish(zeroInitEncoder, &zeroInitCbDesc) : nullptr;
        CHECK(zeroInitCmdBuf != nullptr, "zero-init command buffer finished");

        if (zeroInitCmdBuf)
        {
            wgpuQueueSubmit(queue, 1, &zeroInitCmdBuf);

            mapDone = false;
            WGPUFuture zeroInitMapFuture =
                wgpuBufferMapAsync(readbackBuf, WGPUMapMode_Read, 0, sizeof(uint32_t) * 2, mapCb);
            WGPUFutureWaitInfo zeroInitWaitInfo{zeroInitMapFuture, WGPU_FALSE};
            wgpuInstanceWaitAny(instance, 1, &zeroInitWaitInfo, UINT64_MAX);
            CHECK(mapDone, "zero-init readback buffer map succeeded");

            if (mapDone)
            {
                const void *data =
                    wgpuBufferGetConstMappedRange(readbackBuf, 0, sizeof(uint32_t) * 2);
                CHECK(data != nullptr, "zero-init readback mapped range non-null");

                if (data)
                {
                    const auto *results = static_cast<const uint32_t *>(data);
                    CHECK(results[0] == 1, "sampled-only texture reads as zero");
                    CHECK(results[1] == 1, "storage-only texture reads as zero");
                }
            }
            wgpuBufferUnmap(readbackBuf);
        }

        if (zeroInitCmdBuf)
            wgpuCommandBufferRelease(zeroInitCmdBuf);
        if (zeroInitPass)
            wgpuComputePassEncoderRelease(zeroInitPass);
        if (zeroInitEncoder)
            wgpuCommandEncoderRelease(zeroInitEncoder);
    }

    if (zeroInitBindGroup)
        wgpuBindGroupRelease(zeroInitBindGroup);
    if (zeroInitPipeline)
        wgpuComputePipelineRelease(zeroInitPipeline);
    if (zeroInitPipelineLayout)
        wgpuPipelineLayoutRelease(zeroInitPipelineLayout);
    if (zeroInitBgl)
        wgpuBindGroupLayoutRelease(zeroInitBgl);
    if (zeroInitShader)
        wgpuShaderModuleRelease(zeroInitShader);
    if (storageZeroView)
        wgpuTextureViewRelease(storageZeroView);
    if (storageZeroTex)
        wgpuTextureRelease(storageZeroTex);
    if (sampledZeroView)
        wgpuTextureViewRelease(sampledZeroView);
    if (sampledZeroTex)
        wgpuTextureRelease(sampledZeroTex);

    // 15. Multi-kind texture view descriptors

    printf("\n--- 15. Multi-kind Texture View ---\n");

    WGPUTextureDescriptor multiTexDesc{};
    multiTexDesc.label = {"multi_kind_texture", WGPU_STRLEN};
    multiTexDesc.usage = WGPUTextureUsage_TextureBinding |
                         WGPUTextureUsage_StorageBinding |
                         WGPUTextureUsage_RenderAttachment;
    multiTexDesc.dimension = WGPUTextureDimension_2D;
    multiTexDesc.size = {1, 1, 1};
    multiTexDesc.format = WGPUTextureFormat_RGBA8Unorm;
    multiTexDesc.mipLevelCount = 1;
    multiTexDesc.sampleCount = 1;
    WGPUTexture multiTex = wgpuDeviceCreateTexture(device, &multiTexDesc);
    CHECK(multiTex != nullptr, "multi-kind texture created");

    WGPUTextureViewDescriptor multiViewDesc{};
    multiViewDesc.label = {"multi_kind_view", WGPU_STRLEN};
    multiViewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    multiViewDesc.dimension = WGPUTextureViewDimension_2D;
    multiViewDesc.aspect = WGPUTextureAspect_All;
    multiViewDesc.baseMipLevel = 0;
    multiViewDesc.mipLevelCount = 1;
    multiViewDesc.baseArrayLayer = 0;
    multiViewDesc.arrayLayerCount = 1;
    multiViewDesc.usage = multiTexDesc.usage;
    WGPUTextureView multiView = multiTex ? wgpuTextureCreateView(multiTex, &multiViewDesc) : nullptr;
    CHECK(multiView != nullptr, "multi-kind texture view created");

#if defined(PE_WIN32)
    if (pe::RHII.GetApi() == PE_GRAPHICS_API_DX12 && multiView)
    {
        pe::ImageView *srvView = pwgpu::TextureBindingImageView(multiView);
        pe::ImageView *uavView = pwgpu::StorageBindingImageView(multiView);
        pe::ImageView *rtvView = pwgpu::RenderAttachmentImageView(multiView);
        CHECK(srvView != nullptr, "DX12 multi-kind view has SRV descriptor");
        CHECK(uavView != nullptr, "DX12 multi-kind view has UAV descriptor");
        CHECK(rtvView != nullptr, "DX12 multi-kind view has RTV descriptor");
        CHECK(srvView != uavView && srvView != rtvView && uavView != rtvView,
              "DX12 multi-kind descriptors are distinct");
        if (srvView && uavView && rtvView)
        {
            CHECK(pe::Dx12ImageViewImpl::From(srvView)->GetKind() == pe::Dx12ImageViewKind::Srv,
                  "DX12 texture binding selects SRV");
            CHECK(pe::Dx12ImageViewImpl::From(uavView)->GetKind() == pe::Dx12ImageViewKind::Uav,
                  "DX12 storage binding selects UAV");
            CHECK(pe::Dx12ImageViewImpl::From(rtvView)->GetKind() == pe::Dx12ImageViewKind::Rtv,
                  "DX12 render attachment selects RTV");
        }
    }
#endif

    if (multiView)
        wgpuTextureViewRelease(multiView);
    if (multiTex)
        wgpuTextureRelease(multiTex);

    // 16. Cleanup

    printf("\n--- 16. Cleanup ---\n");

    wgpuDeviceDestroy(device);
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
