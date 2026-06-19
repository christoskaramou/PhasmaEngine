#include "Render/SceneScreenshot.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "Render/ScreenshotWriter.h"

namespace pe
{
    namespace
    {
        size_t GetScreenshotRowPitch(uint32_t width)
        {
            const size_t rowBytes = static_cast<size_t>(width) * 4;
            return RHII.AlignTextureRowPitch(rowBytes);
        }
    } // namespace

    bool QueueSceneScreenshotReadback(CommandBuffer *cmd,
                                      Image *sourceImage,
                                      Image *screenshotTarget,
                                      Buffer *&stagingBuffer,
                                      size_t &rowPitch,
                                      const std::string &stagingBufferName)
    {
        if (!cmd || !sourceImage || !screenshotTarget)
            return false;

        cmd->CopyImage(sourceImage, screenshotTarget);

        const uint32_t width = screenshotTarget->GetWidth();
        const uint32_t height = screenshotTarget->GetHeight();
        rowPitch = GetScreenshotRowPitch(width);
        const size_t bufferSize = rowPitch * height;

        Buffer::Destroy(stagingBuffer);
        stagingBuffer = Buffer::Create({
            .size = bufferSize,
            .usage = PE_BUFFER_USAGE_TRANSFER_DST,
            .memoryUsage = PE_MEMORY_USAGE_GPU_TO_CPU,
            .name = stagingBufferName,
        });

        cmd->CopyImageToBuffer(screenshotTarget, stagingBuffer);
        return true;
    }

    bool SaveSceneScreenshot(Buffer *&stagingBuffer,
                             Image *screenshotTarget,
                             const std::string &path,
                             size_t &rowPitch,
                             std::string *savedPath)
    {
        if (!stagingBuffer || !screenshotTarget)
            return false;

        stagingBuffer->Map();
        const uint8_t *pixels = static_cast<const uint8_t *>(stagingBuffer->Data());

        ScreenshotWriteDesc screenshot{};
        screenshot.path = path;
        screenshot.pixels = pixels;
        screenshot.width = screenshotTarget->GetWidth();
        screenshot.height = screenshotTarget->GetHeight();
        screenshot.rowPitch = rowPitch;
        screenshot.format = screenshotTarget->GetFormat();

        std::string resolvedPath;
        const bool saved = WriteScreenshotPng(screenshot, &resolvedPath);
        if (saved)
        {
            PE_INFO("Screenshot saved: %s", resolvedPath.c_str());
            if (savedPath)
                *savedPath = resolvedPath;
        }
        else
        {
            PE_ERROR("[Renderer] Failed to save screenshot: %s", path.c_str());
        }

        stagingBuffer->Unmap();
        Buffer::Destroy(stagingBuffer);
        rowPitch = 0;
        return saved;
    }
} // namespace pe
