// WebGPU RHI backend, P2: shaders from the desktop bake (docs/wiki/superpowers/specs/2026-11-06-webgpu-rhi-backend-design.md,
// decision 7). .testpool/tests/web_rhi_shaders.py bakes Shaders/*.hlsl with the desktop Vulkan player, converts them,
// and writes the sources and ShaderCache files into MEMFS before main. Prints "[shaders] PASS ..." / "[shaders] FAIL ..."
// and "[shaders] done: N failures, M WebGPU errors". Build: the wasm-player preset, target RhiShaders (Emscripten only).
#include "API/Buffer.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/Shader.h"
#include "API/WebGPU/WebGpuBufferImpl.h"
#include "API/WebGPU/WebGpuDescriptorImpl.h"
#include "API/WebGPU/WebGpuImageImpl.h"
#include "API/WebGPU/WebGpuRhiImpl.h"
#include "API/WebGPU/WebGpuShaderImpl.h"
#include "Base/EventSystem.h"

#include <cstdio>

using namespace pe;

namespace
{
    constexpr uint32_t kCount = 64;
    constexpr float kScale = 2.0f;
    constexpr float kBias = 1.0f;
    constexpr float kPushOffset = 0.5f;

    int s_failures = 0;

    void Check(bool ok, const char *what)
    {
        std::printf("[shaders] %s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok)
            s_failures++;
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

    WGPUDevice Device()
    {
        return WebGpuRhiImpl::Get()->m_device;
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

    Shader *Load(const char *file, PeShaderStageFlags stage, const char *entry)
    {
        return Shader::Create({.sourcePath = std::string("/RhiShaders/") + file, .entryPoint = entry, .stage = stage});
    }

    const WebGpuBindingFacts *Facts(const std::vector<Descriptor *> &sets, uint32_t set, uint32_t binding)
    {
        if (set >= sets.size() || !sets[set])
            return nullptr;

        for (const DescriptorBindingInfo &info : sets[set]->GetBindingInfos())
        {
            if (info.binding == binding)
                return &info.webgpu;
        }

        return nullptr;
    }

    bool IsTexture(const WebGpuBindingFacts *f, uint32_t dimension, uint32_t sampleType, bool multisampled = false)
    {
        return f && f->viewDimension == dimension && f->sampleType == sampleType && f->multisampled == multisampled;
    }

    // Prints every message Chrome's WGSL compiler reports and returns how many are errors.
    uint32_t CompilationErrors(WGPUShaderModule module, const std::string &name)
    {
        struct Result
        {
            const std::string *name = nullptr;
            uint32_t errors = 0;
        } result{&name};

        WGPUCompilationInfoCallbackInfo cb{};
        cb.mode = WGPUCallbackMode_WaitAnyOnly;
        cb.userdata1 = &result;
        cb.callback = [](WGPUCompilationInfoRequestStatus, const WGPUCompilationInfo *info, void *userdata, void *)
        {
            auto *r = static_cast<Result *>(userdata);
            for (size_t i = 0; info && i < info->messageCount; ++i)
            {
                const WGPUCompilationMessage &m = info->messages[i];
                std::printf("[shaders] %s line %llu: %.*s\n",
                            r->name->c_str(),
                            static_cast<unsigned long long>(m.lineNum),
                            static_cast<int>(m.message.length),
                            m.message.data);
                if (m.type == WGPUCompilationMessageType_Error)
                    r->errors++;
            }
        };
        WaitFuture(wgpuShaderModuleGetCompilationInfo(module, cb));
        return result.errors;
    }

    // Engine sets 0-2 from reflection, an empty layout for a set the pass lacks, and the push slot at group 3
    // (decision 8; the backend's own ring arrives with pipelines, so the test makes a plain uniform here).
    struct PipelineLayout
    {
        WGPUBindGroupLayout empty = nullptr;
        WGPUBindGroupLayout push = nullptr;
        WGPUPipelineLayout layout = nullptr;
    };

    PipelineLayout MakePipelineLayout(const std::vector<Descriptor *> &sets)
    {
        PipelineLayout out{};
        WGPUBindGroupLayoutDescriptor emptyDesc{};
        out.empty = wgpuDeviceCreateBindGroupLayout(Device(), &emptyDesc);

        WGPUBindGroupLayoutEntry push{};
        push.binding = 0;
        push.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
        push.buffer.type = WGPUBufferBindingType_Uniform;
        WGPUBindGroupLayoutDescriptor pushDesc{};
        pushDesc.entryCount = 1;
        pushDesc.entries = &push;
        out.push = wgpuDeviceCreateBindGroupLayout(Device(), &pushDesc);

        WGPUBindGroupLayout groups[4] = {out.empty, out.empty, out.empty, out.push};
        for (size_t i = 0; i < sets.size() && i < 3; ++i)
        {
            if (sets[i])
                groups[i] = WebGpuDescriptorLayoutImpl::From(sets[i]->GetLayout())->m_layout;
        }

        WGPUPipelineLayoutDescriptor desc{};
        desc.bindGroupLayoutCount = 4;
        desc.bindGroupLayouts = groups;
        out.layout = wgpuDeviceCreatePipelineLayout(Device(), &desc);
        return out;
    }

    void ReleaseLayout(PipelineLayout &layout)
    {
        wgpuPipelineLayoutRelease(layout.layout);
        wgpuBindGroupLayoutRelease(layout.push);
        wgpuBindGroupLayoutRelease(layout.empty);
    }

    void TestMissingBake()
    {
        // A source with no baked module: the browser cannot compile it, so it is an error, not a silent fallback.
        {
            std::ofstream file("/RhiShaders/Unbaked.hlsl");
            file << "float4 mainPS() : SV_Target0 { return 1.0; }\n";
        }

        Check(Throws([]
                     { Load("Unbaked.hlsl", PE_SHADER_STAGE_FRAGMENT, "mainPS"); }),
              "a shader without a baked module is an error");
    }

    void TestComputeReflection(Shader *cs, const std::vector<Descriptor *> &sets)
    {
        Check(cs->GetPushConstantDesc().size == sizeof(float), "compute: engine reflection keeps the push block");

        const WebGpuBindingFacts *input = Facts(sets, 0, 0);
        const WebGpuBindingFacts *output = Facts(sets, 0, 1);
        const WebGpuBindingFacts *params = Facts(sets, 0, 2);
        const WebGpuBindingFacts *target = Facts(sets, 0, 3);
        Check(input && input->buffer == WGPUBufferBindingType_ReadOnlyStorage,
              "compute: StructuredBuffer is read-only storage");
        Check(output && output->buffer == WGPUBufferBindingType_Storage,
              "compute: written RWStructuredBuffer is storage");
        Check(params && params->buffer == WGPUBufferBindingType_Uniform, "compute: ConstantBuffer is uniform");

        const bool storage = target && target->storageFormat == WGPUTextureFormat_RGBA8Unorm &&
                             target->storageAccess == WGPUStorageTextureAccess_WriteOnly &&
                             target->viewDimension == WGPUTextureViewDimension_2D;
        Check(storage, "compute: written RWTexture2D is a write-only rgba8unorm 2D storage texture");
    }

    // The vertex and fragment stages merge into one layout per set (Shader::ReflectPassDescriptors).
    void TestGraphicsReflection(const std::vector<Descriptor *> &sets)
    {
        const WebGpuBindingFacts *albedo = Facts(sets, 0, 0);
        const WebGpuBindingFacts *shadowSampler = Facts(sets, 0, 5);
        const WebGpuBindingFacts *positions = Facts(sets, 1, 0);

        Check(IsTexture(albedo, WGPUTextureViewDimension_2D, WGPUTextureSampleType_Float) &&
                  albedo->sampler == WGPUSamplerBindingType_Filtering,
              "fragment: combined Texture2D is a 2D float texture under a filtering sampler");
        Check(IsTexture(Facts(sets, 0, 1), WGPUTextureViewDimension_Cube, WGPUTextureSampleType_Float),
              "fragment: combined TextureCube is a cube texture");
        Check(IsTexture(Facts(sets, 0, 2), WGPUTextureViewDimension_2DArray, WGPUTextureSampleType_UnfilterableFloat),
              "fragment: a loaded Texture2DArray is an unfilterable 2D array");
        Check(IsTexture(Facts(sets, 0, 3), WGPUTextureViewDimension_2D, WGPUTextureSampleType_Uint),
              "fragment: Texture2D<uint> is a uint texture");
        Check(IsTexture(Facts(sets, 0, 4), WGPUTextureViewDimension_2D, WGPUTextureSampleType_Depth),
              "fragment: a compared texture is a depth texture");
        Check(shadowSampler && shadowSampler->sampler == WGPUSamplerBindingType_Comparison,
              "fragment: SamplerComparisonState is a comparison sampler");
        Check(IsTexture(Facts(sets, 0, 6), WGPUTextureViewDimension_2D, WGPUTextureSampleType_UnfilterableFloat, true),
              "fragment: Texture2DMS is multisampled");
        Check(positions && positions->buffer == WGPUBufferBindingType_ReadOnlyStorage,
              "vertex + fragment: the shared StructuredBuffer merges to one read-only storage entry");
    }

    void TestModules(const std::vector<Shader *> &shaders)
    {
        for (Shader *shader : shaders)
        {
            WGPUShaderModule module = WebGpuShaderImpl::From(shader)->GetOrCreateModule();
            const std::string name = shader->GetEntryName();
            const bool ok = module && CompilationErrors(module, name) == 0;
            Check(ok, (name + ": the baked WGSL compiles in Chrome with no errors").c_str());
        }
    }

    WGPURenderPipeline MakeRenderPipeline(Shader *vs, Shader *ps, WGPUPipelineLayout layout)
    {
        const std::string vsEntry = vs->GetEntryName();
        const std::string psEntry = ps->GetEntryName();
        WGPUColorTargetState target{};
        target.format = WGPUTextureFormat_RGBA8Unorm;
        target.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment{};
        fragment.module = WebGpuShaderImpl::From(ps)->GetOrCreateModule();
        fragment.entryPoint = {psEntry.data(), psEntry.size()};
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor desc{};
        desc.layout = layout;
        desc.vertex.module = WebGpuShaderImpl::From(vs)->GetOrCreateModule();
        desc.vertex.entryPoint = {vsEntry.data(), vsEntry.size()};
        desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        desc.primitive.cullMode = WGPUCullMode_Back;
        desc.multisample.count = 1;
        desc.multisample.mask = 0xFFFFFFFF;
        desc.fragment = &fragment;
        return wgpuDeviceCreateRenderPipeline(Device(), &desc);
    }

    WGPUComputePipeline MakeComputePipeline(Shader *cs, WGPUPipelineLayout layout)
    {
        const std::string entry = cs->GetEntryName();
        WGPUComputePipelineDescriptor desc{};
        desc.layout = layout;
        desc.compute.module = WebGpuShaderImpl::From(cs)->GetOrCreateModule();
        desc.compute.entryPoint = {entry.data(), entry.size()};
        return wgpuDeviceCreateComputePipeline(Device(), &desc);
    }

    // Reads `size` bytes of `source` back through a mappable copy (readback is off in the engine on the web).
    std::vector<uint8_t> ReadBuffer(WGPUBuffer source, uint64_t size)
    {
        WGPUBufferDescriptor desc{};
        desc.size = size;
        desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        WGPUBuffer readback = wgpuDeviceCreateBuffer(Device(), &desc);

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(Device(), nullptr);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, source, 0, readback, 0, size);
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(WebGpuRhiImpl::Get()->m_queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuCommandEncoderRelease(encoder);

        WGPUBufferMapCallbackInfo mapCb{};
        mapCb.mode = WGPUCallbackMode_WaitAnyOnly;
        mapCb.callback = [](WGPUMapAsyncStatus, WGPUStringView, void *, void *) {};
        WaitFuture(wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, size, mapCb));

        const auto *data = static_cast<const uint8_t *>(wgpuBufferGetConstMappedRange(readback, 0, size));
        std::vector<uint8_t> out(data, data + size);
        wgpuBufferUnmap(readback);
        wgpuBufferRelease(readback);
        return out;
    }

    Buffer *MakeHostBuffer(size_t size, PeBufferUsageFlags usage, const void *data, const char *name)
    {
        Buffer *buffer = Buffer::Create({.size = size,
                                         .usage = usage,
                                         .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                                         .name = name});
        buffer->Map();
        std::memcpy(buffer->Data(), data, size);
        buffer->Flush(); // rule 1 uploads it now; the test records raw WebGPU, so no engine submit runs rule 2
        buffer->Unmap();
        return buffer;
    }

    WGPUBindGroup MakePushGroup(WGPUBindGroupLayout layout, WGPUBuffer &buffer)
    {
        const float push[4] = {kPushOffset, 0.0f, 0.0f, 0.0f};
        WGPUBufferDescriptor desc{};
        desc.size = sizeof(push);
        desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        buffer = wgpuDeviceCreateBuffer(Device(), &desc);
        wgpuQueueWriteBuffer(WebGpuRhiImpl::Get()->m_queue, buffer, 0, push, sizeof(push));

        WGPUBindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = buffer;
        entry.size = sizeof(push);
        WGPUBindGroupDescriptor group{};
        group.layout = layout;
        group.entryCount = 1;
        group.entries = &entry;
        return wgpuDeviceCreateBindGroup(Device(), &group);
    }

    void Dispatch(WGPUComputePipeline pipeline, const PipelineLayout &layout, Descriptor *set0, WGPUBindGroup push)
    {
        WGPUBindGroupDescriptor emptyDesc{};
        emptyDesc.layout = layout.empty;
        WGPUBindGroup empty = wgpuDeviceCreateBindGroup(Device(), &emptyDesc);

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(Device(), nullptr);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, WebGpuDescriptorImpl::From(set0)->GetBindGroup(), 0, nullptr);
        wgpuComputePassEncoderSetBindGroup(pass, 1, empty, 0, nullptr);
        wgpuComputePassEncoderSetBindGroup(pass, 2, empty, 0, nullptr);
        wgpuComputePassEncoderSetBindGroup(pass, 3, push, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);

        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(WebGpuRhiImpl::Get()->m_queue, 1, &commands);
        wgpuCommandBufferRelease(commands);
        wgpuComputePassEncoderRelease(pass);
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(empty);
    }

    // Output[i] = Input[i] * scale + bias + push offset, through the engine's descriptor and the baked module.
    void TestDispatch(WGPUComputePipeline pipeline, const PipelineLayout &layout, Descriptor *set0)
    {
        std::vector<float> input(kCount);
        for (uint32_t i = 0; i < kCount; ++i)
            input[i] = static_cast<float>(i);

        const struct
        {
            float scale;
            float bias;
            uint32_t count;
            uint32_t pad;
        } params{kScale, kBias, kCount, 0};

        Buffer *in = MakeHostBuffer(sizeof(float) * kCount,
                                    PE_BUFFER_USAGE_STORAGE_BUFFER,
                                    input.data(),
                                    "shaders_input");
        Buffer *uniform = MakeHostBuffer(sizeof(params), PE_BUFFER_USAGE_UNIFORM_BUFFER, &params, "shaders_params");
        Buffer *out = Buffer::Create({.size = sizeof(float) * kCount,
                                      .usage = PE_BUFFER_USAGE_STORAGE_BUFFER,
                                      .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY,
                                      .name = "shaders_output"});
        ImageDesc imageDesc{};
        imageDesc.width = 4;
        imageDesc.height = 1;
        imageDesc.format = PE_FORMAT_R8G8B8A8_UNORM;
        imageDesc.usage = PE_IMAGE_USAGE_STORAGE | PE_IMAGE_USAGE_TRANSFER_SRC;
        imageDesc.name = "shaders_target";
        Image *target = Image::Create(imageDesc);
        target->CreateUAV(PE_IMAGE_VIEW_TYPE_2D, 0);

        set0->SetBuffer(0, in);
        set0->SetBuffer(1, out);
        set0->SetBuffer(2, uniform);
        set0->SetImageView(3, target->GetUAV(0));
        set0->Update();

        WGPUBuffer pushBuffer = nullptr;
        WGPUBindGroup push = MakePushGroup(layout.push, pushBuffer);
        Dispatch(pipeline, layout, set0, push);

        const std::vector<uint8_t> bytes = ReadBuffer(WebGpuBufferImpl::From(out)->m_buffer, sizeof(float) * kCount);
        const float *values = reinterpret_cast<const float *>(bytes.data());
        bool match = true;
        for (uint32_t i = 0; i < kCount; ++i)
            match &= values[i] == input[i] * kScale + kBias + kPushOffset;

        std::printf("[shaders] Output[0..3] = %.2f %.2f %.2f %.2f\n", values[0], values[1], values[2], values[3]);
        Check(match, "dispatch: every Output element is Input * scale + bias + push offset");

        wgpuBindGroupRelease(push);
        wgpuBufferRelease(pushBuffer);
        Buffer::Destroy(in);
        Buffer::Destroy(uniform);
        Buffer::Destroy(out);
        Image::Destroy(target);
    }

    void DestroySets(std::vector<Descriptor *> &sets)
    {
        for (Descriptor *&set : sets)
            Descriptor::Destroy(set);
    }

    void Run()
    {
        TestMissingBake();

        Shader *cs = Load("RhiShadersCS.hlsl", PE_SHADER_STAGE_COMPUTE, "mainCS");
        Shader *vs = Load("RhiShadersVS.hlsl", PE_SHADER_STAGE_VERTEX, "mainVS");
        Shader *ps = Load("RhiShadersPS.hlsl", PE_SHADER_STAGE_FRAGMENT, "mainPS");

        std::vector<Descriptor *> computeSets = cs->GetReflection().GetDescriptors();
        TestComputeReflection(cs, computeSets);

        PassInfo pass;
        pass.pVertShader = vs;
        pass.pFragShader = ps;
        std::vector<Descriptor *> graphicsSets = Shader::ReflectPassDescriptors(pass);
        pass.pVertShader = nullptr; // the PassInfo would destroy the shaders
        pass.pFragShader = nullptr;
        TestGraphicsReflection(graphicsSets);

        TestModules({cs, vs, ps});

        const uint32_t errorsBefore = ErrorsSoFar();
        PipelineLayout computeLayout = MakePipelineLayout(computeSets);
        WGPUComputePipeline compute = MakeComputePipeline(cs, computeLayout.layout);
        Check(compute && ErrorsSoFar() == errorsBefore, "compute pipeline builds on the engine's bind group layouts");

        PipelineLayout graphicsLayout = MakePipelineLayout(graphicsSets);
        WGPURenderPipeline render = MakeRenderPipeline(vs, ps, graphicsLayout.layout);
        Check(render && ErrorsSoFar() == errorsBefore,
              "render pipeline builds on the merged vertex + fragment layouts");

        TestDispatch(compute, computeLayout, computeSets[0]);

        wgpuRenderPipelineRelease(render);
        wgpuComputePipelineRelease(compute);
        ReleaseLayout(graphicsLayout);
        ReleaseLayout(computeLayout);
        DestroySets(graphicsSets);
        DestroySets(computeSets);
        Shader::Destroy(cs);
        Shader::Destroy(vs);
        Shader::Destroy(ps);
    }
} // namespace

int main()
{
    try
    {
        EventSystem::Init(); // CommandBuffer::Wait dispatches AfterCommandWait
        RHII.Init(nullptr, PE_GRAPHICS_API_WEBGPU);

        Run();

        const uint32_t errors = ErrorsSoFar();
        if (errors)
            s_failures++;

        RHII.Destroy();
        std::printf("[shaders] done: %d failures, %u WebGPU errors\n", s_failures, errors);
    }
    catch (const std::exception &e)
    {
        std::printf("[shaders] FAIL exception: %s\n[shaders] done: aborted\n", e.what());
    }

    return 0;
}
