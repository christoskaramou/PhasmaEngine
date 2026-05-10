#include "SampleUtils.h"

#include <cstring>

namespace pwgpu::test
{
    namespace
    {
        constexpr uint32_t kTextureRowAlignment = 256;
    }

    std::string GetExecutableDir(const char *argv0)
    {
        if (!argv0)
            return "./";

        std::string path(argv0);
        size_t separator = path.find_last_of("/\\");
        return separator != std::string::npos ? path.substr(0, separator + 1) : "./";
    }

    std::string GetShaderDir(const std::string &exeDir, const char *sampleName)
    {
        std::string shaderDir = exeDir + "Shaders/";
        if (sampleName && sampleName[0] != '\0')
            shaderDir += sampleName;
        shaderDir += "/";
        return shaderDir;
    }

    std::string GetAssetPath(const std::string &exeDir, const char *assetName)
    {
        std::string assetPath = exeDir;
        if (assetName)
            assetPath += assetName;
        return assetPath;
    }

    void DefaultUncapturedErrorCallback(WGPUDevice const *,
                                        WGPUErrorType type,
                                        WGPUStringView message,
                                        void *,
                                        void *)
    {
        fprintf(stderr,
                "[WebGPU error %d] %.*s\n",
                static_cast<int>(type),
                message.data ? static_cast<int>(message.length) : 0,
                message.data ? message.data : "");
    }

    WGPUTextureFormat ChooseSurfaceFormat(const WGPUSurfaceCapabilities &caps)
    {
        return caps.formatCount > 0 ? caps.formats[0] : WGPUTextureFormat_BGRA8Unorm;
    }

    WGPUPresentMode ChoosePresentMode(const WGPUSurfaceCapabilities &caps)
    {
        WGPUPresentMode presentMode = WGPUPresentMode_Fifo;
        for (size_t i = 0; i < caps.presentModeCount; i++)
        {
            if (caps.presentModes[i] == WGPUPresentMode_Immediate)
                return WGPUPresentMode_Immediate;

            if (caps.presentModes[i] == WGPUPresentMode_Mailbox)
                presentMode = WGPUPresentMode_Mailbox;
        }

        return presentMode;
    }

    const char *PresentModeName(WGPUPresentMode mode)
    {
        if (mode == WGPUPresentMode_Immediate)
            return "Immediate";

        if (mode == WGPUPresentMode_Mailbox)
            return "Mailbox";

        if (mode == WGPUPresentMode_FifoRelaxed)
            return "FifoRelaxed";

        return "Fifo";
    }

    const char *SurfaceTextureStatusName(WGPUSurfaceGetCurrentTextureStatus status)
    {
        switch (status)
        {
        case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
            return "SuccessOptimal";
        case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
            return "SuccessSuboptimal";
        case WGPUSurfaceGetCurrentTextureStatus_Timeout:
            return "Timeout";
        case WGPUSurfaceGetCurrentTextureStatus_Outdated:
            return "Outdated";
        case WGPUSurfaceGetCurrentTextureStatus_Lost:
            return "Lost";
        case WGPUSurfaceGetCurrentTextureStatus_Error:
            return "Error";
        default:
            return "Unknown";
        }
    }

    bool IsSurfaceTextureStatusOk(WGPUSurfaceGetCurrentTextureStatus status)
    {
        return status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
               status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
    }

    WGPUBuffer CreateBuffer(WGPUDevice device,
                            uint64_t size,
                            WGPUBufferUsage usage,
                            const char *label)
    {
        WGPUBufferDescriptor descriptor{};
        descriptor.label = {label, WGPU_STRLEN};
        descriptor.size = size;
        descriptor.usage = usage;
        return wgpuDeviceCreateBuffer(device, &descriptor);
    }

    void UploadRGBA8Texture(WGPUQueue queue,
                            WGPUTexture texture,
                            const uint8_t *data,
                            uint32_t width,
                            uint32_t height,
                            uint32_t mipLevel,
                            uint32_t arrayLayer)
    {
        uint32_t unpaddedBytesPerRow = width * 4;
        uint32_t paddedBytesPerRow =
            ((unpaddedBytesPerRow + kTextureRowAlignment - 1u) / kTextureRowAlignment) *
            kTextureRowAlignment;

        std::vector<uint8_t> paddedPixels;
        const void *sourceData = data;
        size_t sourceSize = static_cast<size_t>(unpaddedBytesPerRow) * height;

        if (paddedBytesPerRow != unpaddedBytesPerRow)
        {
            paddedPixels.assign(static_cast<size_t>(paddedBytesPerRow) * height, 0);
            for (uint32_t row = 0; row < height; row++)
            {
                memcpy(paddedPixels.data() + static_cast<size_t>(row) * paddedBytesPerRow,
                       data + static_cast<size_t>(row) * unpaddedBytesPerRow,
                       unpaddedBytesPerRow);
            }

            sourceData = paddedPixels.data();
            sourceSize = paddedPixels.size();
        }

        WGPUTexelCopyTextureInfo destination{};
        destination.texture = texture;
        destination.mipLevel = mipLevel;
        destination.origin = {0, 0, arrayLayer};
        destination.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = paddedBytesPerRow;
        layout.rowsPerImage = height;

        WGPUExtent3D size{width, height, 1};
        wgpuQueueWriteTexture(queue, &destination, sourceData, sourceSize, &layout, &size);
    }

    WGPUTextureView CreateTextureView(WGPUTexture texture, WGPUTextureFormat format)
    {
        WGPUTextureViewDescriptor viewDesc{};
        viewDesc.format = format;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.mipLevelCount = 1;
        viewDesc.arrayLayerCount = 1;
        viewDesc.aspect = WGPUTextureAspect_All;
        return wgpuTextureCreateView(texture, &viewDesc);
    }

    WGPUSurfaceConfiguration MakeSurfaceConfig(WGPUDevice device,
                                               WGPUTextureFormat format,
                                               WGPUTextureUsage usage,
                                               uint32_t width,
                                               uint32_t height,
                                               WGPUPresentMode presentMode,
                                               WGPUCompositeAlphaMode alphaMode,
                                               const WGPUTextureFormat *viewFormats,
                                               size_t viewFormatCount)
    {
        WGPUSurfaceConfiguration config{};
        config.device = device;
        config.format = format;
        config.usage = usage;
        config.width = width;
        config.height = height;
        config.presentMode = presentMode;
        config.alphaMode = alphaMode;
        config.viewFormats = viewFormats;
        config.viewFormatCount = viewFormatCount;
        return config;
    }
} // namespace pwgpu::test
