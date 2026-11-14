#if defined(PE_WEB)

#include "UI/Backends/imgui_impl_webgpu.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace
{
    constexpr const char *kImGuiWgsl = R"(
struct Uniforms {
    scale : vec2f,
    translate : vec2f,
}

struct VertexOut {
    @builtin(position) position : vec4f,
    @location(0) uv : vec2f,
    @location(1) color : vec4f,
}

@group(0) @binding(0) var<uniform> uniforms : Uniforms;
@group(0) @binding(1) var uiSampler : sampler;
@group(0) @binding(2) var uiTexture : texture_2d<f32>;

@vertex
fn vs(@location(0) position : vec2f, @location(1) uv : vec2f, @location(2) color : vec4f) -> VertexOut {
    var out : VertexOut;
    out.position = vec4f(position * uniforms.scale + uniforms.translate, 0.0, 1.0);
    out.uv = uv;
    out.color = color;
    return out;
}

@fragment
fn fs(in : VertexOut) -> @location(0) vec4f {
    return in.color * textureSample(uiTexture, uiSampler, in.uv);
}
)";

    struct Texture
    {
        WGPUTextureView view = nullptr;
        WGPUBindGroup group = nullptr;
    };

    struct GeometryBuffer
    {
        WGPUBuffer buffer = nullptr;
        uint64_t size = 0;
    };

    struct State
    {
        WGPUDevice device = nullptr;
        WGPUQueue queue = nullptr;
        WGPUShaderModule module = nullptr;
        WGPUBindGroupLayout groupLayout = nullptr;
        WGPUPipelineLayout pipelineLayout = nullptr;
        WGPURenderPipeline pipeline = nullptr;
        WGPUSampler sampler = nullptr;
        WGPUBuffer uniforms = nullptr;
        WGPUTexture fontTexture = nullptr;
        WGPUTextureView fontView = nullptr;
        ImTextureID fontId = 0;
        GeometryBuffer vertices;
        GeometryBuffer indices;
    };

    State s_state;

    WGPUBindGroupLayout CreateGroupLayout()
    {
        WGPUBindGroupLayoutEntry entries[3] = {};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Vertex;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Fragment;
        entries[2].texture.sampleType = WGPUTextureSampleType_Float;
        entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutDescriptor desc{};
        desc.entryCount = 3;
        desc.entries = entries;
        return wgpuDeviceCreateBindGroupLayout(s_state.device, &desc);
    }

    void CreatePipeline(WGPUTextureFormat targetFormat)
    {
        WGPUVertexAttribute attributes[3] = {};
        attributes[0].format = WGPUVertexFormat_Float32x2;
        attributes[0].offset = offsetof(ImDrawVert, pos);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Float32x2;
        attributes[1].offset = offsetof(ImDrawVert, uv);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WGPUVertexFormat_Unorm8x4;
        attributes[2].offset = offsetof(ImDrawVert, col);
        attributes[2].shaderLocation = 2;

        WGPUVertexBufferLayout vertexLayout{};
        vertexLayout.arrayStride = sizeof(ImDrawVert);
        vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexLayout.attributeCount = 3;
        vertexLayout.attributes = attributes;

        WGPUBlendState blend{};
        blend.color.operation = WGPUBlendOperation_Add;
        blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blend.alpha.operation = WGPUBlendOperation_Add;
        blend.alpha.srcFactor = WGPUBlendFactor_One;
        blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

        WGPUColorTargetState target{};
        target.format = targetFormat;
        target.blend = &blend;
        target.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment{};
        fragment.module = s_state.module;
        fragment.entryPoint = {"fs", WGPU_STRLEN};
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor desc{};
        desc.layout = s_state.pipelineLayout;
        desc.vertex.module = s_state.module;
        desc.vertex.entryPoint = {"vs", WGPU_STRLEN};
        desc.vertex.bufferCount = 1;
        desc.vertex.buffers = &vertexLayout;
        desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        desc.primitive.frontFace = WGPUFrontFace_CCW;
        desc.primitive.cullMode = WGPUCullMode_None;
        desc.multisample.count = 1;
        desc.multisample.mask = 0xFFFFFFFF;
        desc.fragment = &fragment;
        s_state.pipeline = wgpuDeviceCreateRenderPipeline(s_state.device, &desc);
    }

    void EnsureBuffer(GeometryBuffer &geometry, uint64_t size, WGPUBufferUsage usage)
    {
        if (geometry.size >= size)
            return;

        if (geometry.buffer)
            wgpuBufferRelease(geometry.buffer);

        WGPUBufferDescriptor desc{};
        desc.size = size + size / 2;
        desc.usage = usage | WGPUBufferUsage_CopyDst;
        geometry.buffer = wgpuDeviceCreateBuffer(s_state.device, &desc);
        geometry.size = desc.size;
    }

    void UploadGeometry(const ImDrawData *drawData)
    {
        const uint64_t vertexBytes = uint64_t(drawData->TotalVtxCount) * sizeof(ImDrawVert);
        const uint64_t indexBytes = uint64_t(drawData->TotalIdxCount) * sizeof(ImDrawIdx);
        EnsureBuffer(s_state.vertices, vertexBytes, WGPUBufferUsage_Vertex);
        EnsureBuffer(s_state.indices, indexBytes, WGPUBufferUsage_Index);

        uint64_t vertexOffset = 0;
        uint64_t indexOffset = 0;
        for (const ImDrawList *list : drawData->CmdLists)
        {
            const uint64_t vertexSize = uint64_t(list->VtxBuffer.Size) * sizeof(ImDrawVert);
            const uint64_t indexSize = uint64_t(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
            wgpuQueueWriteBuffer(s_state.queue, s_state.vertices.buffer, vertexOffset, list->VtxBuffer.Data, vertexSize);
            wgpuQueueWriteBuffer(s_state.queue, s_state.indices.buffer, indexOffset, list->IdxBuffer.Data, indexSize);
            vertexOffset += vertexSize;
            indexOffset += indexSize;
        }
    }

    void WriteUniforms(const ImDrawData *drawData)
    {
        const float scaleX = 2.0f / drawData->DisplaySize.x;
        const float scaleY = -2.0f / drawData->DisplaySize.y;
        const float uniforms[4] = {
            scaleX,
            scaleY,
            -1.0f - drawData->DisplayPos.x * scaleX,
            1.0f - drawData->DisplayPos.y * scaleY,
        };
        wgpuQueueWriteBuffer(s_state.queue, s_state.uniforms, 0, uniforms, sizeof(uniforms));
    }

    // False when the clipped rectangle is empty.
    bool ApplyScissor(WGPURenderPassEncoder pass,
                      const ImDrawData *drawData,
                      const ImDrawCmd &command,
                      uint32_t width,
                      uint32_t height)
    {
        const ImVec2 origin = drawData->DisplayPos;
        const ImVec2 scale = drawData->FramebufferScale;
        const float minX = std::max((command.ClipRect.x - origin.x) * scale.x, 0.0f);
        const float minY = std::max((command.ClipRect.y - origin.y) * scale.y, 0.0f);
        const float maxX = std::min((command.ClipRect.z - origin.x) * scale.x, float(width));
        const float maxY = std::min((command.ClipRect.w - origin.y) * scale.y, float(height));
        if (maxX <= minX || maxY <= minY)
            return false;

        wgpuRenderPassEncoderSetScissorRect(pass,
                                            uint32_t(minX),
                                            uint32_t(minY),
                                            uint32_t(maxX - minX),
                                            uint32_t(maxY - minY));
        return true;
    }
} // namespace

