#include "Render/SceneFrameResources.h"
#include "API/Command.h"
#include "API/Semaphore.h"

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

    void WaitSceneFrameCommands(std::vector<CommandBuffer *> &cmds)
    {
        for (auto &cmd : cmds)
            WaitSceneFrameCommand(cmd);
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
