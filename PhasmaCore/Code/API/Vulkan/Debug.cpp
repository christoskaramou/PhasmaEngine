#ifdef PE_VULKAN
#include "API/Debug.h"
#include "API/RHI.h"
#include "API/Queue.h"
#include "API/Command.h"
#include "RenderDoc/renderdoc_app.h"

namespace pe
{
#if PE_DEBUG_MODE
    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = VK_NULL_HANDLE;
    PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT = VK_NULL_HANDLE;
    PFN_vkQueueBeginDebugUtilsLabelEXT vkQueueBeginDebugUtilsLabelEXT = VK_NULL_HANDLE;
    PFN_vkQueueEndDebugUtilsLabelEXT vkQueueEndDebugUtilsLabelEXT = VK_NULL_HANDLE;
    PFN_vkQueueInsertDebugUtilsLabelEXT vkQueueInsertDebugUtilsLabelEXT = VK_NULL_HANDLE;
    PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT = VK_NULL_HANDLE;
    PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT = VK_NULL_HANDLE;
    PFN_vkCmdInsertDebugUtilsLabelEXT vkCmdInsertDebugUtilsLabelEXT = VK_NULL_HANDLE;
    PFN_vkSetDebugUtilsObjectTagEXT vkSetDebugUtilsObjectTagEXT = VK_NULL_HANDLE;
    PFN_vkSubmitDebugUtilsMessageEXT vkSubmitDebugUtilsMessageEXT = VK_NULL_HANDLE;

    const vec4 color{0.0f, 0.0f, 0.0f, 1.0f};

