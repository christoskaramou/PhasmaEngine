#include "API/Debug.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"
#include "API/Vulkan/VulkanQueueImpl.h"
#ifdef PE_TRACY
#include <tracy/TracyVulkan.hpp>
#endif
#include "RenderDoc/renderdoc_app.h"

#define PE_RENDER_DOC 1

#if defined(WIN32) && PE_RENDER_DOC == 1
#include <Windows.h>
#elif defined(__linux__) && PE_RENDER_DOC == 1
#include <dlfcn.h>
#endif

namespace pe
{
#if PE_DEBUG_MODE
    // Frame Capture
    void *capture_module = nullptr;
    RENDERDOC_API_1_5_0 *capture_api = nullptr;

#if PE_RENDER_DOC == 1
#if defined(WIN32)
    std::wstring TryGetRenderDocPath()
    {
        // Query registry for all the render doc paths
        static const wchar_t *pszInstallerFolders = TEXT("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\Folders");

        HKEY hkey;
        LSTATUS status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, pszInstallerFolders, 0, KEY_READ, &hkey);
        if (status != ERROR_SUCCESS) // ensure installer folders key is successfully opened
            return std::wstring();

        // Get the class name and the value count.
#define MAX_VALUE_NAME 8192
        TCHAR achClass[MAX_PATH] = TEXT("");
        DWORD cchClassName = MAX_PATH;
        DWORD cSubKeys = 0;
        DWORD cbMaxSubKey;
        DWORD cchMaxClass;
        DWORD cValues;
        DWORD cchMaxValue;
        DWORD cbMaxValueData;
        DWORD cbSecurityDescriptor;
        FILETIME ftLastWriteTime;

        wchar_t cbEnumValue[MAX_VALUE_NAME] = TEXT("");

        DWORD i, retCode;

