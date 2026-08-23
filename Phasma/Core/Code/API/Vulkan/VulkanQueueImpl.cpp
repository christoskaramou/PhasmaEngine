#include "API/Vulkan/VulkanQueueImpl.h"
#include "API/Command.h"
#include "API/RHI.h"
#include "API/Semaphore.h"
#include "API/Swapchain.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"
#include "API/Vulkan/VulkanRHITypeUtils.h"
#include "API/Vulkan/VulkanSemaphoreImpl.h"
#include "API/Vulkan/VulkanSwapchainImpl.h"

namespace pe
{
    namespace
    {
        std::mutex s_submitMutex{};
    }

    VulkanQueueImpl::VulkanQueueImpl(Queue *owner, uint32_t familyId, const std::string &name)
        : m_owner{owner},
          m_name{name}
    {
        m_apiHandle = VulkanRhi::Device().getQueue(familyId, 0);
        m_owner->m_apiHandle = detail::ToUintPtr(m_apiHandle);
        Debug::SetObjectName(m_apiHandle, m_name);
    }

    VulkanQueueImpl::~VulkanQueueImpl() = default;

    void VulkanQueueImpl::Submit(uint32_t commandBuffersCount,
                                 CommandBuffer *const *commandBuffers,
                                 Semaphore *wait,
                                 Semaphore *signal,
                                 Semaphore *submissionsSemaphore,
                                 uint64_t submissionValue)
    {
        std::lock_guard<std::mutex> lock(s_submitMutex);

        if (RHII.GetCaps().sync2)
        {
            vk::SemaphoreSubmitInfo waitSemaphoreSubmitInfo{};
            std::vector<vk::SemaphoreSubmitInfo> signalSemaphoreSubmitInfos{};
            std::vector<vk::CommandBufferSubmitInfo> commandBufferSubmitInfos{};

            signalSemaphoreSubmitInfos.reserve(2);
            commandBufferSubmitInfos.reserve(commandBuffersCount);

            if (wait)
            {
                waitSemaphoreSubmitInfo.semaphore = GetVulkanSemaphore(wait);
                waitSemaphoreSubmitInfo.stageMask = ToVkPipelineStageFlags(wait->GetStageFlags());
            }

            if (signal)
            {
                vk::SemaphoreSubmitInfo info{};
                info.semaphore = GetVulkanSemaphore(signal);
                info.stageMask = ToVkPipelineStageFlags(signal->GetStageFlags());
                signalSemaphoreSubmitInfos.push_back(info);
            }

            if (submissionsSemaphore)
            {
                vk::SemaphoreSubmitInfo info{};
                info.semaphore = GetVulkanSemaphore(submissionsSemaphore);
                info.stageMask = vk::PipelineStageFlagBits2::eBottomOfPipe;
                info.value = submissionValue;
                signalSemaphoreSubmitInfos.push_back(info);
            }

            for (uint32_t i = 0; i < commandBuffersCount; i++)
            {
                CommandBuffer *cmd = commandBuffers[i];
                if (cmd)
                {
                    vk::CommandBufferSubmitInfo cbInfo{};
                    cbInfo.commandBuffer = GetVulkanCommandBuffer(cmd);
                    commandBufferSubmitInfos.push_back(cbInfo);
                    cmd->SetSubmission(submissionValue);
                }
            }

            vk::SubmitInfo2 si{};
            si.waitSemaphoreInfoCount = wait ? 1 : 0;
            si.pWaitSemaphoreInfos = &waitSemaphoreSubmitInfo;
            si.commandBufferInfoCount = static_cast<uint32_t>(commandBufferSubmitInfos.size());
            si.pCommandBufferInfos = commandBufferSubmitInfos.data();
            si.signalSemaphoreInfoCount = static_cast<uint32_t>(signalSemaphoreSubmitInfos.size());
            si.pSignalSemaphoreInfos = signalSemaphoreSubmitInfos.data();

            auto result = m_apiHandle.submit2(1, &si, nullptr);
            switch (result)
            {
            case vk::Result::eSuccess:
                break;
            case vk::Result::eErrorDeviceLost:
                PE_WARN("[Queue] submit: device lost");
                break;
            case vk::Result::eErrorOutOfDeviceMemory:
            case vk::Result::eErrorOutOfHostMemory:
                PE_WARN("[Queue] submit: out of memory (%d)", static_cast<int>(result));
                break;
            default:
                PE_WARN("[Queue] submit failed with VkResult=%d", static_cast<int>(result));
                break;
            }
            return;
        }

        std::vector<vk::Semaphore> waitSemaphores{};
        std::vector<vk::PipelineStageFlags> waitStages{};
        std::vector<uint64_t> waitValues{};
        std::vector<vk::Semaphore> signalSemaphores{};
        std::vector<uint64_t> signalValues{};
        std::vector<vk::CommandBuffer> commandBufferInfos{};

        commandBufferInfos.reserve(commandBuffersCount);

        if (wait)
        {
            vk::PipelineStageFlags stageMask = ToVkPipelineStageFlagsLegacy(wait->GetStageFlags());
            if (!stageMask)
                stageMask = vk::PipelineStageFlagBits::eAllCommands;
            waitSemaphores.push_back(GetVulkanSemaphore(wait));
            waitStages.push_back(stageMask);
            waitValues.push_back(0);
        }

        if (signal)
        {
            signalSemaphores.push_back(GetVulkanSemaphore(signal));
            signalValues.push_back(0);
        }

        if (submissionsSemaphore)
        {
            signalSemaphores.push_back(GetVulkanSemaphore(submissionsSemaphore));
            signalValues.push_back(submissionValue);
        }

        for (uint32_t i = 0; i < commandBuffersCount; i++)
        {
            CommandBuffer *cmd = commandBuffers[i];
            if (cmd)
            {
                commandBufferInfos.push_back(GetVulkanCommandBuffer(cmd));
                cmd->SetSubmission(submissionValue);
            }
        }

        vk::TimelineSemaphoreSubmitInfo timelineInfo{};
        timelineInfo.waitSemaphoreValueCount = static_cast<uint32_t>(waitValues.size());
        timelineInfo.pWaitSemaphoreValues = waitValues.data();
        timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
        timelineInfo.pSignalSemaphoreValues = signalValues.data();

        vk::SubmitInfo si{};
        si.pNext = (!waitValues.empty() || !signalValues.empty()) ? &timelineInfo : nullptr;
        si.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
        si.pWaitSemaphores = waitSemaphores.data();
        si.pWaitDstStageMask = waitStages.data();
        si.commandBufferCount = static_cast<uint32_t>(commandBufferInfos.size());
        si.pCommandBuffers = commandBufferInfos.data();
        si.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
        si.pSignalSemaphores = signalSemaphores.data();

        auto result = m_apiHandle.submit(1, &si, nullptr);
        switch (result)
        {
        case vk::Result::eSuccess:
            break;
        case vk::Result::eErrorDeviceLost:
            PE_WARN("[Queue] submit: device lost");
            break;
        case vk::Result::eErrorOutOfDeviceMemory:
        case vk::Result::eErrorOutOfHostMemory:
            PE_WARN("[Queue] submit: out of memory (%d)", static_cast<int>(result));
            break;
        default:
            PE_WARN("[Queue] submit failed with VkResult=%d", static_cast<int>(result));
            break;
        }
    }

