#ifdef PE_VULKAN
#include "Renderer/Queue.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Command.h"
#include "Renderer/Swapchain.h"
#include "Renderer/RHI.h"
#include "Renderer/Surface.h"

namespace pe
{
    Queue::Queue(QueueApiHandle apiHandle,
                 uint32_t familyId,
                 QueueTypeFlags queueTypeFlags,
                 ivec3 imageGranularity,
                 const std::string &name)
        : PeHandle<Queue, QueueApiHandle>(apiHandle),
          m_id{ID::NextID()},
          m_familyId{familyId},
          m_queueTypeFlags{queueTypeFlags},
          m_imageGranularity{imageGranularity},
          m_name{name}
    {
#if PE_DEBUG_MODE
        m_frameCmdsSubmitted.resize(SWAPCHAIN_IMAGES);
#endif
    }

    Queue::~Queue()
    {
    }

    void Queue::Submit(uint32_t commandBuffersCount, CommandBuffer *const *commandBuffers,
                       uint32_t waitSemaphoresCount, Semaphore *const *waitSemaphores,
                       uint32_t signalSemaphoresCount, Semaphore *const *signalSemaphores)
    {
        std::lock_guard<std::mutex> lock(s_submitMutex);

        std::vector<VkCommandBufferSubmitInfo> commandBufferSubmitInfos{};
        commandBufferSubmitInfos.reserve(commandBuffersCount);

        std::vector<VkSemaphoreSubmitInfo> waitSemaphoreSubmitInfos{};
        waitSemaphoreSubmitInfos.reserve(waitSemaphoresCount);

        std::vector<VkSemaphoreSubmitInfo> signalSemaphoreSubmitInfos{};
        signalSemaphoreSubmitInfos.reserve(signalSemaphoresCount + commandBuffersCount);

        // command buffers
        for (uint32_t i = 0; i < commandBuffersCount; i++)
        {
            if (commandBuffers[i])
            {
                VkCommandBufferSubmitInfo cbInfo{};
                cbInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
                cbInfo.commandBuffer = commandBuffers[i]->ApiHandle();
                commandBufferSubmitInfos.push_back(cbInfo);
                
                // signal timeline semaphore for the command buffer wait()
                VkSemaphoreSubmitInfo info{};
                info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
                info.semaphore = commandBuffers[i]->GetSemaphore()->ApiHandle();
                info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                info.value = commandBuffers[i]->IncreaseSumbitions();
                signalSemaphoreSubmitInfos.push_back(info);
            }
        }

        // wait semaphores
        for (uint32_t i = 0; i < waitSemaphoresCount; i++)
        {
            if (waitSemaphores[i])
            {
                VkSemaphoreSubmitInfo info{};
                info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
                info.semaphore = waitSemaphores[i]->ApiHandle();
                info.stageMask = Translate<VkPipelineStageFlags2>(waitSemaphores[i]->GetStageFlags());
                info.value = waitSemaphores[i]->GetValue();
                waitSemaphoreSubmitInfos.push_back(info);
            }
        }

        // signal semaphores
        for (uint32_t i = 0; i < signalSemaphoresCount; i++)
        {
            if (signalSemaphores[i])
            {
                VkSemaphoreSubmitInfo info{};
                info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
                info.semaphore = signalSemaphores[i]->ApiHandle();
                info.stageMask = Translate<VkPipelineStageFlags2>(signalSemaphores[i]->GetStageFlags());
                // TODO: figure the value, for now income signals are not timeline semaphores either way
                info.value = 0;
                signalSemaphoreSubmitInfos.push_back(info);
            }
        }

        VkSubmitInfo2 si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        si.waitSemaphoreInfoCount = static_cast<uint32_t>(waitSemaphoreSubmitInfos.size());
        si.pWaitSemaphoreInfos = waitSemaphoreSubmitInfos.data();
        si.commandBufferInfoCount = static_cast<uint32_t>(commandBufferSubmitInfos.size());
        si.pCommandBufferInfos = commandBufferSubmitInfos.data();
        si.signalSemaphoreInfoCount = static_cast<uint32_t>(signalSemaphoreSubmitInfos.size());
        si.pSignalSemaphoreInfos = signalSemaphoreSubmitInfos.data();

        PE_CHECK(vkQueueSubmit2(m_apiHandle, 1, &si, nullptr));

#if PE_DEBUG_MODE
        uint32_t frame = RHII.GetFrameIndex();
        m_frameCmdsSubmitted[frame].insert(m_frameCmdsSubmitted[frame].end(), commandBuffers, commandBuffers + commandBuffersCount);
#endif
    }

    void Queue::Present(
        uint32_t swapchainCount, Swapchain *const *swapchains,
        uint32_t *imageIndices,
        uint32_t waitSemaphoreCount, Semaphore *const *waitSemaphores)
    {
        std::vector<VkSwapchainKHR> swapchainsVK{};
        swapchainsVK.reserve(swapchainCount);
        for (uint32_t i = 0; i < swapchainCount; i++)
        {
            if (swapchains[i])
                swapchainsVK.push_back(swapchains[i]->ApiHandle());
        }

        std::vector<VkSemaphore> waitSemaphoresVK{};
        waitSemaphoresVK.reserve(waitSemaphoreCount);
        for (uint32_t i = 0; i < waitSemaphoreCount; i++)
        {
            if (waitSemaphores[i])
                waitSemaphoresVK.push_back(waitSemaphores[i]->ApiHandle());
        }

        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphoresVK.size());
        pi.pWaitSemaphores = waitSemaphoresVK.data();
        pi.swapchainCount = static_cast<uint32_t>(swapchainsVK.size());
        pi.pSwapchains = swapchainsVK.data();
        pi.pImageIndices = imageIndices;