bool ImGui_ImplWebGpu_Init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat targetFormat)
{
    s_state.device = device;
    s_state.queue = queue;

    WGPUShaderSourceWGSL wgsl{};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = {kImGuiWgsl, WGPU_STRLEN};
    WGPUShaderModuleDescriptor moduleDesc{};
    moduleDesc.nextInChain = &wgsl.chain;
    moduleDesc.label = {"ImGui", WGPU_STRLEN};
    s_state.module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    s_state.groupLayout = CreateGroupLayout();
    WGPUPipelineLayoutDescriptor layoutDesc{};
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &s_state.groupLayout;
    s_state.pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);
    CreatePipeline(targetFormat);

    WGPUSamplerDescriptor samplerDesc{};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.lodMaxClamp = 32.0f;
    samplerDesc.maxAnisotropy = 1;
    s_state.sampler = wgpuDeviceCreateSampler(device, &samplerDesc);

    WGPUBufferDescriptor uniformDesc{};
    uniformDesc.size = sizeof(float) * 4;
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    s_state.uniforms = wgpuDeviceCreateBuffer(device, &uniformDesc);
    return true;
}

ImTextureID ImGui_ImplWebGpu_AddTexture(WGPUTextureView view)
{
    WGPUBindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].buffer = s_state.uniforms;
    entries[0].size = sizeof(float) * 4;
    entries[1].binding = 1;
    entries[1].sampler = s_state.sampler;
    entries[2].binding = 2;
    entries[2].textureView = view;

    WGPUBindGroupDescriptor desc{};
    desc.layout = s_state.groupLayout;
    desc.entryCount = 3;
    desc.entries = entries;

    Texture *texture = IM_NEW(Texture);
    texture->view = view;
    texture->group = wgpuDeviceCreateBindGroup(s_state.device, &desc);
    return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture));
}