    void VulkanQueueImpl::Present(Swapchain *swapchain, uint32_t imageIndex, Semaphore *wait)
    {
        std::lock_guard<std::mutex> lock(s_submitMutex);

        vk::SwapchainKHR vkSwap = pe::GetVulkanSwapchain(swapchain);
        vk::Semaphore waitVk = GetVulkanSemaphore(wait);
        vk::PresentInfoKHR pi{};
        pi.waitSemaphoreCount = wait ? 1 : 0;
        pi.pWaitSemaphores = wait ? &waitVk : nullptr;
        pi.swapchainCount = 1;
        pi.pSwapchains = &vkSwap;
        pi.pImageIndices = &imageIndex;

        VulkanSwapchainImpl *swapchainVk = VulkanSwapchainImpl::From(swapchain);
        const bool presentWaitEnabled = swapchainVk->m_presentWait || swapchainVk->m_presentWait2;
        const uint64_t presentId = presentWaitEnabled ? ++swapchainVk->m_presentId : 0;
        vk::PresentIdKHR presentIdInfo{};
        vk::PresentId2KHR presentId2Info{};
        if (swapchainVk->m_presentWait2)
        {
            presentId2Info.swapchainCount = 1;
            presentId2Info.pPresentIds = &presentId;
            pi.pNext = &presentId2Info;
        }
        else if (swapchainVk->m_presentWait)
        {
            presentIdInfo.swapchainCount = 1;
            presentIdInfo.pPresentIds = &presentId;
            pi.pNext = &presentIdInfo;
        }

        try
        {
            auto result = m_apiHandle.presentKHR(pi);
            // VK_SUBOPTIMAL_KHR is a success code (vulkan.hpp returns it, does not throw). We
            // deliberately create the swapchain with preTransform=IDENTITY instead of currentTransform
            // (see VulkanSwapchainImpl) so the compositor handles Android panel rotation; some drivers
            // then report every present as suboptimal. That is expected and benign here — treating it
            // as out-of-date would rebuild the swapchain every frame. Only a genuine
            // VK_ERROR_OUT_OF_DATE_KHR (the surface actually changed size) rebuilds, via the frame
            // loop's SwapchainOutOfDateError handler.
            if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
                PE_ERROR("[Queue] Failed to present swapchain image!");
            else if (presentWaitEnabled)
                swapchainVk->m_pendingPresentId = presentId;
        }
        catch (vk::OutOfDateKHRError &)
        {
            throw SwapchainOutOfDateError{};
        }
        catch (vk::SystemError &e)
        {
            PE_ERROR("[Queue] Failed to present swapchain image: %s", e.what());
        }
    }

    void VulkanQueueImpl::WaitIdle()
    {
        m_apiHandle.waitIdle();
    }
} // namespace pe
