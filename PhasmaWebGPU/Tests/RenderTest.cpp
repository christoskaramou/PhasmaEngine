#include <webgpu/webgpu.h>
#include "Base/Log.h"
#include "Base/Path.h"
#include "Base/EventSystem.h"
#include "API/GraphicsApiSelection.h"
#include "API/RHI.h"
#include "Common/RunOptions.h"
#include <SDL.h>
#include <cstring>
#include <cmath>

// clang-format off
static const uint32_t kTriangleVS[] = {
    0x07230203, 0x00010000, 0x000e0000, 0x00000022, 0x00000000, 0x00020011, 0x00000001, 0x0003000e,
    0x00000000, 0x00000001, 0x0009000f, 0x00000000, 0x00000001, 0x614d5356, 0x00006e69, 0x00000002,
    0x00000003, 0x00000004, 0x00000005, 0x00030003, 0x00000005, 0x00000258, 0x00050005, 0x00000006,
    0x65707974, 0x4f42552e, 0x00000000, 0x00040006, 0x00000006, 0x00000000, 0x0070766d, 0x00030005,
    0x00000007, 0x004f4255, 0x00060005, 0x00000002, 0x762e6e69, 0x502e7261, 0x5449534f, 0x004e4f49,
    0x00060005, 0x00000003, 0x762e6e69, 0x432e7261, 0x524f4c4f, 0x00000000, 0x00060005, 0x00000005,
    0x2e74756f, 0x2e726176, 0x4f4c4f43, 0x00000052, 0x00040005, 0x00000001, 0x614d5356, 0x00006e69,
    0x00040047, 0x00000004, 0x0000000b, 0x00000000, 0x00040047, 0x00000002, 0x0000001e, 0x00000000,
    0x00040047, 0x00000003, 0x0000001e, 0x00000001, 0x00040047, 0x00000005, 0x0000001e, 0x00000000,
    0x00040047, 0x00000007, 0x00000022, 0x00000000, 0x00040047, 0x00000007, 0x00000021, 0x00000000,
    0x00050048, 0x00000006, 0x00000000, 0x00000023, 0x00000000, 0x00050048, 0x00000006, 0x00000000,
    0x00000007, 0x00000010, 0x00040048, 0x00000006, 0x00000000, 0x00000004, 0x00030047, 0x00000006,
    0x00000002, 0x00040015, 0x00000008, 0x00000020, 0x00000001, 0x0004002b, 0x00000008, 0x00000009,
    0x00000000, 0x00030016, 0x0000000a, 0x00000020, 0x0004002b, 0x0000000a, 0x0000000b, 0x00000000,
    0x0004002b, 0x0000000a, 0x0000000c, 0x3f800000, 0x00040017, 0x0000000d, 0x0000000a, 0x00000004,
    0x00040018, 0x0000000e, 0x0000000d, 0x00000004, 0x0003001e, 0x00000006, 0x0000000e, 0x00040020,
    0x0000000f, 0x00000002, 0x00000006, 0x00040017, 0x00000010, 0x0000000a, 0x00000002, 0x00040020,
    0x00000011, 0x00000001, 0x00000010, 0x00040017, 0x00000012, 0x0000000a, 0x00000003, 0x00040020,
    0x00000013, 0x00000001, 0x00000012, 0x00040020, 0x00000014, 0x00000003, 0x0000000d, 0x00040020,
    0x00000015, 0x00000003, 0x00000012, 0x00020013, 0x00000016, 0x00030021, 0x00000017, 0x00000016,
    0x00040020, 0x00000018, 0x00000002, 0x0000000e, 0x0004003b, 0x0000000f, 0x00000007, 0x00000002,
    0x0004003b, 0x00000011, 0x00000002, 0x00000001, 0x0004003b, 0x00000013, 0x00000003, 0x00000001,
    0x0004003b, 0x00000014, 0x00000004, 0x00000003, 0x0004003b, 0x00000015, 0x00000005, 0x00000003,
    0x00050036, 0x00000016, 0x00000001, 0x00000000, 0x00000017, 0x000200f8, 0x00000019, 0x0004003d,
    0x00000010, 0x0000001a, 0x00000002, 0x0004003d, 0x00000012, 0x0000001b, 0x00000003, 0x00050041,
    0x00000018, 0x0000001c, 0x00000007, 0x00000009, 0x0004003d, 0x0000000e, 0x0000001d, 0x0000001c,
    0x00050051, 0x0000000a, 0x0000001e, 0x0000001a, 0x00000000, 0x00050051, 0x0000000a, 0x0000001f,
    0x0000001a, 0x00000001, 0x00070050, 0x0000000d, 0x00000020, 0x0000001e, 0x0000001f, 0x0000000b,
    0x0000000c, 0x00050090, 0x0000000d, 0x00000021, 0x00000020, 0x0000001d, 0x0003003e, 0x00000004,
    0x00000021, 0x0003003e, 0x00000005, 0x0000001b, 0x000100fd, 0x00010038,
};
// clang-format on
static const size_t kTriangleVSSize = sizeof(kTriangleVS) / sizeof(uint32_t);

