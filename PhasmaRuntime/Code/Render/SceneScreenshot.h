#pragma once

#include <cstddef>
#include <string>

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
