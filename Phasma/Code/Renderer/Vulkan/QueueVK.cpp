#include "Renderer/Queue.h"
#include "Renderer/Semaphore.h"
#include "Renderer/Command.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Swapchain.h"
#include "Renderer/RHI.h"
#include "Renderer/Surface.h"

namespace pe
{
    Queue::Queue(QueueHandle handle,
                 uint32_t familyId,
                 QueueTypeFlags queueTypeFlags,
                 ivec3 imageGranularity,
                 const std::string &name)
    {
        m_handle = handle;
        m_familyId = familyId;
        m_queueTypeFlags = queueTypeFlags;
        m_imageGranularity = imageGranularity;

        m_semaphore = Semaphore::Create(true, name + "_queue_semaphore");
        m_submitions = 0;

        this->name = name;
    }

    Queue::~Queue()
    {
        Semaphore::Destroy(m_semaphore);
    }

    void Queue::Submit(uint32_t commandBuffersCount, CommandBuffer **commandBuffers,
                       uint32_t waitSemaphoresCount,
                       PipelineStageFlags *waitStages, Semaphore **waitSemaphores,
                       uint32_t signalSemaphoresCount,
                       PipelineStageFlags *signalStages, Semaphore **signalSemaphores)
    {
        std::lock_guard<std::mutex> lock(s_submitMutex);

        std::vector<VkCommandBufferSubmitInfo> commandBufferSubmitInfos(commandBuffersCount);
        std::vector<VkSemaphoreSubmitInfo> waitSemaphoreSubmitInfos(waitSemaphoresCount);
        // semaphores requested + command buffers semaphores + queue semaphore
        std::vector<VkSemaphoreSubmitInfo> signalSemaphoreSubmitInfos(signalSemaphoresCount + commandBuffersCount + 1);

        // CommandBuffers
        for (uint32_t i = 0; i < commandBuffersCount; i++)
        {
            commandBufferSubmitInfos[i].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
            commandBufferSubmitInfos[i].commandBuffer = commandBuffers[i]->Handle();
        }

        // WaitSemaphores
        for (uint32_t i = 0; i < waitSemaphoresCount; i++)
        {
            waitSemaphoreSubmitInfos[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitSemaphoreSubmitInfos[i].semaphore = waitSemaphores[i]->Handle();
            waitSemaphoreSubmitInfos[i].stageMask = Translate<VkPipelineStageFlags2>(waitStages[i]);
            waitSemaphoreSubmitInfos[i].value = waitSemaphores[i]->GetValue();
        }

        // SignalSemaphores
        uint32_t signalsCount = static_cast<uint32_t>(signalSemaphoreSubmitInfos.size());
        uint32_t i = 0;
        uint32_t count = 0;

        // semaphores requested
        count += signalSemaphoresCount;
        for (; i < count; i++)
        {
            signalSemaphoreSubmitInfos[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalSemaphoreSubmitInfos[i].semaphore = signalSemaphores[i]->Handle();
            signalSemaphoreSubmitInfos[i].stageMask = Translate<VkPipelineStageFlags2>(signalStages[i]);
            // TODO: figure the value, +1 is probably wrong, for now income signals are not timeline semaphores either way
            signalSemaphoreSubmitInfos[i].value = signalSemaphores[i]->GetValue() + 1;
        }

        // command buffers semaphores
        count += commandBuffersCount;
        for (; i < count; i++)
        {
            // Increase the submit count of the current command buffer
            uint32_t index = i - signalSemaphoresCount;
            commandBuffers[index]->IncreaceSubmitionCount();
            signalSemaphoreSubmitInfos[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalSemaphoreSubmitInfos[i].semaphore = commandBuffers[index]->GetSemaphore()->Handle();
            signalSemaphoreSubmitInfos[i].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signalSemaphoreSubmitInfos[i].value = commandBuffers[index]->GetSumbitionCount();
        }

        // queue semaphore
        {
            m_semaphore->Wait(m_submitions); // Wait previous submition so we can submit again
            m_submitions++;
            signalSemaphoreSubmitInfos[i].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signalSemaphoreSubmitInfos[i].semaphore = m_semaphore->Handle();
            signalSemaphoreSubmitInfos[i].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signalSemaphoreSubmitInfos[i].value = m_submitions;
        }

        VkSubmitInfo2 si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        si.waitSemaphoreInfoCount = waitSemaphoresCount;
        si.pWaitSemaphoreInfos = waitSemaphoreSubmitInfos.data();
        si.commandBufferInfoCount = commandBuffersCount;
        si.pCommandBufferInfos = commandBufferSubmitInfos.data();
        si.signalSemaphoreInfoCount = signalsCount;
        si.pSignalSemaphoreInfos = signalSemaphoreSubmitInfos.data();

        PE_CHECK(vkQueueSubmit2(m_handle, 1, &si, nullptr));
    }

    void Queue::Present(
        uint32_t swapchainCount, Swapchain **swapchains,
        uint32_t *imageIndices,
        uint32_t waitSemaphoreCount, Semaphore **waitSemaphores)
    {
        std::vector<VkSwapchainKHR> swapchainsVK(swapchainCount);
        for (uint32_t i = 0; i < swapchainCount; i++)
            swapchainsVK[i] = swapchains[i]->Handle();

        std::vector<VkSemaphore> waitSemaphoresVK(waitSemaphoreCount);
        for (uint32_t i = 0; i < waitSemaphoreCount; i++)
            waitSemaphoresVK[i] = waitSemaphores[i]->Handle();

        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = waitSemaphoreCount;
        pi.pWaitSemaphores = waitSemaphoresVK.data();
        pi.swapchainCount = swapchainCount;
        pi.pSwapchains = swapchainsVK.data();
        pi.pImageIndices = imageIndices;

        PE_CHECK(vkQueuePresentKHR(m_handle, &pi));
    }

    void Queue::WaitIdle()
    {
        PE_CHECK(vkQueueWaitIdle(m_handle));
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

    void Queue::Init(GpuHandle gpu, DeviceHandle device, SurfaceHandle surface)
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
                    vkGetPhysicalDeviceSurfaceSupportKHR(RHII.GetGpu(), i, RHII.GetSurface()->Handle(), &present);
                    if (present)
                        queueFlags |= QueueType::PresentBit;

                    Queue *queue = Queue::Create(queueVK, i, queueFlags, mitg, "Queue_Queue_" + std::to_string(i) + "_" + std::to_string(j));
                    Debug::SetObjectName(queue->Handle(), ObjectType::Queue, queue->name);

                    auto it = s_allQueues.find(queueFlags.Value());
                    if (it == s_allQueues.end())
                    {
                        s_allQueues[queueFlags.Value()] = std::unordered_map<size_t, Queue *>();
                        s_allFlags.push_back(queueFlags.Value());
                    }

                    s_allQueues[queueFlags.Value()][queue->GetID()] = queue;
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

    Queue *Queue::GetNext(QueueTypeFlags queueTypeFlags, int minImageGranularity)
    {
        std::lock_guard<std::mutex> lock(s_getNextMutex);

        // Find the best suitable flags in the sorted s_allFlags vector
        for (QueueTypeFlags::Type flags : s_allFlags)
        {
            if (flags >= queueTypeFlags.Value() && (flags & queueTypeFlags.Value()) == queueTypeFlags.Value())
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

        PE_ERROR("Queue::GetNext() A queue with these flags is not available!");
        return nullptr;
    }
}