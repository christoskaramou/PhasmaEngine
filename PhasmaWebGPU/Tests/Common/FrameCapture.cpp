#include "FrameCapture.h"

#include <SDL.h>
#include <cstring>

namespace pwgpu::test
{
    uint32_t AlignTo(uint32_t value, uint32_t alignment)
    {
        return (value + alignment - 1u) / alignment * alignment;
    }

    bool ReadTextureRgba8(WGPUInstance instance,
                          WGPUDevice device,
                          WGPUQueue queue,
                          WGPUTexture texture,
                          uint32_t width,
                          uint32_t height,
                          std::vector<uint8_t> &outPixels)
    {
        const uint32_t tightBytesPerRow = width * 4u;
        const uint32_t paddedBytesPerRow = AlignTo(tightBytesPerRow, 256u);
        const uint64_t readbackSize =
            static_cast<uint64_t>(paddedBytesPerRow) * static_cast<uint64_t>(height);

        WGPUBufferDescriptor readbackDesc{};
        readbackDesc.label = {"sample_readback", WGPU_STRLEN};
        readbackDesc.size = readbackSize;
        readbackDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        WGPUBuffer readbackBuffer = wgpuDeviceCreateBuffer(device, &readbackDesc);
        if (!readbackBuffer)
            return false;

        WGPUCommandEncoderDescriptor encoderDesc{};
        encoderDesc.label = {"sample_readback_enc", WGPU_STRLEN};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
        if (!encoder)
        {
            wgpuBufferRelease(readbackBuffer);
            return false;
        }

        WGPUTexelCopyTextureInfo source{};
        source.texture = texture;
        source.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo destination{};
        destination.buffer = readbackBuffer;
        destination.layout.bytesPerRow = paddedBytesPerRow;
        destination.layout.rowsPerImage = height;

        WGPUExtent3D copySize{width, height, 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);

        WGPUCommandBufferDescriptor commandBufferDesc{};
        commandBufferDesc.label = {"sample_readback_cmd", WGPU_STRLEN};
        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDesc);
        if (!commandBuffer)
        {
            wgpuCommandEncoderRelease(encoder);
            wgpuBufferRelease(readbackBuffer);
            return false;
        }

        wgpuQueueSubmit(queue, 1, &commandBuffer);

        bool mapSucceeded = false;
        WGPUBufferMapCallbackInfo mapCallback{};
        mapCallback.mode = WGPUCallbackMode_AllowProcessEvents;
        mapCallback.userdata1 = &mapSucceeded;
        mapCallback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void *userdata1, void *)
        {
            auto *result = static_cast<bool *>(userdata1);
            *result = status == WGPUMapAsyncStatus_Success;
        };

        WGPUFuture future = wgpuBufferMapAsync(readbackBuffer, WGPUMapMode_Read, 0, readbackSize, mapCallback);
        WGPUFutureWaitInfo waitInfo{future, WGPU_FALSE};
        WGPUWaitStatus waitStatus = wgpuInstanceWaitAny(instance, 1, &waitInfo, UINT64_MAX);
        if (waitStatus != WGPUWaitStatus_Success)
        {
            fprintf(stderr, "[Capture] wgpuInstanceWaitAny failed (status %d)\n",
                    static_cast<int>(waitStatus));
            wgpuCommandBufferRelease(commandBuffer);
            wgpuCommandEncoderRelease(encoder);
            wgpuBufferRelease(readbackBuffer);
            return false;
        }

        bool success = false;
        if (mapSucceeded)
        {
            const uint8_t *mapped = static_cast<const uint8_t *>(wgpuBufferGetConstMappedRange(readbackBuffer, 0, readbackSize));
            if (mapped)
            {
                outPixels.resize(static_cast<size_t>(tightBytesPerRow) * static_cast<size_t>(height));
                for (uint32_t row = 0; row < height; row++)
                {
                    memcpy(outPixels.data() + static_cast<size_t>(row) * tightBytesPerRow,
                           mapped + static_cast<size_t>(row) * paddedBytesPerRow,
                           tightBytesPerRow);
                }
                success = true;
            }
        }

        if (mapSucceeded)
            wgpuBufferUnmap(readbackBuffer);

        wgpuCommandBufferRelease(commandBuffer);
        wgpuCommandEncoderRelease(encoder);
        wgpuBufferRelease(readbackBuffer);
        return success;
    }

    bool DumpTextureToBmp(WGPUInstance instance,
                          WGPUDevice device,
                          WGPUQueue queue,
                          WGPUTexture texture,
                          WGPUTextureFormat format,
                          uint32_t width,
                          uint32_t height,
                          const std::string &outputPath)
    {
        std::vector<uint8_t> pixels;
        if (!ReadTextureRgba8(instance, device, queue, texture, width, height, pixels))
            return false;

        if (format == WGPUTextureFormat_BGRA8Unorm ||
            format == WGPUTextureFormat_BGRA8UnormSrgb)
        {
            for (size_t i = 0; i + 3 < pixels.size(); i += 4)
                std::swap(pixels[i], pixels[i + 2]);
        }

        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(),
                                                                  static_cast<int>(width),
                                                                  static_cast<int>(height),
                                                                  32,
                                                                  static_cast<int>(width * 4u),
                                                                  SDL_PIXELFORMAT_RGBA32);
        if (!surface)
            return false;

        int saveResult = SDL_SaveBMP(surface, outputPath.c_str());
        SDL_FreeSurface(surface);
        return saveResult == 0;
    }
} // namespace pwgpu::test
