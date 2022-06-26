/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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

        this->name = name;
    }

    Queue::~Queue()
    {
    }

    void Queue::Submit(
        uint32_t commandBuffersCount, CommandBuffer **commandBuffers,
        PipelineStageFlags *waitStages,
        uint32_t waitSemaphoresCount, Semaphore **waitSemaphores,
        uint32_t signalSemaphoresCount, Semaphore **signalSemaphores)
    {
        // Translates from IHandles<T, ApiHandle> to VK handles are auto converted
        // via ApiHandle operator T() casts

        // CommandBuffers
        std::vector<VkCommandBuffer> commandBuffersVK(commandBuffersCount);
        for (uint32_t i = 0; i < commandBuffersCount; i++)
            commandBuffersVK[i] = commandBuffers[i]->Handle();

        // WaitSemaphores
        std::vector<VkSemaphore> waitSemaphoresVK(waitSemaphoresCount);
        for (uint32_t i = 0; i < waitSemaphoresCount; i++)
            waitSemaphoresVK[i] = waitSemaphores[i]->Handle();

        // SignalSemaphores
        uint32_t signalsCount = signalSemaphoresCount + commandBuffersCount;
        std::vector<VkSemaphore> signalSemaphoresVK(signalsCount);
        std::vector<uint64_t> semaphoreValues(signalsCount);

        for (uint32_t i = 0; i < signalsCount; i++)
        {
            if (i < signalSemaphoresCount)
            {
                signalSemaphoresVK[i] = signalSemaphores[i]->Handle();
                semaphoreValues[i] = 0;
            }
            else
            {
                uint32_t index = i - signalSemaphoresCount;
                // Increase the submit count of the current command buffer
                commandBuffers[index]->IncreaceSubmitionCount();
                // Add command buffer timeline semaphores as signal semaphores
                signalSemaphoresVK[i] = commandBuffers[index]->GetSemaphore()->Handle();
                // Set the signal values from the current command buffer submition counter
                semaphoreValues[i] = commandBuffers[index]->GetSumbitionCount();
            }
        }

        // Pipeline stages
        std::vector<VkPipelineStageFlags> waitStagesVK(waitSemaphoresCount);
        for (uint32_t i = 0; i < waitSemaphoresCount; i++)
            waitStagesVK[i] = GetPipelineStageFlagsVK(waitStages[i]);

        VkTimelineSemaphoreSubmitInfo timelineInfo{};
        timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timelineInfo.pNext = NULL;
        timelineInfo.waitSemaphoreValueCount = 0;
        timelineInfo.pWaitSemaphoreValues = nullptr;
        timelineInfo.signalSemaphoreValueCount = signalsCount;
        timelineInfo.pSignalSemaphoreValues = semaphoreValues.data();

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.pNext = &timelineInfo;
        si.waitSemaphoreCount = waitSemaphoresCount;
        si.pWaitSemaphores = waitSemaphoresVK.data();
        si.pWaitDstStageMask = waitStagesVK.data();
        si.commandBufferCount = commandBuffersCount;
        si.pCommandBuffers = commandBuffersVK.data();
        si.signalSemaphoreCount = signalsCount;
        si.pSignalSemaphores = signalSemaphoresVK.data();

        PE_CHECK(vkQueueSubmit(m_handle, 1, &si, nullptr));
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

                    s_availableQueues[queue->GetID()] = queue;
                    s_allQueues[queue->GetID()] = queue;
                }
            }
        }
    }

    void Queue::Clear()
    {
        for (auto &queue : s_allQueues)
            Queue::Destroy(queue.second);

        s_allQueues.clear();
        s_availableQueues.clear();
    }

    Queue *Queue::GetNext(QueueTypeFlags queueType, int minImageGranularity)
    {
        std::lock_guard<std::mutex> lock(s_getNextMutex);

        for (auto &queuePair : s_availableQueues)
        {
            Queue &queue = *queuePair.second;
            if ((queue.GetQueueTypeFlags() & queueType) == queueType &&
                queue.GetImageGranularity().x <= minImageGranularity &&
                queue.GetImageGranularity().y <= minImageGranularity &&
                queue.GetImageGranularity().z <= minImageGranularity)
            {
                s_availableQueues.erase(queue.GetID());
                return &queue;
            }
        }

        PE_ERROR("Queue::GetNext() A queue of this family type is not available!");
        return nullptr;
    }

    void Queue::Return(Queue *queue)
    {
        std::lock_guard<std::mutex> lock(s_returnMutex);

        // CHECKS
        // -------------------------------------------------
        if (!queue || !queue->Handle())
            PE_ERROR("Queue::Return() Invalid queue!");

        if (s_allQueues.find(queue->GetID()) == s_allQueues.end())
            PE_ERROR("Queue::Return() Queue not found!");

        if (s_availableQueues.find(queue->GetID()) != s_availableQueues.end())
            PE_ERROR("Queue::Return() Queue is already returned!");
        // -------------------------------------------------

        s_availableQueues[queue->GetID()] = queue;
    }
}