void ImGui_ImplWebGpu_RemoveTexture(ImTextureID id)
{
    Texture *texture = reinterpret_cast<Texture *>(static_cast<uintptr_t>(id));
    if (!texture)
        return;

    wgpuBindGroupRelease(texture->group);
    IM_DELETE(texture);
}

void ImGui_ImplWebGpu_CreateFontsTexture()
{
    ImGuiIO &io = ImGui::GetIO();
    unsigned char *pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    WGPUTextureDescriptor desc{};
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size = {uint32_t(width), uint32_t(height), 1};
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    s_state.fontTexture = wgpuDeviceCreateTexture(s_state.device, &desc);
    s_state.fontView = wgpuTextureCreateView(s_state.fontTexture, nullptr);

    WGPUTexelCopyTextureInfo destination{};
    destination.texture = s_state.fontTexture;
    destination.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow = uint32_t(width) * 4;
    layout.rowsPerImage = uint32_t(height);
    wgpuQueueWriteTexture(s_state.queue, &destination, pixels, size_t(width) * height * 4, &layout, &desc.size);

    s_state.fontId = ImGui_ImplWebGpu_AddTexture(s_state.fontView);
    io.Fonts->SetTexID(s_state.fontId);
}

void ImGui_ImplWebGpu_RenderDrawData(ImDrawData *drawData, WGPURenderPassEncoder pass, uint32_t width, uint32_t height)
{
    if (drawData->TotalVtxCount == 0 || !pass)
        return;

    UploadGeometry(drawData);
    WriteUniforms(drawData);

    wgpuRenderPassEncoderSetPipeline(pass, s_state.pipeline);
    wgpuRenderPassEncoderSetViewport(pass, 0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, s_state.vertices.buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, s_state.indices.buffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);

    uint32_t listVertexOffset = 0;
    uint32_t listIndexOffset = 0;
    for (const ImDrawList *list : drawData->CmdLists)
    {
        for (const ImDrawCmd &command : list->CmdBuffer)
        {
            // ponytail: draw-list callbacks are not run; the runtime UI registers none.
            if (command.UserCallback || !ApplyScissor(pass, drawData, command, width, height))
                continue;

            const Texture *texture = reinterpret_cast<const Texture *>(static_cast<uintptr_t>(command.GetTexID()));
            wgpuRenderPassEncoderSetBindGroup(pass, 0, texture->group, 0, nullptr);
            wgpuRenderPassEncoderDrawIndexed(pass,
                                             command.ElemCount,
                                             1,
                                             listIndexOffset + command.IdxOffset,
                                             int32_t(listVertexOffset + command.VtxOffset),
                                             0);
        }

        listVertexOffset += uint32_t(list->VtxBuffer.Size);
        listIndexOffset += uint32_t(list->IdxBuffer.Size);
    }
}

void ImGui_ImplWebGpu_Shutdown()
{
    ImGui_ImplWebGpu_RemoveTexture(s_state.fontId);
    if (s_state.fontView)
        wgpuTextureViewRelease(s_state.fontView);
    if (s_state.fontTexture)
        wgpuTextureRelease(s_state.fontTexture);
    if (s_state.vertices.buffer)
        wgpuBufferRelease(s_state.vertices.buffer);
    if (s_state.indices.buffer)
        wgpuBufferRelease(s_state.indices.buffer);

    wgpuBufferRelease(s_state.uniforms);
    wgpuSamplerRelease(s_state.sampler);
    wgpuRenderPipelineRelease(s_state.pipeline);
    wgpuPipelineLayoutRelease(s_state.pipelineLayout);
    wgpuBindGroupLayoutRelease(s_state.groupLayout);
    wgpuShaderModuleRelease(s_state.module);
    s_state = {};
}

#endif