        TCHAR achValue[MAX_VALUE_NAME];
        DWORD cchValue = MAX_VALUE_NAME;

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
            for (i = 0, retCode = ERROR_SUCCESS; i < cValues; i++)
            {
                cchValue = MAX_VALUE_NAME;
                achValue[0] = '\0';
                DWORD type = REG_SZ;
                DWORD size;
                memset(cbEnumValue, '\0', MAX_VALUE_NAME);

                retCode = RegEnumValue(hkey, i,
                                       achValue,
                                       &cchValue,
                                       nullptr,
                                       &type,
                                       nullptr,
                                       &size);

                if (type != REG_SZ || retCode != ERROR_SUCCESS)
                    continue;

                std::wstring path(achValue);
                if (path.find(L"RenderDoc") != std::wstring::npos)
                {
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
#elif defined(__linux__)
    std::string TryGetRenderDocPath()
    {
        // Search common RenderDoc installation paths on Linux
        std::vector<std::string> searchPaths = {
            "/usr/local/lib/renderdoc/",
            "/usr/lib/renderdoc/",
            "/opt/renderdoc/"};

        for (const auto &path : searchPaths)
        {
            if (std::filesystem::exists(path + "librenderdoc.so"))
                return path;
        }

        return std::string();
    }
#endif
#endif

    void Debug::InitCaptureApi()
    {
#if PE_RENDER_DOC == 1
#if defined(WIN32)
        std::wstring renderdoc_path = TryGetRenderDocPath();
        if (renderdoc_path.empty())
            renderdoc_path = L"renderdoc.dll"; // try to get it either way, maybe as an environment variable

        capture_module = ::GetModuleHandleW(std::wstring(renderdoc_path.begin(), renderdoc_path.end()).c_str());
        if (!capture_module)
            capture_module = ::LoadLibraryW(std::wstring(renderdoc_path.begin(), renderdoc_path.end()).c_str());

        if (capture_module)
        {
            pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)::GetProcAddress(static_cast<HMODULE>(capture_module), "RENDERDOC_GetAPI");

            int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_5_0, (void **)&capture_api);
            assert(ret == 1);

            capture_api->SetCaptureOptionU32(eRENDERDOC_Option_DebugOutputMute, 0);
            capture_api->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
            capture_api->SetCaptureKeys(nullptr, 0);  // disable all hotkey captures — only explicit API calls allowed
            capture_api->EndFrameCapture(NULL, NULL); // discard any auto-capture queued by injection
        }
#elif defined(__linux__)
        std::string renderdoc_path = TryGetRenderDocPath();
        if (renderdoc_path.empty())
            renderdoc_path = "librenderdoc.so";

        capture_module = dlopen(renderdoc_path.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (capture_module)
        {
            pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)dlsym(capture_module, "RENDERDOC_GetAPI");

            int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_5_0, (void **)&capture_api);
            assert(ret == 1);

            capture_api->SetCaptureOptionU32(eRENDERDOC_Option_DebugOutputMute, 0);
            capture_api->MaskOverlayBits(eRENDERDOC_Overlay_None, eRENDERDOC_Overlay_None);
            capture_api->SetCaptureKeys(nullptr, 0);  // disable all hotkey captures — only explicit API calls allowed
            capture_api->EndFrameCapture(NULL, NULL); // discard any auto-capture queued by injection
        }
#endif
#endif
    }

    void Debug::DestroyCaptureApi()
    {
#if defined(WIN32)
        if (capture_module != nullptr)
            ::FreeLibrary(static_cast<HMODULE>(capture_module));
#elif defined(__linux__)
        if (capture_module != nullptr)
            dlclose(capture_module);
#endif
    }

    void Debug::TriggerMultiFrameCapture(uint32_t numFrames)
    {
        if (capture_api)
        {
            capture_api->TriggerMultiFrameCapture(numFrames);
            ShowOrLaunchReplayUI();
        }
    }

    void Debug::ShowOrLaunchReplayUI()
    {
        if (capture_api)
        {
            if (capture_api->IsTargetControlConnected())
                capture_api->ShowReplayUI();
            else
                capture_api->LaunchReplayUI(true, "");
        }
    }

    uint32_t Debug::GetNumCaptures()
    {
        if (capture_api)
            return capture_api->GetNumCaptures();
        return 0;
    }

    bool Debug::IsCaptureApiAvailable()
    {
        return capture_api != nullptr;
    }

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
    void Debug::Init(vk::Instance instance)
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
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
        default:
            return "Unknown";
        }
    }

    uint32_t VKAPI_CALL MessageCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        uint32_t messageType,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData)
    {
        std::string msg = "VALIDATION: " + GetDebugMessageString(messageType) + " " +
                          GetDebugSeverityString(messageSeverity) + " from \"" +
                          pCallbackData->pMessageIdName + "\": " +
                          pCallbackData->pMessage;

        if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            pe::Log::Error(msg);
        else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            pe::Log::Warn(msg);
        else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
            pe::Log::Info(msg);
        else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
            pe::Log::Info(msg);

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

    void Debug::Destroy()
    {
        DestroyDebugMessenger();
        s_instance = nullptr;
        s_debugMessenger = nullptr;
        vkCreateDebugUtilsMessengerEXT = nullptr;
        vkDestroyDebugUtilsMessengerEXT = nullptr;
        vkSetDebugUtilsObjectNameEXT = nullptr;
        vkQueueBeginDebugUtilsLabelEXT = nullptr;
        vkQueueInsertDebugUtilsLabelEXT = nullptr;
        vkQueueEndDebugUtilsLabelEXT = nullptr;
        vkCmdBeginDebugUtilsLabelEXT = nullptr;
        vkCmdEndDebugUtilsLabelEXT = nullptr;
        vkCmdInsertDebugUtilsLabelEXT = nullptr;
    }

    void Debug::SetObjectName(const VkDebugUtilsObjectNameInfoEXT &info)
    {
        if (!vkSetDebugUtilsObjectNameEXT)
            return;

#if !defined(PE_TRACK_RESOURCES_NOSPAM)
        if (info.pObjectName)
            PE_INFO("Name %s set (Handle: %p)", info.pObjectName, info.objectHandle);
#endif

        vkSetDebugUtilsObjectNameEXT(VulkanRhi::Device(), &info);
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

        vkQueueBeginDebugUtilsLabelEXT(pe::GetVulkanQueue(queue), &label);
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

        vkQueueInsertDebugUtilsLabelEXT(pe::GetVulkanQueue(queue), &label);
    }

    void Debug::EndQueueRegion(Queue *queue)
    {
        if (!vkQueueEndDebugUtilsLabelEXT)
            return;

        vkQueueEndDebugUtilsLabelEXT(pe::GetVulkanQueue(queue));
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
        vkCmdBeginDebugUtilsLabelEXT(GetVulkanCommandBuffer(cmd), &label);

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

#ifdef PE_TRACY
        auto *scope = new tracy::VkCtxScope(
            VulkanRhi::TracyContext(),
            __LINE__, __FILE__, strlen(__FILE__),
            __FUNCTION__, strlen(__FUNCTION__),
            name.c_str(), name.size(),
            static_cast<VkCommandBuffer>(GetVulkanCommandBuffer(cmd)), true);
        cmd->m_tracyGpuScopes.push_back(scope);
#endif
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

        vkCmdInsertDebugUtilsLabelEXT(GetVulkanCommandBuffer(cmd), &label);
    }

    void Debug::EndCmdRegion(CommandBuffer *cmd)
    {
        if (!vkCmdEndDebugUtilsLabelEXT)
            return;

        vkCmdEndDebugUtilsLabelEXT(GetVulkanCommandBuffer(cmd));

        cmd->m_gpuTimerInfos[cmd->m_gpuTimerIdsStack.top()].timer->End();
        cmd->m_gpuTimerIdsStack.pop();

#ifdef PE_TRACY
        if (!cmd->m_tracyGpuScopes.empty())
        {
            delete cmd->m_tracyGpuScopes.back();
            cmd->m_tracyGpuScopes.pop_back();
        }
#endif
    }
#endif
} // namespace pe
