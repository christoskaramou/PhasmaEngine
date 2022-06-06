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
#include "Renderer/Fence.h"
#include "Renderer/Command.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Swapchain.h"
#include "Renderer/RHI.h"
#include "Renderer/Surface.h"

namespace pe
{
    Queue::Queue(QueueHandle handle,
                 uint32_t familyId,
                 uint32_t queueTypeFlags,
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
        uint32_t signalSemaphoresCount, Semaphore **signalSemaphores,
        Fence *signalFence)
    {
        std::vector<VkCommandBuffer> commandBuffersVK(commandBuffersCount);
        for (uint32_t i = 0; i < commandBuffersCount; i++)
        {
            commandBuffers[i]->m_fence = signalFence;
            commandBuffersVK[i] = commandBuffers[i]->Handle();
        }

        std::vector<VkSemaphore> waitSemaphoresVK(waitSemaphoresCount);
        for (uint32_t i = 0; i < waitSemaphoresCount; i++)
            waitSemaphoresVK[i] = waitSemaphores[i]->Handle();

        std::vector<VkSemaphore> signalSemaphoresVK(signalSemaphoresCount);
        for (uint32_t i = 0; i < signalSemaphoresCount; i++)
            signalSemaphoresVK[i] = signalSemaphores[i]->Handle();

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = waitSemaphoresCount;
        si.pWaitSemaphores = waitSemaphoresVK.data();
        si.pWaitDstStageMask = waitStages;
        si.commandBufferCount = commandBuffersCount;
        si.pCommandBuffers = commandBuffersVK.data();
        si.signalSemaphoreCount = signalSemaphoresCount;
        si.pSignalSemaphores = signalSemaphoresVK.data();

        VkFence fence = VK_NULL_HANDLE;
        if (signalFence)
        {
            signalFence->m_submitted = true;
            fence = signalFence->Handle();
        }
        PE_CHECK(vkQueueSubmit(m_handle, 1, &si, fence));
    }

    void Queue::SubmitAndWaitFence(
        uint32_t commandBuffersCount, CommandBuffer **commandBuffers,
        PipelineStageFlags *waitStages,
        uint32_t waitSemaphoresCount, Semaphore **waitSemaphores,
        uint32_t signalSemaphoresCount, Semaphore **signalSemaphores)
    {
        Fence *fence = Fence::GetNext();

        Submit(commandBuffersCount, commandBuffers, waitStages, waitSemaphoresCount,
               waitSemaphores, signalSemaphoresCount, signalSemaphores, fence);

        fence->Wait();
        fence->Reset();
        Fence::Return(fence);
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

        if (vkQueuePresentKHR(m_handle, &pi) != VK_SUCCESS)
            PE_ERROR("Present error!");
    }

    void Queue::WaitIdle()
    {
        PE_CHECK(vkQueueWaitIdle(m_handle));
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

                    uint32_t flags = properties[i].queueFlags & QueueType::AllExceptPresentBits;
                    VkBool32 present = VK_FALSE;
                    vkGetPhysicalDeviceSurfaceSupportKHR(RHII.gpu, i, RHII.surface->Handle(), &present);
                    if (present)
                        flags |= QueueType::PresentBit;

                    Queue *queue = Queue::Create(queueVK, i, flags, mitg, "Queue_Queue_" + std::to_string(i) + "_" + std::to_string(j));
                    Debug::SetObjectName(queue->Handle(), VK_OBJECT_TYPE_QUEUE, queue->name);

                    s_availableQueues[queue] = queue;
                    s_allQueues[queue] = queue;
                }
            }
        }
    }

    void Queue::Clear()
    {
        killThreads = true;
        
        for (auto &queueWait : s_queueWaits)
            queueWait.second.get();
        s_queueWaits.clear();

        for (auto &queue : s_allQueues)
            Queue::Destroy(queue.second);

        s_allQueues.clear();
        s_availableQueues.clear();
    }

    void Queue::CheckFutures()
    {
        // join finished futures
        for (auto it = s_queueWaits.begin(); it != s_queueWaits.end();)
        {
            if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::timeout)
            {
                Queue *queue = it->second.get();
                s_availableQueues[queue] = queue;

                it = s_queueWaits.erase(it);
            }
            else
                ++it;
        }
    }

    Queue *Queue::GetNext(uint32_t queueTypeFlags, int minImageGranularity)
    {
        s_availableQueuesReady = false;

        CheckFutures();

        for (auto &queuePair : s_availableQueues)
        {
            Queue *queue = queuePair.second;
            if ((queue->GetQueueTypeFlags() & queueTypeFlags) == queueTypeFlags &&
                queue->GetImageGranularity().x <= minImageGranularity &&
                queue->GetImageGranularity().y <= minImageGranularity &&
                queue->GetImageGranularity().z <= minImageGranularity)
            {
                s_availableQueues.erase(queue);
                s_availableQueuesReady = true;
                return queue;
            }
        }

        PE_ERROR("Queue::GetNext() No queue available!");

        s_availableQueuesReady = true;

        return nullptr;
    }

    void Queue::Return(Queue *queue)
    {
        if (!queue || !queue->Handle())
            return;

        if (s_allQueues.find(queue) == s_allQueues.end())
            PE_ERROR("Queue::Return() Queue not found!");

        if (s_queueWaits.find(queue) != s_queueWaits.end())
            return;

        auto func = [queue]()
        {
            queue->WaitIdle();

            while (!killThreads && !s_availableQueuesReady)
                std::this_thread::yield();

            return queue;
        };

        // start a new thread to return the queue
        auto future = std::async(std::launch::async, func);

        // add it to the deque
        s_queueWaits[queue] = std::move(future);
    }
}