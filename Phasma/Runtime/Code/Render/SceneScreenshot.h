#pragma once

#include <cstddef>

#if defined(PE_WEB)
// Downloads a file of the page's in-memory file system through the browser, under its own file name. The file is read
// before this returns, so the caller may delete it afterwards. Defined in SceneScreenshot.cpp (EM_JS).
extern "C" void PeWebDownloadFile(const char *path, const char *mimeType);
#endif

namespace pe
{
    class Buffer;
    class CommandBuffer;
    class Image;

    [[nodiscard]] bool QueueSceneScreenshotReadback(CommandBuffer *cmd,
                                                    Image *sourceImage,
                                                    Image *screenshotTarget,
                                                    Buffer *&stagingBuffer,
                                                    size_t &rowPitch,
                                                    const std::string &stagingBufferName);

    [[nodiscard]] bool SaveSceneScreenshot(Buffer *&stagingBuffer,
                                           Image *screenshotTarget,
                                           const std::string &path,
                                           size_t &rowPitch,
                                           std::string *savedPath = nullptr);
} // namespace pe
