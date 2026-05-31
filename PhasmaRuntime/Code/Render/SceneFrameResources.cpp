#include "Render/SceneFrameResources.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Semaphore.h"
#include "API/StagingManager.h"
#include "API/Swapchain.h"

namespace pe
{
    void WaitSceneFrameCommand(CommandBuffer *&cmd)
    {
        if (!cmd)
            return;

        cmd->Wait();
        cmd->Return();
        cmd = nullptr;
    }

    void WaitPreviousSceneFrameCommand(std::vector<CommandBuffer *> &cmds)
    {
        if (cmds.empty())
            return;

        const uint32_t frame = RHII.GetFrameIndex();
        WaitSceneFrameCommand(cmds[frame]);
        RHII.FlushDeletionQueue(frame);
        RHII.GetStagingManager()->RemoveUnused();
    }

    void WaitSceneFrameCommands(std::vector<CommandBuffer *> &cmds)
    {
        for (auto &cmd : cmds)
            WaitSceneFrameCommand(cmd);
    }

    void WaitSceneFrameCommandsAndCleanup(std::vector<CommandBuffer *> &cmds)
    {
        WaitSceneFrameCommands(cmds);
        RHII.GetStagingManager()->RemoveUnused();
    }

    void TransitionSceneSwapchainImagesToPresent(CommandBuffer *cmd)
    {
        if (!cmd || RHII.GetApi() == PE_GRAPHICS_API_DX12)
            return;

        const uint32_t imageCount = RHII.GetSwapchainImageCount();
        for (uint32_t i = 0; i < imageCount; i++)
        {
            ImageBarrierInfo barrierInfo{};
            barrierInfo.image = RHII.GetSwapchain()->GetImage(i);
            barrierInfo.layout = PE_IMAGE_LAYOUT_PRESENT_SRC;
            barrierInfo.stageFlags = PE_STAGE_ALL_COMMANDS;
            barrierInfo.accessMask = PE_ACCESS_NONE;
            cmd->ImageBarrier(barrierInfo);
        }
    }

    void SubmitAndPresentSceneFrame(std::vector<CommandBuffer *> &cmds,
                                    const std::vector<Semaphore *> &acquireSemaphores,
                                    const std::vector<Semaphore *> &submitSemaphores,
                                    const SceneFrameRecordCallback &recordFrame,
                                    bool &screenshotPending,
                                    const SceneFrameScreenshotCallback &saveScreenshot,
                                    const SceneFrameSubmitLabels &labels)
    {
        try
        {
            const uint32_t frame = RHII.GetFrameIndex();

            Semaphore *acquireSemaphore = acquireSemaphores[frame];
            Swapchain *swapchain = RHII.GetSwapchain();
            uint32_t imageIndex = 0;
            {
                PE_PROFILE_SCOPE(labels.acquire);
                imageIndex = swapchain->AquireNextImage(acquireSemaphore);
            }

            auto &frameCmd = cmds[frame];
            {
                PE_PROFILE_SCOPE(labels.record);
                frameCmd = recordFrame(imageIndex);
            }

            Semaphore *submitSemaphore = submitSemaphores[imageIndex];
            Queue *queue = RHII.GetMainQueue();
            {
                PE_PROFILE_SCOPE(labels.submit);
                queue->Submit(1, &frameCmd, acquireSemaphore, submitSemaphore);
            }

            {
                PE_PROFILE_SCOPE(labels.present);
                queue->Present(swapchain, imageIndex, submitSemaphore);
            }

            if (screenshotPending)
            {
                frameCmd->Wait();
                saveScreenshot();
                screenshotPending = false;
            }
        }
        catch (const SwapchainOutOfDateError &)
        {
            // Acquire or present reported the swapchain out-of-date/suboptimal (a window resize, or an
            // Android device rotation that changed the surface transform). Request a rebuild: the editor
            // and player frame loops both handle EventType::Resize by recreating the swapchain at the
            // surface's current extent and transform, which keeps the rendered aspect ratio correct.
            EventSystem::PushEvent(EventType::Resize);
        }
    }

    void CreateSceneFrameSemaphores(std::vector<Semaphore *> &acquireSemaphores,
                                    std::vector<Semaphore *> &submitSemaphores,
                                    uint32_t imageCount,
                                    std::string_view acquireNamePrefix,
                                    std::string_view submitNamePrefix,
                                    PeBarrierSync acquireStageFlags,
                                    PeBarrierSync submitStageFlags)
    {
        acquireSemaphores.reserve(imageCount);
        submitSemaphores.reserve(imageCount);

        for (uint32_t i = 0; i < imageCount; i++)
        {
            Semaphore *acquireSemaphore =
                Semaphore::Create(false, std::string(acquireNamePrefix) + std::to_string(i));
            if (acquireStageFlags != PE_STAGE_NONE)
                acquireSemaphore->SetStageFlags(acquireStageFlags);
            acquireSemaphores.push_back(acquireSemaphore);

            Semaphore *submitSemaphore = Semaphore::Create(false, std::string(submitNamePrefix) + std::to_string(i));
            if (submitStageFlags != PE_STAGE_NONE)
                submitSemaphore->SetStageFlags(submitStageFlags);
            submitSemaphores.push_back(submitSemaphore);
        }
    }

    void DestroySceneFrameSemaphores(std::vector<Semaphore *> &semaphores)
    {
        for (auto &semaphore : semaphores)
            Semaphore::Destroy(semaphore);
        semaphores.clear();
    }
} // namespace pe
