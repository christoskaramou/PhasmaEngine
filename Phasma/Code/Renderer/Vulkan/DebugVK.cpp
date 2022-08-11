#if PE_VULKAN
#include "Renderer/Debug.h"
#include "Renderer/RHI.h"
#include "Renderer/Queue.h"
#include "Renderer/Command.h"
#include "RenderDoc/renderdoc_app.h"

namespace pe
{
    // Frame Capture
    HMODULE capture_module = nullptr;
    RENDERDOC_API_1_5_0 *capture_api = nullptr;

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

    void Debug::Destroy()
    {
        DestroyCaptureApi();
        DestroyDebugMessenger();
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
        objectInfo.objectType = Translate<VkObjectType>(type);
        objectInfo.objectHandle = object;
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

        vkCmdBeginDebugUtilsLabelEXT(cmd->Handle(), &label);
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

        vkCmdInsertDebugUtilsLabelEXT(cmd->Handle(), &label);
    }

    void Debug::EndCmdRegion(CommandBuffer *cmd)
    {
        if (!vkCmdEndDebugUtilsLabelEXT)
            return;

        vkCmdEndDebugUtilsLabelEXT(cmd->Handle());
        s_labelDepth--;
    }

#if defined(WIN32) && PE_RENDER_DOC == 1
    std::wstring TryGetRenderDocPath()
    {
        // Query registry for all the render doc paths
        static const wchar_t *pszInstallerFolders = TEXT("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\Folders");

        HKEY hkey;
        LSTATUS status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, pszInstallerFolders, 0, KEY_READ, &hkey);
        if (status != ERROR_SUCCESS) // ensure installer folders key is successfully opened
            return std::wstring();

#define MAX_KEY_LENGTH 255
#define MAX_VALUE_NAME 8192
        TCHAR achClass[MAX_PATH] = TEXT(""); // buffer for class name
        DWORD cchClassName = MAX_PATH;       // size of class string
        DWORD cSubKeys = 0;                  // number of subkeys
        DWORD cbMaxSubKey;                   // longest subkey size
        DWORD cchMaxClass;                   // longest class string
        DWORD cValues;                       // number of values for keyPath
        DWORD cchMaxValue;                   // longest value name
        DWORD cbMaxValueData;                // longest value data
        DWORD cbSecurityDescriptor;          // size of security descriptor
        FILETIME ftLastWriteTime;            // last write time

        wchar_t cbEnumValue[MAX_VALUE_NAME] = TEXT("");

        DWORD i, retCode;

        TCHAR achValue[MAX_VALUE_NAME];
        DWORD cchValue = MAX_VALUE_NAME;

        // Get the class name and the value count.
        retCode = RegQueryInfoKey(
            hkey,                  // keyPath handle
            achClass,              // buffer for class name
            &cchClassName,         // size of class string
            nullptr,               // reserved
            &cSubKeys,             // number of subkeys
            &cbMaxSubKey,          // longest subkey size
            &cchMaxClass,          // longest class string
            &cValues,              // number of values for this keyPath
            &cchMaxValue,          // longest value name
            &cbMaxValueData,       // longest value data
            &cbSecurityDescriptor, // security descriptor
            &ftLastWriteTime);     // last write time

        if (cValues)
        {
            // printf("\nNumber of values: %d\n", cValues);
            for (i = 0, retCode = ERROR_SUCCESS; i < cValues; i++)
            {
                cchValue = MAX_VALUE_NAME;
                achValue[0] = '\0';
                DWORD type = REG_SZ;
                DWORD size;
                memset(cbEnumValue, '\0', MAX_VALUE_NAME);

                // MSDN:  https://docs.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regenumvaluea
                // If the data has the REG_SZ, REG_MULTI_SZ or REG_EXPAND_SZ type, the string may not have been stored with
                // the proper null-terminating characters. Therefore, even if the function returns ERROR_SUCCESS, the application
                // should ensure that the string is properly terminated before using it; otherwise, it may overwrite a buffer.
                retCode = RegEnumValue(hkey, i,
                                       achValue,
                                       &cchValue,
                                       nullptr,
                                       &type,
                                       nullptr,
                                       &size);

                if (type != REG_SZ || retCode != ERROR_SUCCESS)
                    continue;

                retCode = RegQueryInfoKey(
                    hkey,                  // keyPath handle
                    achClass,              // buffer for class name
                    &cchClassName,         // size of class string
                    nullptr,               // reserved
                    &cSubKeys,             // number of subkeys
                    &cbMaxSubKey,          // longest subkey size
                    &cchMaxClass,          // longest class string
                    &cValues,              // number of values for this keyPath
                    &cchMaxValue,          // longest value name
                    &cbMaxValueData,       // longest value data
                    &cbSecurityDescriptor, // security descriptor
                    &ftLastWriteTime);     // last write time

                std::wstring path(achValue);
                if (path.find(L"RenderDoc") != std::wstring::npos)
                {
                    // many paths qualify:
                    //
                    // "C:\\Program Files\\RenderDoc\\plugins\\amd\\counters\\"
                    // "C:\\Program Files\\RenderDoc\\"
                    // "C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\RenderDoc\\"
                    //
                    // Only consider the ones the contain the dll we want
                    const std::wstring rdc_dll_path = path += TEXT("renderdoc.dll");
                    WIN32_FIND_DATA find_file_data = {0};
                    HANDLE file_handle = FindFirstFile(rdc_dll_path.c_str(), &find_file_data);

                    if (file_handle != INVALID_HANDLE_VALUE)
                    {
                        RegCloseKey(hkey);
                        return path;
                    }
                }
            }
        }

        return std::wstring();
    }
#endif

    void Debug::InitCaptureApi()
    {
#if defined(WIN32) && PE_RENDER_DOC == 1
        std::wstring renderdoc_path = TryGetRenderDocPath();
        if (renderdoc_path.empty())
            renderdoc_path = TEXT("renderdoc.dll"); // try to get it either way, maybe as an evniroment variable

        capture_module = ::GetModuleHandleW(renderdoc_path.c_str());
        if (!capture_module)
            capture_module = ::LoadLibraryW(renderdoc_path.c_str());

        if (capture_module)
        {
            pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)::GetProcAddress(capture_module, "RENDERDOC_GetAPI");
            int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_5_0, (void **)&capture_api);
            assert(ret == 1);

            capture_api->SetCaptureOptionU32(eRENDERDOC_Option_DebugOutputMute, 0);
            capture_api->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
        }
#endif
    }

    void Debug::DestroyCaptureApi()
    {
        if (capture_module != nullptr)
            ::FreeLibrary(capture_module);
    }

    void Debug::StartFrameCapture()
    {
#if PE_RENDER_DOC == 1
        if (capture_api)
            capture_api->StartFrameCapture(NULL, NULL);
#endif
    }

    void Debug::EndFrameCapture()
    {
#if PE_RENDER_DOC == 1
        if (capture_api)
            capture_api->EndFrameCapture(NULL, NULL);
#endif
    }

    void Debug::TriggerCapture()
    {
#if PE_RENDER_DOC == 1
        if (capture_api)
        {
            capture_api->TriggerCapture();
            if (capture_api->IsTargetControlConnected())
                capture_api->ShowReplayUI();

            capture_api->LaunchReplayUI(true, "");
        }
#endif
    }
#endif
};
#endif
