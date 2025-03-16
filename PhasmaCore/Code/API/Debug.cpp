#include "API/Debug.h"
#include "API/RHI.h"
#include "API/Queue.h"
#include "API/Command.h"
#include "RenderDoc/renderdoc_app.h"

#if defined(WIN32) && PE_RENDER_DOC == 1
#include <Windows.h>
#elif defined(__linux__) && PE_RENDER_DOC == 1
#include <dlfcn.h>
#endif
#include <filesystem>

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

    void Debug::StartFrameCapture()
    {
        if (capture_api)
            capture_api->StartFrameCapture(NULL, NULL);
    }

    void Debug::EndFrameCapture()
    {
        if (capture_api)
            capture_api->EndFrameCapture(NULL, NULL);
    }

    void Debug::TriggerCapture()
    {
        if (capture_api)
        {
            capture_api->TriggerCapture();
            if (capture_api->IsTargetControlConnected())
                capture_api->ShowReplayUI();
            else
                capture_api->LaunchReplayUI(true, "");
        }
    }
#endif
};