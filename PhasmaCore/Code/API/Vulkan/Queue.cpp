#ifdef PE_VULKAN
#include "API/Queue.h"
#include "API/Semaphore.h"
#include "API/Command.h"
#include "API/Swapchain.h"
#include "API/RHI.h"
#include "API/Surface.h"

namespace pe
{
    CommandPool::CommandPool(Queue *queue, CommandPoolCreateFlags flags, const std::string &name)
    {
        m_queue = queue;
        m_flags = flags;

        VkCommandPoolCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.queueFamilyIndex = m_queue->GetFamilyId();
        cpci.flags = Translate<VkCommandPoolCreateFlags>(flags);

        VkCommandPool commandPool;
        PE_CHECK(vkCreateCommandPool(RHII.GetDevice(), &cpci, nullptr, &commandPool));
        m_apiHandle = commandPool;

        Debug::SetObjectName(m_apiHandle, name);
    }

    CommandPool::~CommandPool()
    {
        if (m_apiHandle)
        {
            vkDestroyCommandPool(RHII.GetDevice(), m_apiHandle, nullptr);
            m_apiHandle = {};
        }

        for (uint32_t i = 0; i < m_freeCmdStack.size(); i++)
        {
            m_freeCmdStack.top()->ApiHandle() = {};
            m_freeCmdStack.pop();
        }
    }

    void CommandPool::Reset()
    {
        PE_CHECK(vkResetCommandPool(RHII.GetDevice(), m_apiHandle, 0));
    }

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
          m_name{name},
          m_submissionsSemaphore{Semaphore::Create(true, name + "_submissionsSemaphore")}
    {
    }

    Queue::~Queue()
    {
        for (auto &pair : m_commandPools)
        {
            for (CommandPool *pool : pair.second)
            {
                CommandPool::Destroy(pool);
            }
        }

        Semaphore::Destroy(m_submissionsSemaphore);
    }

    void Queue::Submit(uint32_t commandBuffersCount, CommandBuffer *const *commandBuffers, Semaphore *wait, Semaphore *signal)
    {
        std::lock_guard<std::mutex> lock(s_submitMutex);

        VkSemaphoreSubmitInfo waitSemaphoreSubmitInfo{};
        std::vector<VkSemaphoreSubmitInfo> signalSemaphoreSubmitInfos{};
        std::vector<VkCommandBufferSubmitInfo> commandBufferSubmitInfos{};

        signalSemaphoreSubmitInfos.reserve(2);
        commandBufferSubmitInfos.reserve(commandBuffersCount);

        // wait semaphore
        if (wait)
        {
            waitSemaphoreSubmitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitSemaphoreSubmitInfo.semaphore = wait->ApiHandle();
            waitSemaphoreSubmitInfo.stageMask = Translate<VkPipelineStageFlags2>(wait->GetStageFlags());
        }

        // signal semaphores
        if (signal)
        {
            VkSemaphoreSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = signal->ApiHandle();
            info.stageMask = Translate<VkPipelineStageFlags2>(signal->GetStageFlags());
            signalSemaphoreSubmitInfos.push_back(info);
        }

        // signal semaphore for the command buffers
        {
            VkSemaphoreSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = m_submissionsSemaphore->ApiHandle();
            info.stageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            info.value = ++m_submissions;
            signalSemaphoreSubmitInfos.push_back(info);
        }

        // command buffers
        for (uint32_t i = 0; i < commandBuffersCount; i++)
        {
            CommandBuffer *cmd = commandBuffers[i];
            if (cmd)
            {
                VkCommandBufferSubmitInfo cbInfo{};
                cbInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
                cbInfo.commandBuffer = cmd->ApiHandle();
                commandBufferSubmitInfos.push_back(cbInfo);
                cmd->SetSubmissions(m_submissions);
            }
        }

        VkSubmitInfo2 si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        si.waitSemaphoreInfoCount = wait ? 1 : 0;
        si.pWaitSemaphoreInfos = &waitSemaphoreSubmitInfo;
        si.commandBufferInfoCount = static_cast<uint32_t>(commandBufferSubmitInfos.size());
        si.pCommandBufferInfos = commandBufferSubmitInfos.data();
        si.signalSemaphoreInfoCount = static_cast<uint32_t>(signalSemaphoreSubmitInfos.size());
        si.pSignalSemaphoreInfos = signalSemaphoreSubmitInfos.data();

        PE_CHECK(vkQueueSubmit2(m_apiHandle, 1, &si, nullptr));
    }

    void Queue::Present(Swapchain *swapchain, uint32_t imageIndex, Semaphore *wait)
    {
        PE_ERROR_IF(!(m_queueTypeFlags & QueueType::PresentBit), "Queue::Present: Queue does not support present!");

        VkSwapchainKHR swapchainVK = swapchain->ApiHandle();
        VkSemaphore waitSemaphoreVK = VK_NULL_HANDLE;
        if (wait)
            waitSemaphoreVK = wait->ApiHandle();

        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = waitSemaphoreVK ? 1 : 0;
        pi.pWaitSemaphores = &waitSemaphoreVK;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchainVK;
        pi.pImageIndices = &imageIndex;

        PE_CHECK(vkQueuePresentKHR(m_apiHandle, &pi));
    }

    void Queue::Wait()
    {
        m_submissionsSemaphore->Wait(m_submissions);
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

    Queue *Queue::Get(QueueTypeFlags queueTypeFlags, int minImageGranularity, const std::vector<Queue *> &exclude)
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
                        auto it = std::find(exclude.begin(), exclude.end(), queue);
                        if (it == exclude.end())
                            return queue;
                    }
                }
            }
        }

        return nullptr;
    }

    CommandBuffer *Queue::GetCommandBuffer(CommandPoolCreateFlags flags)
    {
        std::lock_guard<std::mutex> lock(m_cmdMutex);

        auto it = m_commandPools.find(std::this_thread::get_id());
        if (it == m_commandPools.end())
        {
            auto [it_new, inserted] = m_commandPools.emplace(std::this_thread::get_id(), std::vector<CommandPool *>());
            it = it_new;
        }
        auto &commandPools = it->second;

        // ResetCommandBuffer is always set, we use the same command pool per command buffer types
        // TODO: Find a better way to handle this, maybe reset the command pool at the end of the frame for specific command buffers
        flags |= CommandPoolCreate::ResetCommandBuffer;
        CommandPool *cp = nullptr;
        for (auto *commandPool : commandPools)
        {
            if (commandPool->GetFlags() == flags)
            {
                cp = commandPool;
                break;
            }
        }

        if (!cp)
        {
            cp = CommandPool::Create(this, flags, "CommandPool_" + m_name);
            commandPools.push_back(cp);
        }

        auto &stack = cp->m_freeCmdStack;
        if (stack.empty())
            return CommandBuffer::Create(cp, "CommandBuffer_" + m_name);

        CommandBuffer *cmd = stack.top();
        stack.pop();

        return cmd;
    }

    void Queue::ReturnCommandBuffer(CommandBuffer *cmd)
    {
        std::lock_guard<std::mutex> lock(m_cmdMutex);
        cmd->GetCommandPool()->m_freeCmdStack.push(cmd);
    }
}
#endif
