#include "Render/SceneScreenshot.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "Render/ScreenshotWriter.h"

#if defined(PE_WEB)
#include <emscripten.h>

// The page's files live in memory, so a saved screenshot also downloads through the browser. The body is JavaScript;
// clang-format would split its => into "= >".
// clang-format off
EM_JS_DEPS(PeSceneScreenshot, "$UTF8ToString");

EM_JS(void, PeWebDownloadFile, (const char *path, const char *mimeType), {
    const file = UTF8ToString(path);
    const url = URL.createObjectURL(new Blob([FS.readFile(file)], { type: UTF8ToString(mimeType) }));
    const link = document.createElement("a");
    link.href = url;
    link.download = file.split("/").pop();
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 10000);
});
// clang-format on
#endif

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
#if defined(PE_WEB)
            PeWebDownloadFile(resolvedPath.c_str(), "image/png");
#endif
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
