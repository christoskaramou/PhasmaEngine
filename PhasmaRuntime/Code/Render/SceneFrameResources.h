#pragma once

#include "API/RHITypes.h"


namespace pe
{
    class CommandBuffer;
    class Semaphore;

    struct SceneFrameSubmitLabels
    {
        const char *acquire = "Acquire Image";
        const char *record = "Record Passes";
        const char *submit = "Queue Submit";
        const char *present = "Present";
    };

    using SceneFrameRecordCallback = std::function<CommandBuffer *(uint32_t imageIndex)>;
    using SceneFrameScreenshotCallback = std::function<void()>;

    void WaitSceneFrameCommand(CommandBuffer *&cmd);
    void WaitPreviousSceneFrameCommand(std::vector<CommandBuffer *> &cmds);
    void WaitSceneFrameCommands(std::vector<CommandBuffer *> &cmds);
    void WaitSceneFrameCommandsAndCleanup(std::vector<CommandBuffer *> &cmds);
    void TransitionSceneSwapchainImagesToPresent(CommandBuffer *cmd);

    void SubmitAndPresentSceneFrame(std::vector<CommandBuffer *> &cmds,
                                    const std::vector<Semaphore *> &acquireSemaphores,
                                    const std::vector<Semaphore *> &submitSemaphores,
                                    const SceneFrameRecordCallback &recordFrame,
                                    bool &screenshotPending,
                                    const SceneFrameScreenshotCallback &saveScreenshot,
                                    const SceneFrameSubmitLabels &labels = {});

    void CreateSceneFrameSemaphores(std::vector<Semaphore *> &acquireSemaphores,
                                    std::vector<Semaphore *> &submitSemaphores,
                                    uint32_t imageCount,
                                    std::string_view acquireNamePrefix,
                                    std::string_view submitNamePrefix,
                                    PeBarrierSync acquireStageFlags,
                                    PeBarrierSync submitStageFlags);

    void DestroySceneFrameSemaphores(std::vector<Semaphore *> &semaphores);
} // namespace pe
