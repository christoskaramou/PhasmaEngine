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
SOFTWARE.m_device
*/

#if PE_VULKAN
#include "Renderer/Debug.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"

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
        static bool initialized = false;

        s_instance = instance;

        if (!s_instance)
            PE_ERROR("Invalid instance!");

        if (!initialized)
        {
            if (!vkCreateDebugUtilsMessengerEXT)
                vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");

            if (!vkDestroyDebugUtilsMessengerEXT)
                vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");

            if (!vkSetDebugUtilsObjectNameEXT)
                vkSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT");

            if (!vkQueueBeginDebugUtilsLabelEXT)
                vkQueueBeginDebugUtilsLabelEXT = (PFN_vkQueueBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkQueueBeginDebugUtilsLabelEXT");

            if (!vkQueueInsertDebugUtilsLabelEXT)
                vkQueueInsertDebugUtilsLabelEXT = (PFN_vkQueueInsertDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkQueueInsertDebugUtilsLabelEXT");

            if (!vkQueueEndDebugUtilsLabelEXT)
                vkQueueEndDebugUtilsLabelEXT = (PFN_vkQueueEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkQueueEndDebugUtilsLabelEXT");

            if (!vkCmdBeginDebugUtilsLabelEXT)
                vkCmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT");

            if (!vkCmdEndDebugUtilsLabelEXT)
                vkCmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT");

            if (!vkCmdInsertDebugUtilsLabelEXT)
                vkCmdInsertDebugUtilsLabelEXT = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdInsertDebugUtilsLabelEXT");

            initialized = true;
        }
    }

    void Debug::GetInstanceUtils(std::vector<const char *> &instanceExtensions,
                                 std::vector<const char *> &instanceLayers)
    {
        if (RHII.IsInstanceExtensionValid("VK_EXT_debug_utils"))
            instanceExtensions.push_back("VK_EXT_debug_utils");

#ifdef _DEBUG
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
#ifdef _DEBUG
        if (!s_instance)
            PE_ERROR("Invalid instance handle");

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
#ifdef _DEBUG
        if (s_debugMessenger)
        {
            vkDestroyDebugUtilsMessengerEXT(s_instance, s_debugMessenger, nullptr);
        }
#endif
    }

    void Debug::SetObjectName(uintptr_t object, ObjectType type, const std::string &name)
    {
        if (!vkSetDebugUtilsObjectNameEXT)
            return;

        VkDebugUtilsObjectNameInfoEXT objectInfo{};
        objectInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        objectInfo.pNext = VK_NULL_HANDLE;
        objectInfo.objectType = (VkObjectType)type;
        objectInfo.objectHandle = object;
        objectInfo.pObjectName = name.c_str();

        vkSetDebugUtilsObjectNameEXT(RHII.device, &objectInfo);
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

        vkQueueBeginDebugUtilsLabelEXT(queue->Handle(), &label);
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

        vkQueueInsertDebugUtilsLabelEXT(queue->Handle(), &label);
    }

    void Debug::EndQueueRegion(Queue *queue)
    {
        if (!vkQueueEndDebugUtilsLabelEXT)
            return;

        vkQueueEndDebugUtilsLabelEXT(queue->Handle());
        s_labelDepth--;
    }

    void Debug::BeginCmdRegion(CommandBufferHandle cmd, const std::string &name)
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

        vkCmdBeginDebugUtilsLabelEXT(cmd, &label);
    }

    void Debug::InsertCmdLabel(CommandBufferHandle cmd, const std::string &name)
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

        vkCmdInsertDebugUtilsLabelEXT(cmd, &label);
    }

    void Debug::EndCmdRegion(CommandBufferHandle cmd)
    {
        if (!vkCmdEndDebugUtilsLabelEXT)
            return;

        vkCmdEndDebugUtilsLabelEXT(cmd);
        s_labelDepth--;
    }
#endif
};
#endif
