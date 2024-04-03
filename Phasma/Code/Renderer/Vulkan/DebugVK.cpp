#if PE_VULKAN
#include "Renderer/Debug.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "RenderDoc/renderdoc_app.h"
#include "GUI/GUI.h"

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

    uint32_t s_labelDepth = 1;

    // Get function pointers for the debug report extensions from the device
    void Debug::Init(InstanceHandle instance)
    {
        PE_ERROR_IF(s_instance, "Already initialized!");
        PE_ERROR_IF(!instance, "Invalid initializing instance!");

        s_instance = instance;

        if (!vkCreateDebugUtilsMessengerEXT)
            vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(s_instance, "vkCreateDebugUtilsMessengerEXT");

        if (!vkDestroyDebugUtilsMessengerEXT)
            vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(s_instance, "vkDestroyDebugUtilsMessengerEXT");

        if (!vkSetDebugUtilsObjectNameEXT)
            vkSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(s_instance, "vkSetDebugUtilsObjectNameEXT");

        if (!vkQueueBeginDebugUtilsLabelEXT)
            vkQueueBeginDebugUtilsLabelEXT = (PFN_vkQueueBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(s_instance, "vkQueueBeginDebugUtilsLabelEXT");

        if (!vkQueueInsertDebugUtilsLabelEXT)
            vkQueueInsertDebugUtilsLabelEXT = (PFN_vkQueueInsertDebugUtilsLabelEXT)vkGetInstanceProcAddr(s_instance, "vkQueueInsertDebugUtilsLabelEXT");

        if (!vkQueueEndDebugUtilsLabelEXT)
            vkQueueEndDebugUtilsLabelEXT = (PFN_vkQueueEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(s_instance, "vkQueueEndDebugUtilsLabelEXT");

        if (!vkCmdBeginDebugUtilsLabelEXT)
            vkCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(s_instance, "vkCmdBeginDebugUtilsLabelEXT");

        if (!vkCmdEndDebugUtilsLabelEXT)
            vkCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(s_instance, "vkCmdEndDebugUtilsLabelEXT");

        if (!vkCmdInsertDebugUtilsLabelEXT)
            vkCmdInsertDebugUtilsLabelEXT = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetInstanceProcAddr(s_instance, "vkCmdInsertDebugUtilsLabelEXT");
    }

    void Debug::GetInstanceUtils(std::vector<const char *> &instanceExtensions,
                                 std::vector<const char *> &instanceLayers)
    {
        if (RHII.IsInstanceExtensionValid("VK_EXT_debug_utils"))
            instanceExtensions.push_back("VK_EXT_debug_utils");

#if defined(_DEBUG) && PE_DEBUG_MESSENGER
        // === Debug Layers ============================
        // To use these debug layers, here is assumed VulkanSDK is installed
        // Also VK_LAYER_PATH environmental variable has to be created and target the bin
        // e.g VK_LAYER_PATH = C:\VulkanSDK\1.2.189.1\Bin
        if (RHII.IsInstanceLayerValid("VK_LAYER_KHRONOS_validation"))
            instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
#endif
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
            std::cerr
                << GetDebugMessageString(messageType) << " "
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

        const float color = 1.f - 1.f / ++s_labelDepth;

        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pNext = VK_NULL_HANDLE;
        label.pLabelName = name.c_str();
        label.color[0] = color;
        label.color[1] = color;
        label.color[2] = color;
        label.color[3] = 1.f;

        vkQueueBeginDebugUtilsLabelEXT(queue->ApiHandle(), &label);
    }

    void Debug::InsertQueueLabel(Queue *queue, const std::string &name)
    {
        if (!vkQueueInsertDebugUtilsLabelEXT)
            return;

        const float color = 1.f / s_labelDepth;

        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pNext = VK_NULL_HANDLE;
        label.pLabelName = name.c_str();
        label.color[0] = color;
        label.color[1] = color;
        label.color[2] = color;
        label.color[3] = 1.f;

        vkQueueInsertDebugUtilsLabelEXT(queue->ApiHandle(), &label);
    }

    void Debug::EndQueueRegion(Queue *queue)
    {
        if (!vkQueueEndDebugUtilsLabelEXT)
            return;

        vkQueueEndDebugUtilsLabelEXT(queue->ApiHandle());
        s_labelDepth--;
    }

    void Debug::BeginCmdRegion(CommandBuffer *cmd, const std::string &name)
    {
        if (!vkCmdBeginDebugUtilsLabelEXT)
            return;

        const float color = 1.f - 1.f / ++s_labelDepth;

        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pNext = VK_NULL_HANDLE;
        label.pLabelName = name.c_str();
        label.color[0] = color;
        label.color[1] = color;
        label.color[2] = color;
        label.color[3] = 1.f;

        cmd->SetPassName(name);

        vkCmdBeginDebugUtilsLabelEXT(cmd->ApiHandle(), &label);

        if (std::this_thread::get_id() == e_MainThreadID) // TODO: make it work with other threads
        {
            std::lock_guard<std::mutex> lock(s_infosMutex);

            GpuTimer *timer = GpuTimer::GetFree();
            timer->Start(cmd);
            s_timers[cmd].push_back({timer, name});
            s_timeInfos[timer] = {timer, "", 0};
        }
    }

    void Debug::InsertCmdLabel(CommandBuffer *cmd, const std::string &name)
    {
        if (!vkCmdInsertDebugUtilsLabelEXT)
            return;

        const float color = 1.f / s_labelDepth;

        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pNext = VK_NULL_HANDLE;
        label.pLabelName = name.c_str();
        label.color[0] = color;
        label.color[1] = color;
        label.color[2] = color;
        label.color[3] = 1.f;

        vkCmdInsertDebugUtilsLabelEXT(cmd->ApiHandle(), &label);
    }

    void Debug::EndCmdRegion(CommandBuffer *cmd)
    {
        if (!vkCmdEndDebugUtilsLabelEXT)
            return;

        vkCmdEndDebugUtilsLabelEXT(cmd->ApiHandle());
        s_labelDepth--;

        if (std::this_thread::get_id() == e_MainThreadID) // TODO: make it work with other threads
        {
            std::lock_guard<std::mutex> lock(s_infosMutex);

            auto &timerInfos = s_timers[cmd];
            auto &timerInfo = timerInfos.back();
            timerInfo.timer->End();

            auto &timeInfo = s_timeInfos[timerInfo.timer];
            timeInfo.name = timerInfo.name;
            timeInfo.depth = static_cast<uint32_t>(timerInfos.size()) - 1;

            timerInfos.pop_back();
        }
    }
#endif
};
#endif