// clang-format off
static const uint32_t kTrianglePS[] = {
    0x07230203, 0x00010000, 0x000e0000, 0x00000012, 0x00000000, 0x00020011, 0x00000001, 0x0003000e,
    0x00000000, 0x00000001, 0x0007000f, 0x00000004, 0x00000001, 0x614d5350, 0x00006e69, 0x00000002,
    0x00000003, 0x00030010, 0x00000001, 0x00000007, 0x00030003, 0x00000005, 0x00000258, 0x00060005,
    0x00000002, 0x762e6e69, 0x432e7261, 0x524f4c4f, 0x00000000, 0x00070005, 0x00000003, 0x2e74756f,
    0x2e726176, 0x545f5653, 0x65677261, 0x00003074, 0x00040005, 0x00000001, 0x614d5350, 0x00006e69,
    0x00040047, 0x00000002, 0x0000001e, 0x00000000, 0x00040047, 0x00000003, 0x0000001e, 0x00000000,
    0x00030016, 0x00000004, 0x00000020, 0x0004002b, 0x00000004, 0x00000005, 0x3f800000, 0x00040017,
    0x00000006, 0x00000004, 0x00000004, 0x00040017, 0x00000007, 0x00000004, 0x00000003, 0x00040020,
    0x00000008, 0x00000001, 0x00000007, 0x00040020, 0x00000009, 0x00000003, 0x00000006, 0x00020013,
    0x0000000a, 0x00030021, 0x0000000b, 0x0000000a, 0x0004003b, 0x00000008, 0x00000002, 0x00000001,
    0x0004003b, 0x00000009, 0x00000003, 0x00000003, 0x00050036, 0x0000000a, 0x00000001, 0x00000000,
    0x0000000b, 0x000200f8, 0x0000000c, 0x0004003d, 0x00000007, 0x0000000d, 0x00000002, 0x00050051,
    0x00000004, 0x0000000e, 0x0000000d, 0x00000000, 0x00050051, 0x00000004, 0x0000000f, 0x0000000d,
    0x00000001, 0x00050051, 0x00000004, 0x00000010, 0x0000000d, 0x00000002, 0x00070050, 0x00000006,
    0x00000011, 0x0000000e, 0x0000000f, 0x00000010, 0x00000005, 0x0003003e, 0x00000003, 0x00000011,
    0x000100fd, 0x00010038,
};
// clang-format on
static const size_t kTrianglePSSize = sizeof(kTrianglePS) / sizeof(uint32_t);

struct Vertex
{
    float pos[2];
    float color[3];
};

static const Vertex kTriangleVertices[] = {
    {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}}, // top, red
    {{-0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}}, // bottom-left, green
    {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},  // bottom-right, blue
};