    // Get function pointers for the debug report extensions from the device
    void Debug::Init(InstanceApiHandle instance)
    {
        PE_ERROR_IF(s_instance, "Already initialized!");
        PE_ERROR_IF(!instance, "Invalid instance!");

        s_instance = instance;

        vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(s_instance, "vkCreateDebugUtilsMessengerEXT");
        vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(s_instance, "vkDestroyDebugUtilsMessengerEXT");
        vkSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(s_instance, "vkSetDebugUtilsObjectNameEXT");
        vkQueueBeginDebugUtilsLabelEXT = (PFN_vkQueueBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(s_instance, "vkQueueBeginDebugUtilsLabelEXT");
        vkQueueInsertDebugUtilsLabelEXT = (PFN_vkQueueInsertDebugUtilsLabelEXT)vkGetInstanceProcAddr(s_instance, "vkQueueInsertDebugUtilsLabelEXT");
        vkQueueEndDebugUtilsLabelEXT = (PFN_vkQueueEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(s_instance, "vkQueueEndDebugUtilsLabelEXT");
        vkCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(s_instance, "vkCmdBeginDebugUtilsLabelEXT");
        vkCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(s_instance, "vkCmdEndDebugUtilsLabelEXT");
        vkCmdInsertDebugUtilsLabelEXT = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetInstanceProcAddr(s_instance, "vkCmdInsertDebugUtilsLabelEXT");
    }

    std::string GetDebugMessageString(VkDebugUtilsMessageTypeFlagsEXT value)
    {
        switch (value)
        {
        case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
            return "General";
        case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
            return "Validation";
        case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
            return "Performance";
        }

        return "Unknown";
    }

    std::string GetDebugSeverityString(VkDebugUtilsMessageSeverityFlagBitsEXT value)
    {
        switch (value)
        {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            return "Verbose";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            return "Info";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            return "Warning";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            return "Error";
        }

        return "Unknown";
    }

    uint32_t VKAPI_CALL MessageCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        uint32_t messageType,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData)
    {
        if (messageSeverity > VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
            std::cerr << GetDebugMessageString(messageType) << " "
                      << GetDebugSeverityString(messageSeverity) << " from \""
                      << pCallbackData->pMessageIdName << "\": "
                      << pCallbackData->pMessage << std::endl;

        return VK_FALSE;
    }

    void Debug::CreateDebugMessenger()
    {
#if defined(_DEBUG) && PE_DEBUG_MESSENGER == 1
        PE_ERROR_IF(!s_instance, "A valid instance handle is required to initialize debug messenger!");

        if (!vkCreateDebugUtilsMessengerEXT)
            return;

        VkDebugUtilsMessengerCreateInfoEXT dumci{};
        dumci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dumci.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dumci.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dumci.pfnUserCallback = MessageCallback;

        VkDebugUtilsMessengerEXT debugMessengerVK;
        vkCreateDebugUtilsMessengerEXT(s_instance, &dumci, nullptr, &debugMessengerVK);
        s_debugMessenger = debugMessengerVK;
#endif
    }

    void Debug::DestroyDebugMessenger()
    {
#if defined(_DEBUG) && PE_DEBUG_MESSENGER == 1
        if (s_debugMessenger)
        {
            vkDestroyDebugUtilsMessengerEXT(s_instance, s_debugMessenger, nullptr);
        }
#endif
    }

    void Debug::SetObjectName(uint64_t handle, ObjectType type, const std::string &name)
    {
        if (!vkSetDebugUtilsObjectNameEXT)
            return;

        VkDebugUtilsObjectNameInfoEXT objectInfo{};
        objectInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        objectInfo.pNext = VK_NULL_HANDLE;
        objectInfo.objectType = Translate<VkObjectType>(type);
        objectInfo.objectHandle = handle;
        objectInfo.pObjectName = name.c_str();

        vkSetDebugUtilsObjectNameEXT(RHII.GetDevice(), &objectInfo);
    }

    void Debug::BeginQueueRegion(Queue *queue, const std::string &name)
    {
        if (!vkQueueBeginDebugUtilsLabelEXT)
            return;

        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pNext = VK_NULL_HANDLE;
        label.pLabelName = name.c_str();
        label.color[0] = color.x;
        label.color[1] = color.y;
        label.color[2] = color.z;
        label.color[3] = color.w;

        vkQueueBeginDebugUtilsLabelEXT(queue->ApiHandle(), &label);
    }

    void Debug::InsertQueueLabel(Queue *queue, const std::string &name)
    {
        if (!vkQueueInsertDebugUtilsLabelEXT)
            return;

        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pNext = VK_NULL_HANDLE;
        label.pLabelName = name.c_str();
        label.color[0] = color.x;
        label.color[1] = color.y;
        label.color[2] = color.z;
        label.color[3] = color.w;

        vkQueueInsertDebugUtilsLabelEXT(queue->ApiHandle(), &label);
    }

    void Debug::EndQueueRegion(Queue *queue)
    {
        if (!vkQueueEndDebugUtilsLabelEXT)
            return;

        vkQueueEndDebugUtilsLabelEXT(queue->ApiHandle());
    }

    void Debug::BeginCmdRegion(CommandBuffer *cmd, const std::string &name)
    {
        if (!vkCmdBeginDebugUtilsLabelEXT)
            return;

        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pNext = VK_NULL_HANDLE;
        label.pLabelName = name.c_str();
        label.color[0] = color.x;
        label.color[1] = color.y;
        label.color[2] = color.z;
        label.color[3] = color.w;

        cmd->SetPassName(name);

        vkCmdBeginDebugUtilsLabelEXT(cmd->ApiHandle(), &label);

        if (cmd->m_gpuTimerInfos.size() < cmd->m_gpuTimerInfosCount + 1)
        {
            for (int i = 0; i < 10; ++i)
            {
                GpuTimerInfo info{};
                info.timer = GpuTimer::Create("gpu timer_" + std::to_string(cmd->m_gpuTimerInfos.size()));
                cmd->m_gpuTimerInfos.push_back(info);
            }
        }

        GpuTimerInfo &timerInfo = cmd->m_gpuTimerInfos[cmd->m_gpuTimerInfosCount];
        timerInfo.timer->Start(cmd);
        timerInfo.name = name;
        timerInfo.depth = cmd->m_gpuTimerIdsStack.size();
        cmd->m_gpuTimerIdsStack.push(cmd->m_gpuTimerInfosCount);
        cmd->m_gpuTimerInfosCount++;
    }

    void Debug::InsertCmdLabel(CommandBuffer *cmd, const std::string &name)
    {
        if (!vkCmdInsertDebugUtilsLabelEXT)
            return;

        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pNext = VK_NULL_HANDLE;
        label.pLabelName = name.c_str();
        label.color[0] = color.x;
        label.color[1] = color.y;
        label.color[2] = color.z;
        label.color[3] = color.w;

        vkCmdInsertDebugUtilsLabelEXT(cmd->ApiHandle(), &label);
    }

    void Debug::EndCmdRegion(CommandBuffer *cmd)
    {
        if (!vkCmdEndDebugUtilsLabelEXT)
            return;

        vkCmdEndDebugUtilsLabelEXT(cmd->ApiHandle());

        cmd->m_gpuTimerInfos[cmd->m_gpuTimerIdsStack.top()].timer->End();
        cmd->m_gpuTimerIdsStack.pop();
    }
#endif
};
#endif
