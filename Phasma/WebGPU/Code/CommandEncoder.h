#pragma once

#include <webgpu/webgpu.h>
#include "API/Command.h"

struct WGPUDeviceImpl;

struct WGPURenderPipelineImpl;
struct WGPUComputePipelineImpl;
struct WGPUBindGroupImpl;
struct WGPUQuerySetImpl;
struct WGPUTextureViewImpl;
struct WGPURenderBundleImpl;

struct WGPUBufferImpl;
struct WGPUTextureImpl;

namespace pe
{
    class ImageView;
}

struct RetainedResources
{
    std::vector<WGPURenderPipelineImpl *> renderPipelines;
    std::vector<WGPUComputePipelineImpl *> computePipelines;
    std::vector<WGPUBindGroupImpl *> bindGroups;
    std::vector<WGPUQuerySetImpl *> querySets;
    std::vector<WGPUTextureViewImpl *> textureViews;
    std::vector<WGPURenderBundleImpl *> renderBundles;
    std::vector<WGPUBufferImpl *> usedBuffers;   // buffers touched by this command encoder
    std::vector<WGPUTextureImpl *> usedTextures; // textures touched by this command encoder
    std::vector<pe::ImageView *> nativeImageViews;

    void UseBuffer(WGPUBufferImpl *buffer)
    {
        wgpuBufferAddRef(buffer);
        usedBuffers.push_back(buffer);
    }
    void UseTexture(WGPUTextureImpl *texture)
    {
        wgpuTextureAddRef(texture);
        usedTextures.push_back(texture);
    }
    void MergeFrom(RetainedResources &other);
    void ReleaseAll();
};

struct WGPUCommandBufferImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    pe::CommandBuffer *cmd = nullptr;
    bool submitted = false;
    bool invalid = false;
    bool deferredResourceError = false; // §3.3 destroyed resource — fires at submit

    RetainedResources retained;
};

struct WGPUCommandEncoderImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    pe::CommandBuffer *cmd = nullptr;
    bool finished = false;
    bool hasOpenPass = false;
    bool invalid = false;
    uint32_t debugGroupDepth = 0;
    RetainedResources retained;
    std::string deferredErrorMessage;
    bool deferredResourceError = false; // §3.3 destroyed resource — fires at submit
};

namespace pwgpu
{
    // §23.x: layout parameters and offset + requiredBytesInCopy vs. bufferSize,
    // overflow-safe. Shared by the encoder copies and queue.writeTexture.
    bool ValidateBufferCopyLayout(uint64_t bufferSize, uint64_t offset,
                                  uint32_t bytesPerRow, uint32_t rowsPerImage,
                                  const WGPUExtent3D &copySize,
                                  uint32_t footprint, uint32_t blockW, uint32_t blockH);
} // namespace pwgpu
