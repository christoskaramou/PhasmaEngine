#pragma once

#include "API/RHITypes.h"

#include <string_view>
#include <vector>

namespace pe
{
    class CommandBuffer;
    class Semaphore;

    void WaitSceneFrameCommand(CommandBuffer *&cmd);
    void WaitPreviousSceneFrameCommand(std::vector<CommandBuffer *> &cmds);
    void WaitSceneFrameCommands(std::vector<CommandBuffer *> &cmds);
    void WaitSceneFrameCommandsAndCleanup(std::vector<CommandBuffer *> &cmds);
    void TransitionSceneSwapchainImagesToPresent(CommandBuffer *cmd);

    void CreateSceneFrameSemaphores(std::vector<Semaphore *> &acquireSemaphores,
                                    std::vector<Semaphore *> &submitSemaphores,
                                    uint32_t imageCount,
                                    std::string_view acquireNamePrefix,
                                    std::string_view submitNamePrefix,
                                    PeBarrierSync acquireStageFlags,
                                    PeBarrierSync submitStageFlags);

    void DestroySceneFrameSemaphores(std::vector<Semaphore *> &semaphores);
} // namespace pe