static void mat4Identity(float m[16])
{
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4RotZ(float m[16], float radians)
{
    mat4Identity(m);
    float c = std::cos(radians);
    float s = std::sin(radians);
    m[0] = c;
    m[1] = s;
    m[4] = -s;
    m[5] = c;
}

static void uncapturedError(WGPUDevice const * /*device*/, WGPUErrorType type,
                            WGPUStringView message, void * /*u1*/, void * /*u2*/)
{
    fprintf(stderr, "[WebGPU error type=%d] %.*s\n",
            static_cast<int>(type),
            message.data ? static_cast<int>(message.length) : 0,
            message.data ? message.data : "");
}

int main(int argc, char *argv[])
{
    pe::Log::Init();
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("=== PhasmaWebGPU Render Test ===\n\n");

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

    const int kWidth = 800, kHeight = 600;
    uint32_t windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
    if (api == PE_GRAPHICS_API_DX12)
        windowFlags &= ~SDL_WINDOW_VULKAN;
    SDL_Window *window = SDL_CreateWindow("WebGPU Render Test",
                                          SDL_WINDOWPOS_CENTERED_DISPLAY(runOptions.displayIndex),
                                          SDL_WINDOWPOS_CENTERED_DISPLAY(runOptions.displayIndex),
                                          kWidth,
                                          kHeight,
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

    WGPUInstance instance = wgpuCreateInstance(nullptr);

    WGPUAdapter adapter = nullptr;
    {
        WGPURequestAdapterCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowSpontaneous;
        cb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter a,
                         WGPUStringView, void *u1, void *)
        { *static_cast<WGPUAdapter *>(u1) = (status == WGPURequestAdapterStatus_Success) ? a : nullptr; };
        cb.userdata1 = &adapter;
        wgpuInstanceRequestAdapter(instance, nullptr, cb);
    }
    if (!adapter)
    {
        fprintf(stderr, "Failed to get adapter\n");
        return 1;
    }

    WGPUDevice device = nullptr;
    {
        WGPUUncapturedErrorCallbackInfo errCb{};
        errCb.callback = uncapturedError;
        WGPUDeviceDescriptor devDesc{};
        devDesc.uncapturedErrorCallbackInfo = errCb;
        WGPURequestDeviceCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_AllowSpontaneous;
        cb.callback = [](WGPURequestDeviceStatus status, WGPUDevice d,
                         WGPUStringView, void *u1, void *)
        { *static_cast<WGPUDevice *>(u1) = (status == WGPURequestDeviceStatus_Success) ? d : nullptr; };
        cb.userdata1 = &device;
        wgpuAdapterRequestDevice(adapter, &devDesc, cb);
    }
    if (!device)
    {
        fprintf(stderr, "Failed to get device\n");
        return 1;
    }
    WGPUQueue queue = wgpuDeviceGetQueue(device);

    WGPUSurface surface = wgpuInstanceCreateSurface(instance, nullptr);
    if (!surface)
    {
        fprintf(stderr, "Failed to create surface\n");
        return 1;
    }

    WGPUSurfaceCapabilities caps{};
    wgpuSurfaceGetCapabilities(surface, adapter, &caps);
    WGPUTextureFormat swapchainFormat = (caps.formatCount > 0) ? caps.formats[0] : WGPUTextureFormat_BGRA8Unorm;
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    WGPUSurfaceConfiguration surfConfig{};
    surfConfig.device = device;
    surfConfig.format = swapchainFormat;
    surfConfig.usage = WGPUTextureUsage_RenderAttachment;
    surfConfig.width = kWidth;
    surfConfig.height = kHeight;
    surfConfig.presentMode = WGPUPresentMode_Fifo;
    surfConfig.alphaMode = WGPUCompositeAlphaMode_Opaque;
    wgpuSurfaceConfigure(surface, &surfConfig);
    printf("[Surface] Configured %dx%d\n", kWidth, kHeight);

    WGPUBufferDescriptor vbDesc{};
    vbDesc.label = {"vertex_buffer", WGPU_STRLEN};
    vbDesc.size = sizeof(kTriangleVertices);
    vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device, &vbDesc);
    wgpuQueueWriteBuffer(queue, vertexBuffer, 0, kTriangleVertices, sizeof(kTriangleVertices));

    WGPUBufferDescriptor ubDesc{};
    ubDesc.label = {"uniform_buffer", WGPU_STRLEN};
    ubDesc.size = 64;
    ubDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device, &ubDesc);

    WGPUBindGroupLayoutEntry bglEntry{};
    bglEntry.binding = 0;
    bglEntry.visibility = WGPUShaderStage_Vertex;
    bglEntry.buffer.type = WGPUBufferBindingType_Uniform;
    bglEntry.buffer.minBindingSize = 64;

    WGPUBindGroupLayoutDescriptor bglDesc{};
    bglDesc.label = {"mvp_bgl", WGPU_STRLEN};
    bglDesc.entryCount = 1;
    bglDesc.entries = &bglEntry;
    WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);

    WGPUPipelineLayoutDescriptor plDesc{};
    plDesc.label = {"triangle_pl", WGPU_STRLEN};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &bgl;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

    WGPUBindGroupEntry bgEntry{};
    bgEntry.binding = 0;
    bgEntry.buffer = uniformBuffer;
    bgEntry.offset = 0;
    bgEntry.size = 64;

    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.label = {"mvp_bg", WGPU_STRLEN};
    bgDesc.layout = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);

    WGPUShaderSourceSPIRV vsSpirvSource{};
    vsSpirvSource.chain.sType = WGPUSType_ShaderSourceSPIRV;
    vsSpirvSource.code = kTriangleVS;
    vsSpirvSource.codeSize = static_cast<uint32_t>(kTriangleVSSize);

    WGPUShaderModuleDescriptor vsSmDesc{};
    vsSmDesc.label = {"triangle_vs", WGPU_STRLEN};
    vsSmDesc.nextInChain = &vsSpirvSource.chain;
    WGPUShaderModule vsModule = wgpuDeviceCreateShaderModule(device, &vsSmDesc);

    WGPUShaderSourceSPIRV psSpirvSource{};
    psSpirvSource.chain.sType = WGPUSType_ShaderSourceSPIRV;
    psSpirvSource.code = kTrianglePS;
    psSpirvSource.codeSize = static_cast<uint32_t>(kTrianglePSSize);

    WGPUShaderModuleDescriptor psSmDesc{};
    psSmDesc.label = {"triangle_ps", WGPU_STRLEN};
    psSmDesc.nextInChain = &psSpirvSource.chain;
    WGPUShaderModule psModule = wgpuDeviceCreateShaderModule(device, &psSmDesc);

    WGPUVertexAttribute vertexAttrs[2] = {};
    vertexAttrs[0].format = WGPUVertexFormat_Float32x2;
    vertexAttrs[0].offset = offsetof(Vertex, pos);
    vertexAttrs[0].shaderLocation = 0;
    vertexAttrs[1].format = WGPUVertexFormat_Float32x3;
    vertexAttrs[1].offset = offsetof(Vertex, color);
    vertexAttrs[1].shaderLocation = 1;

    WGPUVertexBufferLayout vertexBufLayout{};
    vertexBufLayout.arrayStride = sizeof(Vertex);
    vertexBufLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufLayout.attributeCount = 2;
    vertexBufLayout.attributes = vertexAttrs;

    WGPUColorTargetState colorTarget{};
    colorTarget.format = swapchainFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState{};
    fragmentState.module = psModule;
    fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor rpDesc{};
    rpDesc.label = {"triangle_pipeline", WGPU_STRLEN};
    rpDesc.layout = pipelineLayout;
    rpDesc.vertex.module = vsModule;
    rpDesc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
    rpDesc.vertex.bufferCount = 1;
    rpDesc.vertex.buffers = &vertexBufLayout;
    rpDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rpDesc.primitive.frontFace = WGPUFrontFace_CCW;
    rpDesc.primitive.cullMode = WGPUCullMode_None;
    rpDesc.multisample.count = 1;
    rpDesc.multisample.mask = 0xFFFFFFFF;
    rpDesc.fragment = &fragmentState;

    WGPURenderPipeline renderPipeline = wgpuDeviceCreateRenderPipeline(device, &rpDesc);
    if (!renderPipeline)
    {
        fprintf(stderr, "Failed to create render pipeline\n");
        return 1;
    }
    printf("[Pipeline] Render pipeline created\n\n");

    // Render loop

    printf("Rendering... close the window to exit.\n");

    Uint64 startTicks = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();
    bool running = true;
    uint64_t frameCount = 0;

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                running = false;
        }
        if (!running)
            break;

        // Update uniform: rotation matrix
        double elapsed = static_cast<double>(SDL_GetPerformanceCounter() - startTicks) / static_cast<double>(freq);
        float angle = static_cast<float>(elapsed) * 1.5708f; // ~90 deg/s → full rotation in ~4s
        float mvp[16];
        mat4RotZ(mvp, angle);
        wgpuQueueWriteBuffer(queue, uniformBuffer, 0, mvp, sizeof(mvp));

        // Acquire swapchain texture
        WGPUSurfaceTexture surfTex{};
        wgpuSurfaceGetCurrentTexture(surface, &surfTex);
        if (!surfTex.texture || surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal)
        {
            if (surfTex.texture)
                wgpuTextureRelease(surfTex.texture);
            continue;
        }

        // Create view for the swapchain texture
        WGPUTextureViewDescriptor tvDesc{};
        tvDesc.format = swapchainFormat;
        tvDesc.dimension = WGPUTextureViewDimension_2D;
        tvDesc.baseMipLevel = 0;
        tvDesc.mipLevelCount = 1;
        tvDesc.baseArrayLayer = 0;
        tvDesc.arrayLayerCount = 1;
        tvDesc.aspect = WGPUTextureAspect_All;
        WGPUTextureView swapView = wgpuTextureCreateView(surfTex.texture, &tvDesc);

        // Command encoder
        WGPUCommandEncoderDescriptor ceDesc{};
        ceDesc.label = {"frame_encoder", WGPU_STRLEN};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &ceDesc);

        // Render pass
        WGPURenderPassColorAttachment colorAtt{};
        colorAtt.view = swapView;
        colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAtt.loadOp = WGPULoadOp_Clear;
        colorAtt.storeOp = WGPUStoreOp_Store;
        colorAtt.clearValue = {0.1, 0.1, 0.1, 1.0};

        WGPURenderPassDescriptor passDesc{};
        passDesc.label = {"frame_pass", WGPU_STRLEN};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAtt;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, renderPipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, sizeof(kTriangleVertices));
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);

        // Finish and submit
        WGPUCommandBufferDescriptor cbDesc{};
        cbDesc.label = {"frame_cmdbuf", WGPU_STRLEN};
        WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &cbDesc);
        wgpuQueueSubmit(queue, 1, &cmdBuf);

        // Present
        wgpuSurfacePresent(surface);

        // Release per-frame objects
        wgpuCommandBufferRelease(cmdBuf);
        wgpuRenderPassEncoderRelease(pass);
        wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(swapView);
        wgpuTextureRelease(surfTex.texture);

        ++frameCount;
        if (runOptions.exitAfterFrames > 0 && frameCount >= runOptions.exitAfterFrames)
            running = false;
        if (runOptions.exitAfterSeconds > 0.0 && elapsed >= runOptions.exitAfterSeconds)
            running = false;
    }

    // Cleanup

    printf("\nCleaning up...\n");
    wgpuDeviceDestroy(device);

    wgpuRenderPipelineRelease(renderPipeline);
    wgpuShaderModuleRelease(psModule);
    wgpuShaderModuleRelease(vsModule);
    wgpuBindGroupRelease(bindGroup);
    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuBufferRelease(uniformBuffer);
    wgpuBufferRelease(vertexBuffer);
    wgpuSurfaceUnconfigure(surface);
    wgpuSurfaceRelease(surface);
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);

    pe::RHII.Destroy();
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("=== Done ===\n");
    return 0;
}