        PE_CHECK(vkQueuePresentKHR(m_apiHandle, &pi));
    }

    void Queue::WaitIdle()
    {
        PE_CHECK(vkQueueWaitIdle(m_apiHandle));
    }

    void Queue::BeginDebugRegion(const std::string &name)
    {
        Debug::BeginQueueRegion(this, name);
    }

    void Queue::InsertDebugLabel(const std::string &name)
    {
        Debug::InsertQueueLabel(this, name);
    }

    void Queue::EndDebugRegion()
    {
        Debug::EndQueueRegion(this);
    }

    void Queue::Init(GpuApiHandle gpu, DeviceApiHandle device, SurfaceApiHandle surface)
    {
        uint32_t queueFamPropCount;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamPropCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamPropCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamPropCount, queueFamilyProperties.data());

        auto &properties = queueFamilyProperties;
        for (uint32_t i = 0; i < properties.size(); i++)
        {
            for (uint32_t j = 0; j < properties[i].queueCount; j++)
            {
                VkQueue queueVK;
                vkGetDeviceQueue(device, i, j, &queueVK);
                if (queueVK)
                {
                    ivec3 mitg;
                    mitg.x = properties[i].minImageTransferGranularity.width;
                    mitg.y = properties[i].minImageTransferGranularity.height;
                    mitg.z = properties[i].minImageTransferGranularity.depth;

                    QueueTypeFlags queueFlags{};
                    if (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                        queueFlags |= QueueType::GraphicsBit;
                    if (properties[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
                        queueFlags |= QueueType::ComputeBit;
                    if (properties[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
                        queueFlags |= QueueType::TransferBit;
                    if (properties[i].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT)
                        queueFlags |= QueueType::SparseBindingBit;
                    if (properties[i].queueFlags & VK_QUEUE_PROTECTED_BIT)
                        queueFlags |= QueueType::ProtectedBit;

                    VkBool32 present = VK_FALSE;
                    vkGetPhysicalDeviceSurfaceSupportKHR(RHII.GetGpu(), i, RHII.GetSurface()->ApiHandle(), &present);
                    if (present)
                        queueFlags |= QueueType::PresentBit;

                    Queue *queue = Queue::Create(queueVK, i, queueFlags, mitg, "Queue_" + std::to_string(i) + "_" + std::to_string(j));
                    Debug::SetObjectName(queue->ApiHandle(), queue->m_name);

                    auto it = s_allQueues.find(queueFlags.Value());
                    if (it == s_allQueues.end())
                    {
                        s_allQueues[queueFlags.Value()] = {};
                        s_allFlags.push_back(queueFlags.Value());
                    }

                    s_allQueues[queueFlags.Value()][queue->m_id] = queue;
                }
            }
        }

        std::sort(s_allFlags.begin(), s_allFlags.end());
    }

    void Queue::Clear()
    {
        for (auto &queueMap : s_allQueues)
            for (auto &queue : queueMap.second)
                Queue::Destroy(queue.second);

        s_allQueues.clear();
    }

    Queue *Queue::Get(QueueTypeFlags queueTypeFlags, int minImageGranularity)
    {
        std::lock_guard<std::mutex> lock(s_getMutex);

        // Find the best suitable flags in the sorted s_allFlags vector
        for (QueueTypeFlags::Type flags : s_allFlags)
        {
            uint64_t value = queueTypeFlags.Value();
            if (flags >= value && (flags & value) == value)
            {
                auto &queues = s_allQueues[flags];
                for (auto it = queues.begin(); it != queues.end(); it++)
                {
                    Queue *queue = it->second;
                    if (queue->GetImageGranularity().x <= minImageGranularity &&
                        queue->GetImageGranularity().y <= minImageGranularity &&
                        queue->GetImageGranularity().z <= minImageGranularity)
                    {
                        return queue;
                    }
                }
            }
        }

        PE_ERROR("Queue::Get() A queue with these flags is not available!");
        return nullptr;
    }

    Queue *GetQueueFromCmd(CommandBuffer *cmd)
    {
        Queue *renderQueue = RHII.GetRenderQueue();
        Queue *computeQueue = RHII.GetComputeQueue();
        Queue *transferQueue = RHII.GetTransferQueue();

        if (cmd->GetFamilyId() == renderQueue->GetFamilyId())
            return renderQueue;
        else if (cmd->GetFamilyId() == computeQueue->GetFamilyId())
            return computeQueue;
        else if (cmd->GetFamilyId() == transferQueue->GetFamilyId())
            return transferQueue;
        else
            return nullptr;
    }

    Semaphore *Queue::SubmitCommands(Semaphore *wait, const std::vector<CommandBuffer *> &cmds)
    {
        if (!cmds.empty())
        {
            const uint32_t frameIndex = RHII.GetFrameIndex();
            const auto &frameSemaphores = RHII.GetSemaphores(frameIndex);

            Semaphore *signal = frameSemaphores[1];
            signal->SetStageFlags(PipelineStage::AllCommandsBit);

            Queue *queue = nullptr;
            for (auto *cmd : cmds)
            {
                if (cmd)
                {
                    queue = GetQueueFromCmd(cmd);
                    break;
                }
            }

            queue->Submit(static_cast<uint32_t>(cmds.size()), cmds.data(), 1, &wait, 1, &signal);

            return signal;
        }

        return nullptr;
    }
}
#endif
